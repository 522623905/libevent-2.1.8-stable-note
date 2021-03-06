/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef EVBUFFER_INTERNAL_H_INCLUDED_
#define EVBUFFER_INTERNAL_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

#include "event2/event-config.h"
#include "evconfig-private.h"
#include "event2/util.h"
#include "event2/event_struct.h"
#include "util-internal.h"
#include "defer-internal.h"

/* Experimental cb flag: "never deferred."  Implementation note:
 * these callbacks may get an inaccurate view of n_del/n_added in their
 * arguments. */
#define EVBUFFER_CB_NODEFER 2

#ifdef _WIN32
#include <winsock2.h>
#endif
#include <sys/queue.h>

/* Minimum allocation for a chain.  We define this so that we're burning no
 * more than 5% of each allocation on overhead.  It would be nice to lose even
 * less space, though. */
#if EVENT__SIZEOF_VOID_P < 8
#define MIN_BUFFER_SIZE	512
#else
#define MIN_BUFFER_SIZE	1024
#endif

/** A single evbuffer callback for an evbuffer. This function will be invoked
 * when bytes are added to or removed from the evbuffer. */
// 内部结构体，结构体成员对用户透明
// evbuffer的单个evbuffer回调。
// 当在evbuffer中添加或删除字节时，将调用此回调函数
struct evbuffer_cb_entry {
	/** Structures to implement a doubly-linked queue of callbacks */
	LIST_ENTRY(evbuffer_cb_entry) next;
	/** The callback function to invoke when this callback is called.
	    If EVBUFFER_CB_OBSOLETE is set in flags, the cb_obsolete field is
	    valid; otherwise, cb_func is valid. */
    // 哪个回调类型。一般都是evbuffer_cb_func
	union {
		evbuffer_cb_func cb_func;
		evbuffer_cb cb_obsolete;
	} cb;
	/** Argument to pass to cb. */
    // 回调函数的参数
	void *cbarg;
	/** Currently set flags on this callback. */
    // 该回调的标志
	ev_uint32_t flags;
};

struct bufferevent;
struct evbuffer_chain;
// Libevent将网络 IO 的缓冲数据都存放到evbuffer中,
// 它不提供调度 IO 或者当 IO 就绪时触发 IO 的 功能:这是 bufferevent 的工作
// 实现了为向后面添加数据和从前面移除数据而优化的字节队列。
// 通过一个个的evbuffer_chain连成的链表可以存放很多的缓冲数据
struct evbuffer {
	/** The first chain in this buffer's linked list of chains. */
	struct evbuffer_chain *first;
	/** The last chain in this buffer's linked list of chains. */
	struct evbuffer_chain *last;

	/** Pointer to the next pointer pointing at the 'last_with_data' chain.
	 *
	 * To unpack:
	 *
	 * The last_with_data chain is the last chain that has any data in it.
	 * If all chains in the buffer are empty, it is the first chain.
	 * If the buffer has no chains, it is NULL.
	 *
	 * The last_with_datap pointer points at _whatever 'next' pointer_
	 * points at the last_with_datap chain.  If the last_with_data chain
	 * is the first chain, or it is NULL, then the last_with_datap pointer
	 * is &buf->first.
	 */
    // 这是一个二级指针。使用*last_with_datap时，指向的是链表中最后一个有数据的evbuffer_chain。
    // 所以last_with_datap存储的是倒数第二个evbuffer_chain的next成员地址。
    // 一开始buffer->last_with_datap = &buffer->first;此时first为NULL。所以当链表没有节点时
    // *last_with_datap为NULL。当只有一个节点时*last_with_datap就是first。
	struct evbuffer_chain **last_with_datap;

	/** Total amount of bytes stored in all chains.*/
    // 链表中所有chain的总字节数
	size_t total_len;

	/** Number of bytes we have added to the buffer since we last tried to
	 * invoke callbacks. */
    // 上一次添加到buf中的字节数
	size_t n_add_for_cb;
	/** Number of bytes we have removed from the buffer since we last
	 * tried to invoke callbacks. */
    // 上一次移除buf中的字节数
	size_t n_del_for_cb;

#ifndef EVENT__DISABLE_THREAD_SUPPORT
	/** A lock used to mediate access to this buffer. */
	void *lock;
#endif
	/** True iff we should free the lock field when we free this
	 * evbuffer. */
	unsigned own_lock : 1;
	/** True iff we should not allow changes to the front of the buffer
	 * (drains or prepends). */
    // 禁止在头部添加数据
	unsigned freeze_start : 1;
	/** True iff we should not allow changes to the end of the buffer
	 * (appends) */
    // 禁止在尾部添加数据
	unsigned freeze_end : 1;
	/** True iff this evbuffer's callbacks are not invoked immediately
	 * upon a change in the buffer, but instead are deferred to be invoked
	 * from the event_base's loop.	Useful for preventing enormous stack
	 * overflows when we have mutually recursive callbacks, and for
	 * serializing callbacks in a single thread. */
	unsigned deferred_cbs : 1;
#ifdef _WIN32
	/** True iff this buffer is set up for overlapped IO. */
	unsigned is_overlapped : 1;
#endif
	/** Zero or more EVBUFFER_FLAG_* bits */
	ev_uint32_t flags;

	/** Used to implement deferred callbacks. */
    // 用于延迟回调使用的base
	struct event_base *cb_queue;

	/** A reference count on this evbuffer.	 When the reference count
	 * reaches 0, the buffer is destroyed.	Manipulated with
	 * evbuffer_incref and evbuffer_decref_and_unlock and
	 * evbuffer_free. */
    // 引用计数
	int refcnt;

	/** A struct event_callback handle to make all of this buffer's callbacks
	 * invoked from the event loop. */
    // evbuffer的延迟回调函数
    // 可以让 evbuffer 回调不在 evbuffer 被修改时立即运行,而是延迟到某 event_base 的事件循环中执行
	struct event_callback deferred;

	/** A doubly-linked-list of callback functions */
    // evbuffer的回调函数链表,单个 evbuffer 可以设置多个回调
	LIST_HEAD(evbuffer_cb_queue, evbuffer_cb_entry) callbacks;

	/** The parent bufferevent object this evbuffer belongs to.
	 * NULL if the evbuffer stands alone. */
    // 这个 evbuffer 所属的父 bufferevent 对象。如果 evbuffer 独立，则为 NULL
	struct bufferevent *parent;
};

#if EVENT__SIZEOF_OFF_T < EVENT__SIZEOF_SIZE_T
typedef ev_ssize_t ev_misalign_t;
#define EVBUFFER_CHAIN_MAX ((size_t)EV_SSIZE_MAX)
#else
typedef ev_off_t ev_misalign_t;
#if EVENT__SIZEOF_OFF_T > EVENT__SIZEOF_SIZE_T
#define EVBUFFER_CHAIN_MAX EV_SIZE_MAX
#else
#define EVBUFFER_CHAIN_MAX ((size_t)EV_SSIZE_MAX)
#endif
#endif

/** A single item in an evbuffer. */
// evbuffer链表的一项buffer
struct evbuffer_chain {
	/** points to next buffer in the chain */
	struct evbuffer_chain *next;

	/** total allocation available in the buffer field. */
    // buffer可存储的大小
	size_t buffer_len;

	/** unused space at the beginning of buffer or an offset into a
	 * file for sendfile buffers. */
    // 错开不使用的空间。该成员的值一般等于0
    // buffer中真正的数据是从buffer + misalign开始
	ev_misalign_t misalign;

	/** Offset into buffer + misalign at which to start writing.
	 * In other words, the total number of bytes actually stored
	 * in buffer. */
    // evbuffer_chain已存数据的字节数
    // 所以要从buffer + misalign + off的位置开始写入数据
	size_t off;

	/** Set if special handling is required for this chain */
	unsigned flags;
#define EVBUFFER_FILESEGMENT	0x0001  /**< A chain used for a file segment */
#define EVBUFFER_SENDFILE	0x0002	/**< a chain used with sendfile */
#define EVBUFFER_REFERENCE	0x0004	/**< a chain with a mem reference */
#define EVBUFFER_IMMUTABLE	0x0008	/**< read-only chain */
	/** a chain that mustn't be reallocated or freed, or have its contents
	 * memmoved, until the chain is un-pinned. */
#define EVBUFFER_MEM_PINNED_R	0x0010
#define EVBUFFER_MEM_PINNED_W	0x0020
#define EVBUFFER_MEM_PINNED_ANY (EVBUFFER_MEM_PINNED_R|EVBUFFER_MEM_PINNED_W)
	/** a chain that should be freed, but can't be freed until it is
	 * un-pinned. */
#define EVBUFFER_DANGLING	0x0040
	/** a chain that is a referenced copy of another chain */
#define EVBUFFER_MULTICAST	0x0080

	/** number of references to this chain */
	int refcnt;

	/** Usually points to the read-write memory belonging to this
	 * buffer allocated as part of the evbuffer_chain allocation.
	 * For mmap, this can be a read-only buffer and
	 * EVBUFFER_IMMUTABLE will be set in flags.  For sendfile, it
	 * may point to NULL.
	 */
    // buffer真正的数据
	unsigned char *buffer;
};

/** callback for a reference chain; lets us know what to do with it when
 * we're done with it. Lives at the end of an evbuffer_chain with the
 * EVBUFFER_REFERENCE flag set */
struct evbuffer_chain_reference {
	evbuffer_ref_cleanup_cb cleanupfn;
	void *extra;
};

/** File segment for a file-segment chain.  Lives at the end of an
 * evbuffer_chain with the EVBUFFER_FILESEGMENT flag set.  */
struct evbuffer_chain_file_segment {
	struct evbuffer_file_segment *segment;
#ifdef _WIN32
	/** If we're using CreateFileMapping, this is the handle to the view. */
	HANDLE view_handle;
#endif
};

/* Declared in event2/buffer.h; defined here. */
struct evbuffer_file_segment {
	void *lock; /**< lock prevent concurrent access to refcnt */
	int refcnt; /**< Reference count for this file segment */
	unsigned flags; /**< combination of EVBUF_FS_* flags  */

	/** What kind of file segment is this? */
	unsigned can_sendfile : 1; // 表示sendfile是否可以使用
	unsigned is_mapping : 1;	// 表示mmap是否可以使用

	/** The fd that we read the data from. */
	int fd;
	/** If we're using mmap, this is the raw mapped memory. */
	void *mapping;
#ifdef _WIN32
	/** If we're using CreateFileMapping, this is the mapping */
	HANDLE mapping_handle;
#endif
	/** If we're using mmap or IO, this is the content of the file
	 * segment. */
	char *contents;
	/** Position of this segment within the file. */
	ev_off_t file_offset;
	/** If we're using mmap, this is the offset within 'mapping' where
	 * this data segment begins. */
	ev_off_t mmap_offset;
	/** The length of this segment. */
	ev_off_t length;
	/** Cleanup callback function */
	evbuffer_file_segment_cleanup_cb cleanup_cb;
	/** Argument to be pass to cleanup callback function */
	void *cleanup_cb_arg;
};

/** Information about the multicast parent of a chain.  Lives at the
 * end of an evbuffer_chain with the EVBUFFER_MULTICAST flag set.  */
struct evbuffer_multicast_parent {
	/** source buffer the multicast parent belongs to */
	struct evbuffer *source;
	/** multicast parent for this chain */
	struct evbuffer_chain *parent;
};

#define EVBUFFER_CHAIN_SIZE sizeof(struct evbuffer_chain)
/** Return a pointer to extra data allocated along with an evbuffer. */
// 返回chain + sizeof(evbuffer_chain) 的内存地址
#define EVBUFFER_CHAIN_EXTRA(t, c) (t *)((struct evbuffer_chain *)(c) + 1)

/** Assert that we are holding the lock on an evbuffer */
#define ASSERT_EVBUFFER_LOCKED(buffer)			\
	EVLOCK_ASSERT_LOCKED((buffer)->lock)

#define EVBUFFER_LOCK(buffer)						\
	do {								\
		EVLOCK_LOCK((buffer)->lock, 0);				\
	} while (0)
#define EVBUFFER_UNLOCK(buffer)						\
	do {								\
		EVLOCK_UNLOCK((buffer)->lock, 0);			\
	} while (0)
#define EVBUFFER_LOCK2(buffer1, buffer2)				\
	do {								\
		EVLOCK_LOCK2((buffer1)->lock, (buffer2)->lock, 0, 0);	\
	} while (0)
#define EVBUFFER_UNLOCK2(buffer1, buffer2)				\
	do {								\
		EVLOCK_UNLOCK2((buffer1)->lock, (buffer2)->lock, 0, 0);	\
	} while (0)

/** Increase the reference count of buf by one. */
void evbuffer_incref_(struct evbuffer *buf);
/** Increase the reference count of buf by one and acquire the lock. */
void evbuffer_incref_and_lock_(struct evbuffer *buf);
/** Pin a single buffer chain using a given flag. A pinned chunk may not be
 * moved or freed until it is unpinned. */
void evbuffer_chain_pin_(struct evbuffer_chain *chain, unsigned flag);
/** Unpin a single buffer chain using a given flag. */
void evbuffer_chain_unpin_(struct evbuffer_chain *chain, unsigned flag);
/** As evbuffer_free, but requires that we hold a lock on the buffer, and
 * releases the lock before freeing it and the buffer. */
void evbuffer_decref_and_unlock_(struct evbuffer *buffer);

/** As evbuffer_expand, but does not guarantee that the newly allocated memory
 * is contiguous.  Instead, it may be split across two or more chunks. */
int evbuffer_expand_fast_(struct evbuffer *, size_t, int);

/** Helper: prepares for a readv/WSARecv call by expanding the buffer to
 * hold enough memory to read 'howmuch' bytes in possibly noncontiguous memory.
 * Sets up the one or two iovecs in 'vecs' to point to the free memory and its
 * extent, and *chainp to point to the first chain that we'll try to read into.
 * Returns the number of vecs used.
 */
int evbuffer_read_setup_vecs_(struct evbuffer *buf, ev_ssize_t howmuch,
    struct evbuffer_iovec *vecs, int n_vecs, struct evbuffer_chain ***chainp,
    int exact);

/* Helper macro: copies an evbuffer_iovec in ei to a win32 WSABUF in i. */
#define WSABUF_FROM_EVBUFFER_IOV(i,ei) do {		\
		(i)->buf = (ei)->iov_base;		\
		(i)->len = (unsigned long)(ei)->iov_len;	\
	} while (0)
/* XXXX the cast above is safe for now, but not if we allow mmaps on win64.
 * See note in buffer_iocp's launch_write function */

/** Set the parent bufferevent object for buf to bev */
void evbuffer_set_parent_(struct evbuffer *buf, struct bufferevent *bev);

void evbuffer_invoke_callbacks_(struct evbuffer *buf);


int evbuffer_get_callbacks_(struct evbuffer *buffer,
    struct event_callback **cbs,
    int max_cbs);

#ifdef __cplusplus
}
#endif

#endif /* EVBUFFER_INTERNAL_H_INCLUDED_ */
