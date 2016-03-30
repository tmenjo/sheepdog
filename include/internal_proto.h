/*
 * Copyright (C) 2009-2011 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __INTERNAL_PROTO_H__
#define __INTERNAL_PROTO_H__

/*
 * This file specified the sheepdog-internal protocol, which is spoken between
 * sheepdog daemons, as well as between dog and sheepdog daemon for internal
 * operations.
 */

#include <stdint.h>
#include <netinet/in.h>

#include "sheepdog_proto.h"
#include "rbtree.h"
#include "fec.h"

#define SD_SHEEP_PROTO_VER 0x0a

#define SD_DEFAULT_COPIES 3
/*
 * For erasure coding, we use at most SD_EC_MAX_STRIP for data strips and
 * (SD_EC_MAX_STRIP - 1) for parity strips
 */
#define SD_MAX_COPIES (SD_EC_MAX_STRIP * 2 - 1)

/*
 * The max number of nodes sheep daemon can support is constrained by
 * the number of nodes in the struct cluster_info, but the actual max
 * number is determined by the cluster driver because we have to pass
 * sys->cinfo around the cluster to handle membership management.
 *
 * Currently, only zookeeper driver support SD_MAX_NODES nodes because
 * its message buffer size is large enough to hold nodes[SD_MAX_NODES].
 */
#define SD_DEFAULT_VNODES 128

/*
 * Operations with opcodes above 0x80 are considered part of the inter-sheep
 * include sheep-dog protocol and are versioned using SD_SHEEP_PROTO_VER
 * instead of SD_PROTO_VER.
 *
 * These same applies for the above 0x80 flags and error values below.
 */
#define SD_OP_GET_NODE_LIST  0x82
#define SD_OP_MAKE_FS        0x84
#define SD_OP_SHUTDOWN       0x85
#define SD_OP_STAT_SHEEP     0x86
#define SD_OP_STAT_CLUSTER   0x87
#define SD_OP_GET_VDI_ATTR   0x89
#define SD_OP_FORCE_RECOVER  0x8a
#define SD_OP_GET_STORE_LIST 0x90
#define SD_OP_SNAPSHOT       0x91
#define SD_OP_RESTORE        0x92
#define SD_OP_GET_SNAP_FILE  0x93
#define SD_OP_CLEANUP        0x94
#define SD_OP_TRACE_STATUS   0x95
#define SD_OP_TRACE_READ_BUF 0x96
#define SD_OP_STAT_RECOVERY  0x97
#define SD_OP_FLUSH_DEL_CACHE  0x98
#define SD_OP_NOTIFY_VDI_DEL 0x99
#define SD_OP_KILL_NODE      0x9A
#define SD_OP_TRACE_ENABLE   0x9B
#define SD_OP_TRACE_DISABLE  0x9C
#define SD_OP_GET_OBJ_LIST   0xA1
#define SD_OP_GET_EPOCH      0xA2
#define SD_OP_CREATE_AND_WRITE_PEER 0xA3
#define SD_OP_READ_PEER      0xA4
#define SD_OP_WRITE_PEER     0xA5
#define SD_OP_REMOVE_PEER    0xA6
/* #define SD_OP_SET_CACHE_SIZE 0xA7 deleted */
#define SD_OP_ENABLE_RECOVER 0xA8
#define SD_OP_DISABLE_RECOVER 0xA9
#define SD_OP_GET_VDI_COPIES 0xAB
#define SD_OP_COMPLETE_RECOVERY 0xAC
#define SD_OP_FLUSH_NODES 0xAD
#define SD_OP_FLUSH_PEER 0xAE
#define SD_OP_NOTIFY_VDI_ADD  0xAF
#define SD_OP_DELETE_CACHE    0xB0
#define SD_OP_MD_INFO   0xB1
#define SD_OP_MD_PLUG   0xB2
#define SD_OP_MD_UNPLUG 0xB3
#define SD_OP_GET_HASH       0xB4
#define SD_OP_REWEIGHT       0xB5
#define SD_OP_GET_CACHE_INFO 0xB6 /* obsolete */
#define SD_OP_CACHE_PURGE    0xB7 /* obsolete */
#define SD_OP_STAT	0xB8
#define SD_OP_GET_LOGLEVEL	0xB9
#define SD_OP_SET_LOGLEVEL	0xBA
#define SD_OP_NFS_CREATE	0xBB
#define SD_OP_NFS_DELETE	0xBC
#define SD_OP_EXIST	0xBD
#define SD_OP_CLUSTER_INFO	0xBE
#define SD_OP_ALTER_CLUSTER_COPY	0xBF
#define SD_OP_ALTER_VDI_COPY	0xC0
#define SD_OP_DECREF_OBJ     0xC1
#define SD_OP_DECREF_PEER    0xC2
#define SD_OP_PREVENT_INODE_UPDATE    0xC3 /* obsolete */
#define SD_OP_ALLOW_INODE_UPDATE      0xC4 /* obsolete */
#define SD_OP_REPAIR_REPLICA	0xC5
#define SD_OP_OIDS_EXIST	0xC6
#define SD_OP_VDI_STATE_CHECKPOINT_CTL  0xC7
#define SD_OP_INODE_COHERENCE 0xC8
#define SD_OP_READ_DEL_VDIS  0xC9
#define SD_OP_GET_RECOVERY      0xCA
#define SD_OP_SET_RECOVERY      0xCB
#define SD_OP_SET_VNODES 0xCC
#define SD_OP_GET_VNODES 0xCD

/* internal flags for hdr.flags, must be above 0x80 */
#define SD_FLAG_CMD_RECOVERY 0x0080
#define SD_FLAG_CMD_WILDCARD 0x0100

/* flags for VDI attribute operations */
#define SD_FLAG_CMD_CREAT    0x0100
#define SD_FLAG_CMD_EXCL     0x0200
#define SD_FLAG_CMD_DEL      0x0400

/* internal error return values, must be above 0x80 */
#define SD_RES_OLD_NODE_VER  0x81 /* Request has an old epoch */
#define SD_RES_NEW_NODE_VER  0x82 /* Request has a new epoch */
#define SD_RES_NOT_FORMATTED 0x83 /* Sheepdog is not formatted yet */
#define SD_RES_INVALID_CTIME 0x84 /* Creation time of sheepdog is different */
#define SD_RES_INVALID_EPOCH 0x85 /* Invalid epoch */
#define SD_RES_NETWORK_ERROR 0x86 /* Network error between sheep */
#define SD_RES_NO_CACHE      0x87 /* No cache object found */
#define SD_RES_BUFFER_SMALL  0x88 /* The buffer is too small */
#define SD_RES_FORCE_RECOVER    0x89 /* Users should not force recover this cluster */
#define SD_RES_NO_STORE         0x8A /* No targeted backend store */
#define SD_RES_NO_SUPPORT       0x8B /* Operation is not supported by backend store */
#define SD_RES_NODE_IN_RECOVERY 0x8C /*	Targeted node is in recovery */
#define SD_RES_KILLED           0x8D /* Node is killed */
#define SD_RES_OID_EXIST        0x8E /* Object ID exists already */
#define SD_RES_AGAIN            0x8F /* Ask to try again */
#define SD_RES_STALE_OBJ        0x90 /* Object may be stale */
#define SD_RES_CLUSTER_ERROR    0x91 /* Cluster driver error */
#define SD_RES_VDI_NOT_EMPTY    0x92 /* VDI is not empty */
#define SD_RES_NOT_FOUND	0x93 /* Cannot found target */
#define SD_RES_INCOMPLETE    0x94 /* Object (in kv) is incomplete uploading */
/* sheep is collecting cluster wide status, not ready for operation */
#define SD_RES_COLLECTING_CINFO 0x95
#define SD_RES_GATEWAY_MODE  0x97 /* Target node is gateway mode */
#define SD_RES_INVALID_VNODES_STRATEGY 0x98 /* Invalid vnodes strategy */
/* Node doesn't have a required entry of checkpoint */
#define SD_RES_NO_CHECKPOINT_ENTRY 0x99

#define SD_CLUSTER_FLAG_STRICT		0x0001 /* Strict mode for write */
#define SD_CLUSTER_FLAG_DISKMODE	0x0002 /* Disk mode for cluster */
#define SD_CLUSTER_FLAG_AUTO_VNODES	0x0004 /* Cluster vnodes strategy */
#define SD_CLUSTER_FLAG_USE_LOCK	0x0008 /* Lock/Unlock vdi */
#define SD_CLUSTER_FLAG_RECYCLE_VID	0x0010 /* Enable recycling of VID */


enum sd_status {
	SD_STATUS_OK = 1,
	SD_STATUS_WAIT,
	SD_STATUS_SHUTDOWN,
	SD_STATUS_KILLED,
};

enum sd_node_status {
	SD_NODE_STATUS_INITIALIZATION = 1,
	SD_NODE_STATUS_COLLECTING_CINFO,
	SD_NODE_STATUS_OK,
};

#ifdef HAVE_ACCELIO
#define IO_TRANSPORT_TYPE_TCP 1
#define IO_TRANSPORT_TYPE_RDMA 2
#endif

struct node_id {
	uint8_t addr[16];
	uint16_t port;
	uint8_t io_addr[16];
	uint16_t io_port;
#ifndef HAVE_ACCELIO
	uint8_t pad[4];
#else
	uint8_t io_transport_type;
	uint8_t pad[3];
#endif
};

struct disk_info {
	uint64_t disk_id;
	uint64_t disk_space;
};

#define DISK_MAX     32
#define WEIGHT_MIN   (1ULL << 32)       /* 4G */


/*
 * struct sd_node_0_7: an old sd_node struct used in 0.7.x
 * This struct is defined just for upgrading.
 */
struct sd_node_0_7 {
	struct node_id nid;
	uint16_t       nr_vnodes;
	uint32_t       zone;
	uint64_t       space;
};

/*
 * struct sd_node_0_8: an old sd_node struct used in 0.8.x
 * This struct is defined just for upgrading.
 * The difference is just things related to diskvnodes.
 */
struct sd_node_0_8 {
	struct rb_node rb;
	struct node_id nid;
	uint16_t       nr_vnodes;
	uint32_t       zone;
	uint64_t       space;
};

struct sd_node {
	struct rb_node  rb;
	struct node_id  nid;
	uint16_t	nr_vnodes;
	uint32_t	zone;
	uint64_t        space;
#ifdef HAVE_DISKVNODES
	#define SD_MAX_NODES 830
	#define SD_NODE_SIZE (80 + sizeof(struct disk_info) * DISK_MAX)
	struct disk_info disks[DISK_MAX];
#else
	#define SD_MAX_NODES 6144
	#define SD_NODE_SIZE 80
	struct disk_info disks[0];
#endif
};

struct oid_entry {
	struct rb_node rb;
	struct sd_node *node; /* key */
	uint64_t *oids;       /* object id array */
	int end;              /* idx to the end of the allocated oid array */
	int last;             /* idx to the last element of the oid array */
};

/*
 * node_info: node specific configuration
 * This is a config data initialized with "dog node format".
 * Currently, it is very simple so doesn't have different expressions
 * like cluster_info and sheepdog_config.
 */
struct node_info {
	uint8_t store[STORE_LEN];
};

#define NODE_CONFIG_PATH "node_config"

/*
 * A joining sheep multicasts the local cluster info.  Then, the existing nodes
 * reply the latest cluster info which is unique among all of the nodes.
 */
struct cluster_info {
	uint8_t proto_ver; /* the version number of the internal protocol */
	uint8_t disable_recovery;
	int16_t nr_nodes;
	uint32_t epoch;
	uint64_t ctime;
	uint16_t flags;
	uint8_t nr_copies;
	uint8_t copy_policy;
	enum sd_status status : 8;
	uint8_t block_size_shift;
	uint8_t __pad[3];
	uint8_t default_store[STORE_LEN];

	/* Node list at cluster_info->epoch */
	struct sd_node nodes[SD_MAX_NODES];
};

struct epoch_log {
	uint64_t ctime;
	uint64_t time;		/* treated as time_t */
	uint32_t epoch;
	uint32_t nr_nodes;
	uint8_t  disable_recovery;
	uint8_t  nr_copies;
	uint8_t  copy_policy;
	uint8_t  __pad[3];
	uint16_t flags;
	char drv_name[STORE_LEN];
	struct sd_node nodes[0];
};

struct vdi_op_message {
	struct sd_req req;
	struct sd_rsp rsp;
	uint8_t data[0];
};

struct md_info {
	int idx;
	uint64_t free;
	uint64_t used;
	char path[PATH_MAX];
};

#define MD_MAX_DISK 64 /* FIXME remove roof and make it dynamic */
struct sd_md_info {
	struct md_info disk[MD_MAX_DISK];
	int nr;
};

static inline __attribute__((used)) void __sd_epoch_format_build_bug_ons(void)
{
	/* never called, only for checking BUILD_BUG_ON()s */
	BUILD_BUG_ON(sizeof(struct sd_node) != SD_NODE_SIZE);
}

enum rw_state {
	RW_PREPARE_LIST, /* the recovery thread is preparing object list */
	RW_RECOVER_OBJ, /* the thread is recovering objects */
	RW_NOTIFY_COMPLETION, /* the thread is notifying recovery completion */
};

struct recovery_state {
	uint8_t in_recovery;
	enum rw_state state;
	uint64_t nr_finished;
	uint64_t nr_total;
};

#define CACHE_MAX	1024
struct cache_info {
	uint32_t vid;
	uint32_t dirty;
	uint32_t total;
};

struct object_cache_info {
	uint64_t size;
	uint64_t used;
	struct cache_info caches[CACHE_MAX];
	int count;
	uint8_t directio;
};

struct sd_stat {
	struct s_request {
		uint64_t gway_active_nr; /* nr of running request */
		uint64_t peer_active_nr;
		uint64_t gway_total_nr; /* Total nr of requests received */
		uint64_t peer_total_nr;
		uint64_t gway_total_rx; /* Data in */
		uint64_t gway_total_tx; /* Data out */
		uint64_t peer_total_rx;
		uint64_t peer_total_tx;
		uint64_t gway_total_remove_nr;
		uint64_t gway_total_read_nr;
		uint64_t gway_total_write_nr;
		uint64_t gway_total_flush_nr;
		uint64_t peer_total_remove_nr;
		uint64_t peer_total_read_nr;
		uint64_t peer_total_write_nr;
	} r;
};

void sd_inode_stat(const struct sd_inode *inode, uint64_t *, uint64_t *);

#ifdef HAVE_TRACE

#define TRACE_GRAPH_ENTRY  0x01
#define TRACE_GRAPH_RETURN 0x02

#define TRACE_FNAME_LEN    36
#define TRACE_THREAD_LEN   MAX_THREAD_NAME_LEN

struct trace_graph_item {
	char tname[TRACE_THREAD_LEN];
	int type;
	char fname[TRACE_FNAME_LEN];
	int depth;
	uint64_t entry_time;
	uint64_t return_time;
};

#else

/*
 * Some functions e.g. trace_buffer_push() can declare a pointer of struct
 * trace_graph_item in its parameters, so we need the below empty
 * declaration.
 */
struct trace_graph_item;

#endif	/* HAVE_TRACE */

/* VDI locking state, used by both of sheep and dog */
enum lock_state {
	LOCK_STATE_UNLOCKED = 1,
	LOCK_STATE_LOCKED,
	LOCK_STATE_SHARED,
};

enum shared_lock_state {
	/* for iSCSI multipath, per node shared state */
	SHARED_LOCK_STATE_MODIFIED = 1,
	SHARED_LOCK_STATE_SHARED,
	SHARED_LOCK_STATE_INVALIDATED,
};

struct vdi_state {
	uint32_t vid;
	uint8_t nr_copies;
	uint8_t snapshot;
	uint8_t deleted;
	uint8_t copy_policy;
	uint8_t block_size_shift;
	uint8_t __pad[3];
	uint32_t parent_vid;

	uint32_t lock_state;

	/* for normal locking */
	struct node_id lock_owner;

	/* for iSCSI multipath */
	uint32_t nr_participants;
	/*
	 * XXX: participants should be able to have arbital length
	 *
	 * But if a number of tgtd can be equal to SD_MAX_COPIES, sheepdog can
	 * tolerate enough hardware faults.
	 */
	uint32_t participants_state[SD_MAX_COPIES];
	struct node_id participants[SD_MAX_COPIES];
};

#endif /* __INTERNAL_PROTO_H__ */
