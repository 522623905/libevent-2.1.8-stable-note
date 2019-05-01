/*
 * Copyright 2001-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright 2007-2012 Niels Provos and Nick Mathewson
 *
 * This header file contains definitions for dealing with HTTP requests
 * that are internal to libevent.  As user of the library, you should not
 * need to know about these.
 */

#ifndef HTTP_INTERNAL_H_INCLUDED_
#define HTTP_INTERNAL_H_INCLUDED_

#include "event2/event_struct.h"
#include "util-internal.h"
#include "defer-internal.h"

#define HTTP_CONNECT_TIMEOUT	45
#define HTTP_WRITE_TIMEOUT	50
#define HTTP_READ_TIMEOUT	50

#define HTTP_PREFIX		"http://"
#define HTTP_DEFAULTPORT	80

enum message_read_status {
	ALL_DATA_READ = 1,
	MORE_DATA_EXPECTED = 0,
	DATA_CORRUPTED = -1,
	REQUEST_CANCELED = -2,
	DATA_TOO_LONG = -3
};

struct evbuffer;
struct addrinfo;
struct evhttp_request;

/* Indicates an unknown request method. */
#define EVHTTP_REQ_UNKNOWN_ (1<<15)

// http连接的状态
enum evhttp_connection_state {
	EVCON_DISCONNECTED,	/**< not currently connected not trying either*/
	EVCON_CONNECTING,	/**< tries to currently connect */
	EVCON_IDLE,		/**< connection is established */
	EVCON_READING_FIRSTLINE,/**< reading Request-Line (incoming conn) or
				 **< Status-Line (outgoing conn) */
	EVCON_READING_HEADERS,	/**< reading request/response headers */
	EVCON_READING_BODY,	/**< reading request/response body */
	EVCON_READING_TRAILER,	/**< reading request/response chunked trailer */
	EVCON_WRITING		/**< writing request/response headers/body */
};

struct event_base;

/* A client or server connection. */
// http连接
struct evhttp_connection {
	/* we use this tailq only if this connection was created for an http
	 * server */
	TAILQ_ENTRY(evhttp_connection) next;

	evutil_socket_t fd;	// socket fd
	struct bufferevent *bufev; // 数据缓冲,在读取或者写入了足够量的数据之后调用用户提供的回调

	struct event retry_ev;		/* for retrying connects 用于重连的event */

	// 要绑定的IP+port
	char *bind_address;		/* address to use for binding the src */
	ev_uint16_t bind_port;		/* local port for binding the src */

	// 要连接的IP+port
	char *address;			/* address to connect to */
	ev_uint16_t port;

	size_t max_headers_size;
	ev_uint64_t max_body_size;

	int flags;
#define EVHTTP_CON_INCOMING	0x0001       /* only one request on it ever */
#define EVHTTP_CON_OUTGOING	0x0002       /* multiple requests possible */
#define EVHTTP_CON_CLOSEDETECT	0x0004   /* detecting if persistent close */
/* set when we want to auto free the connection */
#define EVHTTP_CON_AUTOFREE	EVHTTP_CON_PUBLIC_FLAGS_END
/* Installed when attempt to read HTTP error after write failed, see
 * EVHTTP_CON_READ_ON_WRITE_ERROR */
#define EVHTTP_CON_READING_ERROR	(EVHTTP_CON_AUTOFREE << 1)

	struct timeval timeout;		/* timeout for events */
	int retry_cnt;			/* retry count 重试次数 */ 
	int retry_max;			/* maximum number of retries 重试最大次数*/
	struct timeval initial_retry_timeout; /* Timeout for low long to wait
					       * after first failing attempt
					       * before retry 初始的重试时间*/

	enum evhttp_connection_state state; // 连接状态

	/* for server connections, the http server they are connected with */
	// 对于服务器连接，它们与之连接的http服务器
	struct evhttp *http_server;

	// 一个链表，存放该连接上的所有请求，每个请求对应evhttp_request
	TAILQ_HEAD(evcon_requestq, evhttp_request) requests;

	// 回调函数
	void (*cb)(struct evhttp_connection *, void *);
	void *cb_arg;

	// 关闭回调
	void (*closecb)(struct evhttp_connection *, void *);
	void *closecb_arg;

	// 读延迟回调
	struct event_callback read_more_deferred_cb;

	// 所属的base
	struct event_base *base;
	struct evdns_base *dns_base;
	int ai_family;
};

/* A callback for an http server */
// http server回调函数
struct evhttp_cb {
	TAILQ_ENTRY(evhttp_cb) next;

	// 存放字符串uri，注意这里需要free掉
	char *what; 

	// uri对应的回调函数
	void (*cb)(struct evhttp_request *req, void *);
	void *cbarg;
};

/* both the http server as well as the rpc system need to queue connections */
TAILQ_HEAD(evconq, evhttp_connection);

/* each bound socket is stored in one of these */
// 存储每个绑定服务端的套接字
struct evhttp_bound_socket {
	TAILQ_ENTRY(evhttp_bound_socket) next;

	struct evconnlistener *listener; // 所属的监听器
};

/* server alias list item. */
struct evhttp_server_alias {
	TAILQ_ENTRY(evhttp_server_alias) next;

	char *alias; /* the server alias. */
};

// 可看做http server，绑定到某个特定端口和地址，保存访问该server的连接
struct evhttp {
	/* Next vhost, if this is a vhost. */
	TAILQ_ENTRY(evhttp) next_vhost;

	/* All listeners for this host */
	// 存储每个绑定的监听套接字
	TAILQ_HEAD(boundq, evhttp_bound_socket) sockets;

	// 存放用户定义的回调函数链表，如访问uri时要执行的回调函数
	TAILQ_HEAD(httpcbq, evhttp_cb) callbacks;

	/* All live connections on this host. */
	// 存放所有连接evhttp_connection的链表
	struct evconq connections;

	TAILQ_HEAD(vhostsq, evhttp) virtualhosts;

	TAILQ_HEAD(aliasq, evhttp_server_alias) aliases;

	/* NULL if this server is not a vhost */
	char *vhost_pattern;

	struct timeval timeout;

	size_t default_max_headers_size;  // 默认header最大值
	ev_uint64_t default_max_body_size; // 默认body最大值
	int flags;
	const char *default_content_type; // 默认content类型

	/* Bitmask of all HTTP methods that we accept and pass to user
	 * callbacks. */
	ev_uint16_t allowed_methods; // http支持的方法

	/* Fallback callback if all the other callbacks for this connection
	   don't match. */
	// 通用路由的回调函数(即不匹配其他的uri时，调用的回调函数)
	void (*gencb)(struct evhttp_request *req, void *);
	void *gencbarg;

	// http server的buffer event回调函数
	struct bufferevent* (*bevcb)(struct event_base *, void *);
	void *bevcbarg;

	// 由该event_base负责管理evhttp
	struct event_base *base;
};

/* XXX most of these functions could be static. */

/* resets the connection; can be reused for more requests */
void evhttp_connection_reset_(struct evhttp_connection *);

/* connects if necessary */
int evhttp_connection_connect_(struct evhttp_connection *);

enum evhttp_request_error;
/* notifies the current request that it failed; resets connection */
void evhttp_connection_fail_(struct evhttp_connection *,
    enum evhttp_request_error error);

enum message_read_status;

enum message_read_status evhttp_parse_firstline_(struct evhttp_request *, struct evbuffer*);
enum message_read_status evhttp_parse_headers_(struct evhttp_request *, struct evbuffer*);

void evhttp_start_read_(struct evhttp_connection *);
void evhttp_start_write_(struct evhttp_connection *);

/* response sending HTML the data in the buffer */
void evhttp_response_code_(struct evhttp_request *, int, const char *);
void evhttp_send_page_(struct evhttp_request *, struct evbuffer *);

int evhttp_decode_uri_internal(const char *uri, size_t length,
    char *ret, int decode_plus);

#endif /* _HTTP_H */
