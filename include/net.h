#ifndef __NET_H__
#define __NET_H__

#include <sys/socket.h>
#include <arpa/inet.h>

#include "list.h"
#include "sheepdog_proto.h"

/*
 * We can't always retry because if only IO NIC is down, we'll retry for ever.
 *
 * We observed that for a busy node, the response could be as long as 15s, so
 * wait 30s would be a safe value. Even we are false timeouted, the gateway will
 * retry the request and sockfd cache module will repair the false-closes.
 */
#define MAX_POLLTIME 30 /* seconds */
#define POLL_TIMEOUT 5 /* seconds */
#define MAX_RETRY_COUNT (MAX_POLLTIME / POLL_TIMEOUT)

enum conn_state {
	C_IO_HEADER = 0,
	C_IO_DATA_INIT,
	C_IO_DATA,
	C_IO_END,
	C_IO_CLOSED,
};

struct connection {
	int fd;
	unsigned int events;

	uint16_t port;
	char ipstr[INET6_ADDRSTRLEN];

	bool dead;

#ifdef HAVE_ACCELIO
	/* session: the session this connection belongs to */
	struct xio_session *session;

	struct xio_connection *conn;
	struct list_node list;
#endif
};

#ifdef HAVE_ACCELIO

struct sd_xio_session {
	int efd;
};

#endif

int conn_tx_off(struct connection *conn);
int conn_tx_on(struct connection *conn);
int conn_rx_off(struct connection *conn);
int conn_rx_on(struct connection *conn);
int rx(struct connection *conn, enum conn_state next_state);
int tx(struct connection *conn, enum conn_state next_state);
int connect_to(const char *name, int port);
int send_req(int sockfd, struct sd_req *hdr, void *data, unsigned int wlen,
	     bool (*need_retry)(uint32_t), uint32_t, uint32_t);
int exec_req(int sockfd, struct sd_req *hdr, void *,
	     bool (*need_retry)(uint32_t), uint32_t, uint32_t);
int do_read(int sockfd, void *buf, uint32_t len,
	    bool (*need_retry)(uint32_t), uint32_t, uint32_t);
int create_listen_ports(const char *bindaddr, int port,
			int (*callback)(int fd, void *), void *data);
int create_unix_domain_socket(const char *unix_path,
			      int (*callback)(int, void *), void *data);

const char *addr_to_str(const uint8_t *addr, uint16_t port);
bool str_to_addr(const char *ipstr, uint8_t *addr);
char *sockaddr_in_to_str(struct sockaddr_in *sockaddr);
int set_nodelay(int fd);
int set_keepalive(int fd);
int set_snd_timeout(int fd);
int set_rcv_timeout(int fd);
int get_local_addr(uint8_t *bytes);
bool inetaddr_is_valid(char *addr);
int do_writev2(int fd, void *hdr, size_t hdr_len, void *body, size_t body_len);

/* for typical usage of do_writev2() */
#define writev2(fd, hdr, body, body_len)	\
	do_writev2(fd, hdr, sizeof(*hdr), body, body_len)

static inline int connect_to_addr(const uint8_t *addr, int port)
{
	return connect_to(addr_to_str(addr, 0), port);
}

#endif
