#ifndef _FI_SHM_UTILS_INTERNAL_
#define _FI_SHM_UTILS_INTERNAL_

#if HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <numa.h>
#include <numaif.h>
#include "mpool.h"
#define FI_SHM_MEMCPY_DEFAULT  (0)
#define FI_SHM_MEMCPY_COMPILER (1)
#define FI_SHM_MEMCPY_MOVB     (2)

#define FI_SHM_MEMCPY FI_SHM_MEMCPY_COMPILER

#define FI_SHM_SELF_ADDR 0x1ULL

enum {
	FI_SHM_OK,
	FI_SHM_NOK,
	FI_SHM_ERROR_LAST
};

#define FI_SHM_ERROR_STRING_MAX (256)

#define fi_shm_compiler_barrier() __asm__ __volatile__ ( "" ::: "memory" )

#define fi_shm_x86_mfence() __asm__ __volatile__ ( "mfence" )
#define fi_shm_x86_sfence() __asm__ __volatile__ ( "sfence" )
#define fi_shm_x86_lfence() __asm__ __volatile__ ( "lfence" )
#define fi_shm_x86_pause()  __asm__ __volatile__ ( "pause"  )

static inline void
fi_shm_x86_atomic_int_inc(int * var)
{
	__asm__ __volatile__ ("lock ; incl %0" :"=m" (*var) :"m" (*var));
	return;
}

static inline int
fi_shm_x86_atomic_fetch_and_add_int(volatile int * var, int val)
{
	__asm__ __volatile__ ("lock ; xadd %0,%1"
			      : "=r" (val), "=m" (*var)
			      :  "0" (val),  "m" (*var));
	return val;
}


static inline int
fi_shm_x86_atomic_int_cas(int * var, int old_value, int new_value)
{
	int prev_value;

	__asm__ __volatile__ ("lock ; cmpxchg %3,%4" : "=a" (prev_value), "=m" (*var) : "0" (old_value), "q" (new_value), "m" (*var));

	return prev_value;
}  

enum fi_shm_context_type {
	FI_SHM_NOCOMP_SEND_CONTEXT = 1,
	FI_SHM_NOCOMP_RECV_CONTEXT,
	FI_SHM_SEND_CONTEXT,
	FI_SHM_RECV_CONTEXT,
	FI_SHM_INJECT_CONTEXT
};

#define FI_SHM_CTXT_REQ(fi_context)	((fi_context)->internal[0])
#define FI_SHM_CTXT_TYPE(fi_context)	(*(int *)&(fi_context)->internal[1])
#define FI_SHM_CTXT_USER(fi_context)	((fi_context)->internal[2])
#define FI_SHM_CTXT_EP(fi_context)	((fi_context)->internal[3])

/* SHM provider node level memory region ID */

typedef int fi_shm_blockid_t;

/* SHM provider connection control block */

typedef struct {
	volatile int     isReady;
	fi_shm_blockid_t sendBlockId;
	fi_shm_blockid_t connBlockId; /* Sender ID */
	pid_t            pid;
	int              numa_node;
} __attribute__ ((aligned (64))) fi_shm_conn_ctrl_entry_t;

#define FI_SHM_CONN_CTRL_POOL_MAX_SIZE (512)

typedef struct {
	volatile int size;
	__attribute__ ((aligned (64)))
	fi_shm_conn_ctrl_entry_t pool[FI_SHM_CONN_CTRL_POOL_MAX_SIZE];
} fi_shm_conn_ctrl_pool_t;

/* SHM provider transport buffers */

typedef struct {
	volatile int  is_busy;
	uint64_t      tag;  /* matching */
	size_t        size;
} fi_shm_transport_buffer_header_t;

#define FI_SHM_FIRST_FRAGMENT_MASK     (1ULL << (8 * sizeof(size_t) - 1))
#define FI_SHM_IS_FIRST_FRAGMENT(size) (size & (FI_SHM_FIRST_FRAGMENT_MASK))

#define FI_SHM_SET_TOTAL_SIZE(size) (size |   (FI_SHM_FIRST_FRAGMENT_MASK))
#define FI_SHM_GET_TOTAL_SIZE(size) (size & (~(FI_SHM_FIRST_FRAGMENT_MASK)))

typedef struct {
	volatile int is_ready;
} __attribute__ ((aligned (64))) fi_shm_transport_buffer_footer_t;

#define FI_SHM_TRANSPORT_BUFFER_SIZE (16384 - sizeof(fi_shm_transport_buffer_header_t))

typedef struct {
	fi_shm_transport_buffer_header_t header;
	unsigned char                  payload[FI_SHM_TRANSPORT_BUFFER_SIZE];
} fi_shm_transport_buffer_entry_t;

#define FI_SHM_TRANSPORT_BUFFER_FIFO_SIZE (16)

typedef struct {
	fi_shm_transport_buffer_entry_t buffers[FI_SHM_TRANSPORT_BUFFER_FIFO_SIZE];
} fi_shm_transport_buffer_fifo_t;

static inline void
fi_shm_transport_buffer_memcpy(void * dst, const void * src, size_t len)
{
#if FI_SHM_MEMCPY == FI_SHM_MEMCPY_MOVB
	size_t nl = len >> 2;
	__asm__ __volatile__ ("\
	cld;\
	rep; movsl;\
	mov %3,%0;\
	rep; movsb"\
	: "+c" (nl), "+S" (src), "+D" (dst)	\
	: "r" (len & 3));
#elif FI_SHM_MEMCPY == FI_SHM_MEMCPY_COMPILER
	memcpy(dst, src, len);
#else
	memcpy(dst, src, len);
#endif
}
/* SHM provider request queue management structures */

typedef enum {
	FI_SHM_REQUEST_NONE,
	FI_SHM_REQUEST_TAGGED_SEND,
	FI_SHM_REQUEST_TAGGED_RECV
} fi_shm_request_type_t;

#define FI_SHM_REQUEST_FREE_LIST_INIT_SIZE (32*8)

typedef struct fi_shm_request {
	fi_shm_request_type_t type;

	void     * buf;
	size_t     len;
	uint64_t   tag;
	uint64_t   ignore;
	void     * context;

	struct fi_shm_connection * connection;

	size_t     left_len;

	void     * ue_buf;

	TAILQ_ENTRY(fi_shm_request) requests;

	struct fi_shm_request * next; /* free_list infrastructure */
} __attribute__ ((aligned (64))) fi_shm_request_t;

typedef struct fi_shm_request_free_list {
	fi_shm_request_t * head;
	fi_shm_request_t * tail;
} fi_shm_request_free_list_t;

typedef TAILQ_HEAD(fi_shm_req_queue, fi_shm_request) fi_shm_req_queue_t;
typedef struct fi_shm_connection {
	fi_shm_transport_buffer_fifo_t * send_ptr;
	fi_shm_transport_buffer_fifo_t * recv_ptr;

	int last_send_index; /* fifo entry index */
	int last_recv_index; /* fifo entry index */

	fi_shm_blockid_t connBlockId; /* cache */
	fi_shm_blockid_t sendBlockId; /* cache */
	fi_shm_blockid_t recvBlockId; /* cache */

	pid_t pid;
	int numa_node;

	fi_shm_conn_ctrl_pool_t * conn_ptr;

	fi_shm_req_queue_t postponed_send_request_queue;

	fi_shm_request_t * last_unexpected_request;

	SLIST_ENTRY(fi_shm_connection) total_connections;
} __attribute__ ((aligned (64))) fi_shm_connection_t;

typedef fi_shm_connection_t fi_shm_epaddr_t;

typedef struct fi_shm_cq_event {

	union {
		struct fi_cq_entry        context;
		struct fi_cq_tagged_entry tagged;
		struct fi_cq_err_entry	  err;
	} cqe;

	int      error;
	uint64_t source;

	fi_addr_t src_addr;

	struct fi_shm_cq_event * next;

} fi_shm_cq_event_t;

struct fi_shm_cq_event_queue {
	struct fi_shm_cq_event *head;
	struct fi_shm_cq_event *tail;
	size_t	count;
};

struct fi_shm_cq {
	struct shm_ep * ep;
	int format;
	int entry_size;

	struct fi_shm_cq_event_queue event_queue;
	struct mpool* events_free_list;

	struct fi_shm_cq_event *pending_error;
};

struct shm_ep {
	/* SHM connection management */
	struct mpool *connections_pool;
	SLIST_HEAD(, fi_shm_connection) total_connection_list;
	fi_shm_blockid_t          myConnBlockId;
	int                     my_current_conn_ctrl_pool_size;
	fi_shm_conn_ctrl_pool_t * my_conn_ptr;
	fi_shm_blockid_t          myNumaNodeId;

	/* Global posted and unexpected queues */
	fi_shm_req_queue_t posted_recv_request_queue;
	fi_shm_req_queue_t unexpected_recv_request_queue;

	/* Request free list */
	struct mpool *request_free_list;
	int			send_cq_event_flag:1;
	int			recv_cq_event_flag:1;

	struct fi_shm_cq *send_cq;
	struct fi_shm_cq *recv_cq;
};
struct fi_shm_env {
	int debug;
	int warning;
	int busy_wait_count;
	int numa_aware;
};
extern struct fi_shm_env fi_shm_env;


static inline unsigned long long
fi_shm_rdtscll()
{
	unsigned int a;
	unsigned int d;

	asm volatile("rdtsc" : "=a" (a), "=d" (d));

	return (unsigned long long)((unsigned long long)a) | (((unsigned long long)d) << 32);
}

static inline
void fi_shm_trace(char *fmt, ...)
{
	va_list ap;

	char error_str[FI_SHM_ERROR_STRING_MAX];
	int  error_str_offset = 0;

	error_str_offset = snprintf(error_str, FI_SHM_ERROR_STRING_MAX - 1, "<%llu> [%d]: ", fi_shm_rdtscll(), getpid());
	error_str[FI_SHM_ERROR_STRING_MAX - 1] = '\0';

	va_start(ap, fmt);
	vsnprintf(error_str + error_str_offset, FI_SHM_ERROR_STRING_MAX - 1, fmt, ap);
	error_str[FI_SHM_ERROR_STRING_MAX - 1] = '\0';
	va_end(ap);

	fprintf(stderr, error_str);
}

#if 0
#define FI_SHM_DEBUG_MODE 1
#endif

#ifdef FI_SHM_DEBUG_MODE
#define FI_SHM_DEBUG(arg)                       \
	{                                       \
		if (fi_shm_env.debug)           \
		{                               \
			fi_shm_trace arg;       \
		}                               \
	}
#else /* FI_SHM_DEBUG_MODE */
#define FI_SHM_DEBUG(arg)
#endif /* FI_SHM_DEBUG_MODE */

#define FI_SHM_WARNING(arg)                     \
	{                                       \
		if (fi_shm_env.warning)         \
		{                               \
			fi_shm_trace arg;       \
		}                               \
	}

int
fi_shm_ep_handle_incoming_connections(struct shm_ep * ep);
int
fi_shm_cq_poll_all(struct fi_shm_cq *cq, struct fi_cq_tagged_entry *cqe,
		   int count, fi_addr_t *src_addr);
void fi_shm_debug_dump_connection(fi_shm_connection_t * connection);
void fi_shm_debug_dump_ep(struct shm_ep * ep);
void fi_shm_cq_enqueue_event(struct fi_shm_cq *cq, struct fi_shm_cq_event *event);
#endif /*_FI_SHM_UTILS_INTERNAL_*/
