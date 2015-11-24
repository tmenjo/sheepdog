/*
 * Copyright (C) 2009-2011 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * This code is based on bs.c from Linux target framework (tgt):
 *   Copyright (C) 2007 FUJITA Tomonori <tomof@acm.org>
 *   Copyright (C) 2007 Mike Christie <michaelc@cs.wisc.edu>
 */
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <syscall.h>
#include <sys/types.h>
#include <sys/time.h>
#include <linux/types.h>
#include <signal.h>
#include <sched.h>

#include "common.h"
#include "list.h"
#include "util.h"
#include "bitops.h"
#include "work.h"
#include "event.h"

#define TRACEPOINT_DEFINE
#include "work_tp.h"

/*
 * The protection period from shrinking work queue.  This is necessary
 * to avoid many calls of pthread_create.  Without it, threads are
 * frequently created and deleted and it leads poor performance.
 */
#define WQ_PROTECTION_PERIOD 1000 /* ms */

struct wq_info {
	const char *name;

	struct list_head finished_list;
	struct list_node list;

	struct sd_mutex finished_lock;

	/* workers sleep on this and signaled by work producer */
	struct sd_cond *pending_conds;
	/* locked by work producer and workers */
	struct sd_mutex *pending_locks;
	/* protected by pending_locks */
	struct work_queue q;

	/* we cannot shrink work queue till this time */
	uint64_t tm_end_of_protection;
	enum wq_thread_control tc;

	/* worker_routine write to this after initialization */
	int create_efd;
};

static int efd;
static LIST_HEAD(wq_info_list);
static size_t nr_nodes = 1;
static size_t (*wq_get_nr_nodes)(void);

static void *worker_routine(void *arg);

unsigned long nr_cores;

static int mycpu(void)
{
	int ret = sched_getcpu();

	if (ret < 0) {
		sd_warn("this kernel doesn't support sched_getcpu() syscall: %m");
		ret = 0;	/* CPU0 must exist in all systems */
	}

	return ret;
}

#ifdef HAVE_TRACE

#define TID_MAX_DEFAULT 0x8000 /* default maximum tid for most systems */

static size_t tid_max;
static unsigned long *tid_map;
static struct sd_mutex tid_map_lock = SD_MUTEX_INITIALIZER;

static int resume_efd;
static int ack_efd;

void suspend_worker_threads(void)
{
	struct wq_info *wi;
	int tid;

	for (int i = 0; i < nr_cores; i++) {
		list_for_each_entry(wi, &wq_info_list, list) {
			sd_mutex_lock(&wi->pending_locks[i]);
		}
	}

	FOR_EACH_BIT(tid, tid_map, tid_max) {
		if (unlikely(tkill(tid, SIGUSR2) < 0))
			panic("%m");
	}

	/*
	 * Wait for all the worker thread to suspend.  We cannot use
	 * wi->nr_threads here because some thread may have not called set_bit()
	 * yet (then, the thread doesn't receive SIGUSR2).
	 */
	FOR_EACH_BIT(tid, tid_map, tid_max) {
		eventfd_xread(ack_efd);
	}
}

void resume_worker_threads(void)
{
	struct wq_info *wi;
	int nr_threads = 0, tid;

	FOR_EACH_BIT(tid, tid_map, tid_max) {
		nr_threads++;
	}

	eventfd_xwrite(resume_efd, nr_threads);
	for (int i = 0; i < nr_threads; i++)
		eventfd_xread(ack_efd);

	for (int i = 0; i < nr_cores; i++) {
		list_for_each_entry(wi, &wq_info_list, list) {
			sd_mutex_unlock(&wi->pending_locks[i]);
		}
	}
}

static void suspend(int num, siginfo_t *info, void *context)
{
	int uninitialized_var(value);

	eventfd_xwrite(ack_efd, 1); /* ack of suspend */
	value = eventfd_xread(resume_efd);
	sd_assert(value == 1);
	eventfd_xwrite(ack_efd, 1); /* ack of resume */
}

int wq_trace_init(void)
{
	tid_max = TID_MAX_DEFAULT;
	tid_map = alloc_bitmap(NULL, 0, tid_max);

	resume_efd = eventfd(0, EFD_SEMAPHORE);
	ack_efd = eventfd(0, EFD_SEMAPHORE);

	if (resume_efd < 0 || ack_efd < 0) {
		sd_err("failed to create event fds: %m");
		return -1;
	}

	/* trace uses this signal to suspend the worker threads */
	if (install_sighandler(SIGUSR2, suspend, false) < 0) {
		sd_debug("%m");
		return -1;
	}
	return 0;
}

static void trace_set_tid_map(int tid)
{
	sd_mutex_lock(&tid_map_lock);
	if (tid > tid_max) {
		size_t old_tid_max = tid_max;

		/* enlarge bitmap size */
		while (tid > tid_max)
			tid_max *= 2;

		tid_map = alloc_bitmap(tid_map, old_tid_max, tid_max);
	}
	set_bit(tid, tid_map);
	sd_mutex_unlock(&tid_map_lock);
}

static void trace_clear_tid_map(int tid)
{
	sd_mutex_lock(&tid_map_lock);
	clear_bit(tid, tid_map);
	sd_mutex_unlock(&tid_map_lock);
}

#else

int wq_trace_init(void) { return 0; }
static inline void trace_set_tid_map(int tid) {}
static inline void trace_clear_tid_map(int tid) {}

#endif	/* HAVE_TRACE */

static uint64_t get_msec_time(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static inline uint64_t wq_get_roof(struct wq_info *wi)
{
	uint64_t nr = 1;

	switch (wi->tc) {
	case WQ_ORDERED:
		break;
	case WQ_DYNAMIC:
		/* FIXME: 2 * nr_nodes threads. No rationale yet. */
		nr = nr_nodes * 2;
		break;
	case WQ_UNLIMITED:
		nr = SIZE_MAX;
		break;
	default:
		panic("Invalid threads control %d", wi->tc);
	}
	return nr;
}

static bool wq_need_grow(struct wq_info *wi, int cpu)
{
	if (wi->q.nr_threads[cpu] < uatomic_read(&wi->q.nr_queued_work[cpu]) &&
	    wi->q.nr_threads[cpu] * 2 <= wq_get_roof(wi)) {
		wi->tm_end_of_protection = get_msec_time() +
			WQ_PROTECTION_PERIOD;
		return true;
	}

	return false;
}

/*
 * Return true if more than half of threads are not used more than
 * WQ_PROTECTION_PERIOD seconds
 */
static bool wq_need_shrink(struct wq_info *wi, int cpu)
{
	if (uatomic_read(&wi->q.nr_queued_work[cpu]) == 1)
		/* we need at least one thread on each core */
		return false;

	if (uatomic_read(&wi->q.nr_queued_work[cpu]) < wi->q.nr_threads[cpu] / 2)
		/* we cannot shrink work queue during protection period. */
		return wi->tm_end_of_protection <= get_msec_time();

	/* update the end of protection time */
	wi->tm_end_of_protection = get_msec_time() + WQ_PROTECTION_PERIOD;
	return false;
}

static int create_worker_threads(struct wq_info *wi, size_t nr_threads, int core)
{
        pthread_t thread;
        cpu_set_t cpuset;
        int ret;

        CPU_ZERO(&cpuset);
	CPU_SET(core, &cpuset);

	while (wi->q.nr_threads[core] < nr_threads) {
		ret = pthread_create(&thread, NULL, worker_routine, wi);
		if (ret != 0) {
			sd_err("failed to create worker thread: %m");
			return -1;
		}

		if (wi->q.wq_state != WQ_ORDERED &&
		    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset))
			sd_info("set thread affinity failed");

		eventfd_xwrite(wi->create_efd, 1);

		wi->q.nr_threads[core]++;
		sd_debug("create thread %s %zu (CPU: %d)", wi->name,
			 wi->q.nr_threads[core], core);
	}

        return 0;
}

void queue_work(struct work_queue *q, struct work *work)
{
	struct wq_info *wi = container_of(q, struct wq_info, q);
	int cpu;

	if (q->wq_state == WQ_ORDERED)
		cpu = 0;
	else if (is_main_thread()) {
		static __thread int next_core;
		cpu = next_core++ % nr_cores;
	} else
		cpu = mycpu();

	tracepoint(work, queue_work, wi, work);

	uatomic_inc(&wi->q.nr_queued_work[cpu]);
	sd_mutex_lock(&wi->pending_locks[cpu]);

	work->cpu = cpu;

	if (wq_need_grow(wi, cpu))
		/* double the thread pool size */
		create_worker_threads(wi, wi->q.nr_threads[cpu] * 2, cpu);

	list_add_tail(&work->w_list, &wi->q.pending_lists[cpu]);
	sd_mutex_unlock(&wi->pending_locks[cpu]);

	sd_cond_signal(&wi->pending_conds[cpu]);
}

static void worker_thread_request_done(int fd, int events, void *data)
{
	struct wq_info *wi;
	struct work *work;
	LIST_HEAD(list);

	if (wq_get_nr_nodes)
		nr_nodes = wq_get_nr_nodes();

	eventfd_xread(fd);

	list_for_each_entry(wi, &wq_info_list, list) {
		sd_mutex_lock(&wi->finished_lock);
		list_splice_init(&wi->finished_list, &list);
		sd_mutex_unlock(&wi->finished_lock);

		while (!list_empty(&list)) {
			work = list_first_entry(&list, struct work, w_list);
			list_del(&work->w_list);

			tracepoint(work, request_done, wi, work);

			uatomic_dec(&wi->q.nr_queued_work[work->cpu]);
			work->done(work);
		}
	}
}

static void *worker_routine(void *arg)
{
	struct wq_info *wi = arg;
	struct work *work;
	int tid = gettid();
	int cpu;

	set_thread_name(wi->name, (wi->tc != WQ_ORDERED));

	trace_set_tid_map(tid);

	/* wait pthread_setaffinity_np() in create_worker_threads() */
	eventfd_xread(wi->create_efd);
	/* now I'm running on correct cpu */
	cpu = wi->q.wq_state == WQ_ORDERED ? 0 : mycpu();

	while (true) {

		sd_mutex_lock(&wi->pending_locks[cpu]);
		if (wq_need_shrink(wi, cpu) &&
		    nr_cores < wi->q.nr_threads[cpu]) {
			wi->q.nr_threads[cpu]--;

			trace_clear_tid_map(tid);
			sd_mutex_unlock(&wi->pending_locks[cpu]);
			pthread_detach(pthread_self());
			sd_debug("destroy thread %s %d, %zu", wi->name, tid,
				 wi->q.nr_threads[cpu]);
			break;
		}
retest:
		if (list_empty(&wi->q.pending_lists[cpu])) {
			sd_cond_wait(&wi->pending_conds[cpu],
				     &wi->pending_locks[cpu]);
			goto retest;
		}

		work = list_first_entry(&wi->q.pending_lists[cpu],
				       struct work, w_list);

		list_del(&work->w_list);
		sd_mutex_unlock(&wi->pending_locks[cpu]);

		tracepoint(work, do_work, wi, work);

		if (work->fn)
			work->fn(work);

		sd_mutex_lock(&wi->finished_lock);
		list_add_tail(&work->w_list, &wi->finished_list);
		sd_mutex_unlock(&wi->finished_lock);

		eventfd_xwrite(efd, 1);
	}

	pthread_exit(NULL);
}

int init_work_queue(size_t (*get_nr_nodes)(void))
{
	int ret;

	wq_get_nr_nodes = get_nr_nodes;

	if (wq_get_nr_nodes)
		nr_nodes = wq_get_nr_nodes();

	efd = eventfd(0, EFD_NONBLOCK);
	if (efd < 0) {
		sd_err("failed to create event fd: %m");
		return -1;
	}

	ret = register_event(efd, worker_thread_request_done, NULL);
	if (ret) {
		sd_err("failed to register event fd %m");
		close(efd);
		return -1;
	}

	return 0;
}

/*
 * Allowing unlimited threads to be created is necessary to solve the following
 * problems:
 *
 *  1. timeout of IO requests from guests. With on-demand short threads, we
 *     guarantee that there is always one thread available to execute the
 *     request as soon as possible.
 *  2. sheep halt for corner case that all gateway and io threads are executing
 *     local requests that ask for creation of another thread to execute the
 *     requests and sleep-wait for responses.
 */
struct work_queue *create_work_queue(const char *name,
				     enum wq_thread_control tc)
{
	int ret;
	struct wq_info *wi;

	wi = xzalloc(sizeof(*wi));
	wi->name = name;
	wi->tc = tc;

	wi->q.pending_lists = xcalloc(nr_cores, sizeof(wi->q.pending_lists[0]));
	for (int i = 0; i < nr_cores; i++)
		INIT_LIST_HEAD(&wi->q.pending_lists[i]);

	wi->q.nr_threads = xcalloc(nr_cores, sizeof(wi->q.nr_threads[0]));
	wi->q.nr_queued_work = xcalloc(nr_cores, sizeof(wi->q.nr_queued_work[0]));

	INIT_LIST_HEAD(&wi->finished_list);

	wi->pending_conds = xcalloc(nr_cores, sizeof(wi->pending_conds[0]));
	for (int i = 0; i < nr_cores; i++)
		sd_cond_init(&wi->pending_conds[i]);

	sd_init_mutex(&wi->finished_lock);

	wi->pending_locks = xcalloc(nr_cores, sizeof(wi->pending_locks[0]));
	for (int i = 0; i < nr_cores; i++)
		sd_init_mutex(&wi->pending_locks[i]);

	wi->create_efd = eventfd(0, EFD_SEMAPHORE);
	if (wi->create_efd < 0)
		panic("failed to create efd for workqueue: %m");

	if (tc == WQ_ORDERED) {
		ret = create_worker_threads(wi, 1, 0);
		if (ret < 0)
			goto destroy_threads;
	} else {
		for (int i = 0; i < nr_cores; i++) {
			ret = create_worker_threads(wi, 1, i);
			if (ret < 0)
				goto destroy_threads;
		}
	}

	list_add(&wi->list, &wq_info_list);

	tracepoint(work, create_queue, wi->name, wi, tc);
	return &wi->q;
destroy_threads:
	for (int i = 0; i < nr_cores; i++)
		sd_destroy_cond(&wi->pending_conds[i]);
	free(wi->pending_conds);

	for (int i = 0; i < nr_cores; i++)
		sd_destroy_mutex(&wi->pending_locks[i]);
	free(wi->pending_locks);

	sd_destroy_mutex(&wi->finished_lock);

	free(wi->q.nr_queued_work);
	free(wi->q.nr_threads);
	free(wi->q.pending_lists);

	free(wi);

	return NULL;
}

struct work_queue *create_ordered_work_queue(const char *name)
{
	return create_work_queue(name, WQ_ORDERED);
}

bool work_queue_empty(struct work_queue *q, int core)
{
	struct wq_info *wi = container_of(q, struct wq_info, q);

	return uatomic_read(&wi->q.nr_queued_work[core]) == 0;
}

bool work_queue_empty_all(struct work_queue *q)
{
	struct wq_info *wi = container_of(q, struct wq_info, q);

	for (int core = 0; core < nr_cores; core++) {
		if (uatomic_read(&wi->q.nr_queued_work[core]) != 0)
			return false;
	}

	return true;
}

struct thread_args {
	const char *name;
	void *(*start_routine)(void *);
	void *arg;
	bool show_idx;
};

static void *thread_starter(void *arg)
{
	struct thread_args *args = (struct thread_args *)arg;
	void *ret;

	set_thread_name(args->name, args->show_idx);
	ret = args->start_routine(args->arg);
	free(arg);

	return ret;
}

static int __sd_thread_create(const char *name, sd_thread_t *thread,
			      void *(*start_routine)(void *), void *arg,
			      bool show_idx)
{
	struct thread_args *args;


	args = xzalloc(sizeof(*args));
	args->name = name;
	args->start_routine = start_routine;
	args->arg = arg;
	args->show_idx = show_idx;

	/*
	 * currently, there isn't a thread which uses ptread_attr_t
	 * in sheepdog
	 */
	return pthread_create(thread, NULL, thread_starter, args);
}

int sd_thread_create(const char *name, sd_thread_t *thread,
		     void *(*start_routine)(void *), void *arg)
{
	return __sd_thread_create(name, thread, start_routine, arg, false);
}

int sd_thread_create_with_idx(const char *name, sd_thread_t *thread,
			      void *(*start_routine)(void *), void *arg)
{
	return __sd_thread_create(name, thread, start_routine, arg, true);
}

int sd_thread_join(sd_thread_t thread, void **retval)
{
	return pthread_join(thread, retval);
}

static void __attribute__((constructor)) __nr_cores_init(void)
{
	nr_cores = sysconf(_SC_NPROCESSORS_ONLN);
}
