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
#ifndef EVENT2_EVENT_STRUCT_H_INCLUDED_
#define EVENT2_EVENT_STRUCT_H_INCLUDED_

/** @file event2/event_struct.h

  Structures used by event.h.  Using these structures directly WILL harm
  forward compatibility: be careful.

  No field declared in this file should be used directly in user code.  Except
  for historical reasons, these fields would not be exposed at all.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event-config.h>
#ifdef EVENT__HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

/* For int types. */
#include <event2/util.h>

/* For evkeyvalq */
#include <event2/keyvalq_struct.h>

//libevent 用于标记 event信息的字段，表明事件当前的状态
#define EVLIST_TIMEOUT	    0x01    //event在time堆中
#define EVLIST_INSERTED	    0x02    //event在已注册事件链表中
#define EVLIST_SIGNAL	    0x04    //目前未使用
#define EVLIST_ACTIVE	    0x08    //event在激活链表中
#define EVLIST_INTERNAL	    0x10    //内部使用事件标记
#define EVLIST_ACTIVE_LATER 0x20    //事件在下一次激活链表中
#define EVLIST_FINALIZING   0x40    //event已终止标记
#define EVLIST_INIT	    0x80        //event已被初始化

#define EVLIST_ALL          0xff

/* Fix so that people don't have to run with <sys/queue.h> */
#ifndef TAILQ_ENTRY
#define EVENT_DEFINED_TQENTRY_
#define TAILQ_ENTRY(type)						\
struct {								\
	struct type *tqe_next;	/* next element */			\
	struct type **tqe_prev;	/* address of previous next element */	\
}
#endif /* !TAILQ_ENTRY */

#ifndef TAILQ_HEAD
#define EVENT_DEFINED_TQHEAD_
#define TAILQ_HEAD(name, type)			\
struct name {					\
	struct type *tqh_first;			\
	struct type **tqh_last;			\
}
#endif

/* Fix so that people don't have to run with <sys/queue.h> */
#ifndef LIST_ENTRY
#define EVENT_DEFINED_LISTENTRY_
#define LIST_ENTRY(type)						\
struct {								\
	struct type *le_next;	/* next element */			\
	struct type **le_prev;	/* address of previous next element */	\
}
#endif /* !LIST_ENTRY */

#ifndef LIST_HEAD
#define EVENT_DEFINED_LISTHEAD_
#define LIST_HEAD(name, type)						\
struct name {								\
	struct type *lh_first;  /* first element */			\
	}
#endif /* !LIST_HEAD */

struct event;

struct event_callback {
	TAILQ_ENTRY(event_callback) evcb_active_next;       //活动事件队列
	short evcb_flags;           //事件标记
	ev_uint8_t evcb_pri;	/* smaller numbers are higher priority 指定事件处理器优先级，值越小则优先级越高*/
	ev_uint8_t evcb_closure;    //指定event_base执行事件处理器的回调函数时的行为
	/* allows us to adopt for different types of events */
        union {
		void (*evcb_callback)(evutil_socket_t, short, void *);      //事件处理器的回调函数
		void (*evcb_selfcb)(struct event_callback *, void *);
		void (*evcb_evfinalize)(struct event *, void *);
		void (*evcb_cbfinalize)(struct event_callback *, void *);
	} evcb_cb_union;
	void *evcb_arg;             //函调函数参数
};

struct event_base;
/*
 * event事件处理器，封装句柄、事件类型、回调函数，以及其他必要的标志和数据。
 * */
struct event {
	struct event_callback ev_evcallback;

	/* for managing timeouts */
	union {
		TAILQ_ENTRY(event) ev_next_with_common_timeout; //指出该定时器在通用定时器队列中的位置
		int min_heap_idx;   //指出该定时器在时间堆中的位置
	} ev_timeout_pos;       //仅用于定时事件处理器
	evutil_socket_t ev_fd;      //对于I/O事件处理器，是文件描述符值 对于信号事件处理器，是信号值

	struct event_base *ev_base; //该事件处理器从属的event_base实例

	union {
		/* used for io events */
		struct {
			LIST_ENTRY (event) ev_io_next;  //I/O事件队列
			struct timeval ev_timeout;
		} ev_io;

		/* used by signal events */
		struct {
			LIST_ENTRY (event) ev_signal_next;      //信号事件队列
            //指定当信号事件发生时，Reactor需要执行多少次该事件对应的事件处理器中的回调函数
			short ev_ncalls;
			/* Allows deletes in callback */
			short *ev_pncalls;      //NULL或指向ev.ev_signal.ev_ncalls
		} ev_signal;
	} ev_;

	short ev_events;        //监测事件标记
	short ev_res;		/* result passed to event callback 记录当前激活事件的类型 */
	struct timeval ev_timeout;  //仅对定时器有效，指定定时器的超时值
};

TAILQ_HEAD (event_list, event);

#ifdef EVENT_DEFINED_TQENTRY_
#undef TAILQ_ENTRY
#endif

#ifdef EVENT_DEFINED_TQHEAD_
#undef TAILQ_HEAD
#endif

LIST_HEAD (event_dlist, event); 

#ifdef EVENT_DEFINED_LISTENTRY_
#undef LIST_ENTRY
#endif

#ifdef EVENT_DEFINED_LISTHEAD_
#undef LIST_HEAD
#endif

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_EVENT_STRUCT_H_INCLUDED_ */
