/*
 * Copyright (c) 2013-2014 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _FI_H_
#define _FI_H_

#include "config.h"

#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <sys/param.h>

#include <rdma/fabric.h>
#include <rdma/fi_prov.h>
#include <rdma/fi_atomic.h>
#include <rdma/fi_log.h>

#ifdef __APPLE__
#include <osx/osd.h>
#else
#include <linux/osd.h>
#endif

#ifdef HAVE_ATOMICS
#  include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef INCLUDE_VALGRIND
#   include <valgrind/memcheck.h>
#   ifndef VALGRIND_MAKE_MEM_DEFINED
#      warning "Valgrind requested, but VALGRIND_MAKE_MEM_DEFINED undefined"
#   endif
#endif

#ifndef VALGRIND_MAKE_MEM_DEFINED
#   define VALGRIND_MAKE_MEM_DEFINED(addr, len)
#endif

static inline void *mem_dup(const void *src, size_t size)
{
	void *dest;

	if ((dest = malloc(size)))
		memcpy(dest, src, size);
	return dest;
}

/*
 * OS X doesn't have __BYTE_ORDER, Linux usually has BYTE_ORDER but not under
 * all features.h flags
 */
#if !defined(BYTE_ORDER)
#  if defined(__BYTE_ORDER) && \
      defined(__LITTLE_ENDIAN) && \
      defined(__BIG_ENDIAN)
#    define BYTE_ORDER __BYTE_ORDER
#    define LITTLE_ENDIAN __LITTLE_ENDIAN
#    define BIG_ENDIAN __BIG_ENDIAN
#  else
#    error "cannot determine endianness!"
#  endif
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#ifndef htonll
static inline uint64_t htonll(uint64_t x) { return bswap_64(x); }
#endif
#ifndef ntohll
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
#endif
#else
#ifndef htonll
static inline uint64_t htonll(uint64_t x) { return x; }
#endif
#ifndef ntohll
static inline uint64_t ntohll(uint64_t x) { return x; }
#endif
#endif

#define sizeof_field(type, field) sizeof(((type *)0)->field)

#ifndef MIN
#define MIN(a, b) \
	({ typeof (a) _a = (a); \
		typeof (b) _b = (b); \
		_a < _b ? _a : _b; })
#endif

#ifndef MAX
#define MAX(a, b) \
	({ typeof (a) _a = (a); \
		typeof (b) _b = (b); \
		_a > _b ? _a : _b; })
#endif

/* Restrict to size of struct fi_context */
struct fi_prov_context {
	int disable_logging;
};

struct fi_filter {
	char **names;
	int negated;
};

extern struct fi_filter prov_log_filter;

void fi_create_filter(struct fi_filter *filter, const char *env_name);
void fi_free_filter(struct fi_filter *filter);
int fi_apply_filter(struct fi_filter *filter, const char *name);

void fi_log_init(void);
void fi_log_fini(void);
void fi_param_init(void);
void fi_param_fini(void);
void fi_param_undefine(const struct fi_provider *provider);

/* flsll is defined on BSD systems, but is different. */
static inline int fi_flsll(long long int i)
{
	return i ? 65 - ffsll(htonll(i)) : 0;
}

static inline uint64_t roundup_power_of_two(uint64_t n)
{
	return 1ULL << fi_flsll(n - 1);
}

#define FI_TAG_GENERIC	0xAAAAAAAAAAAAAAAAULL


#if PT_LOCK_SPIN == 1

#define fastlock_t_ pthread_spinlock_t
#define fastlock_init_(lock) pthread_spin_init(lock, PTHREAD_PROCESS_PRIVATE)
#define fastlock_destroy_(lock) pthread_spin_destroy(lock)
#define fastlock_acquire_(lock) pthread_spin_lock(lock)
#define fastlock_tryacquire_(lock) pthread_spin_trylock(lock)
#define fastlock_release_(lock) pthread_spin_unlock(lock)

#else

#define fastlock_t_ pthread_mutex_t
#define fastlock_init_(lock) pthread_mutex_init(lock, NULL)
#define fastlock_destroy_(lock) pthread_mutex_destroy(lock)
#define fastlock_acquire_(lock) pthread_mutex_lock(lock)
#define fastlock_tryacquire_(lock) pthread_mutex_trylock(lock)
#define fastlock_release_(lock) pthread_mutex_unlock(lock)

#endif /* PT_LOCK_SPIN */

#if ENABLE_DEBUG

typedef struct {
	fastlock_t_ impl;
	int is_initialized;
} fastlock_t;

static inline int fastlock_init(fastlock_t *lock)
{
	int ret;

	ret = fastlock_init_(&lock->impl);
	lock->is_initialized = !ret;
	return ret;
}

static inline void fastlock_destroy(fastlock_t *lock)
{
	int ret;

	assert(lock->is_initialized);
	lock->is_initialized = 0;
	ret = fastlock_destroy_(&lock->impl);
	assert(!ret);
}

static inline void fastlock_acquire(fastlock_t *lock)
{
	int ret;

	assert(lock->is_initialized);
	ret = fastlock_acquire_(&lock->impl);
	assert(!ret);
}

static inline int fastlock_tryacquire(fastlock_t *lock)
{
	assert(lock->is_initialized);
	return fastlock_tryacquire_(&lock->impl);
}

static inline void fastlock_release(fastlock_t *lock)
{
	int ret;

	assert(lock->is_initialized);
	ret = fastlock_release_(&lock->impl);
	assert(!ret);
}

#else /* !ENABLE_DEBUG */

#  define fastlock_t fastlock_t_
#  define fastlock_init(lock) fastlock_init_(lock)
#  define fastlock_destroy(lock) fastlock_destroy_(lock)
#  define fastlock_acquire(lock) fastlock_acquire_(lock)
#  define fastlock_tryacquire(lock) fastlock_tryacquire_(lock)
#  define fastlock_release(lock) fastlock_release_(lock)

#endif


#if ENABLE_DEBUG
#define ATOMIC_IS_INITIALIZED(atomic) assert(atomic->is_initialized)
#else
#define ATOMIC_IS_INITIALIZED(atomic)
#endif

#ifdef HAVE_ATOMICS
typedef struct {
    atomic_int val;
#if ENABLE_DEBUG
    int is_initialized;
#endif
} atomic_t;

static inline int atomic_inc(atomic_t *atomic)
{
	ATOMIC_IS_INITIALIZED(atomic);
	return atomic_fetch_add_explicit(&atomic->val, 1, memory_order_acq_rel) + 1;
}

static inline int atomic_dec(atomic_t *atomic)
{
	ATOMIC_IS_INITIALIZED(atomic);
	return atomic_fetch_sub_explicit(&atomic->val, 1, memory_order_acq_rel) - 1;
}

static inline int atomic_set(atomic_t *atomic, int value)
{
	ATOMIC_IS_INITIALIZED(atomic);
	atomic_store(&atomic->val, value);
	return value;
}

static inline int atomic_get(atomic_t *atomic)
{
	ATOMIC_IS_INITIALIZED(atomic);
	return atomic_load(&atomic->val);
}

/* avoid using "atomic_init" so we don't conflict with symbol/macro from stdatomic.h */
static inline void atomic_initialize(atomic_t *atomic, int value)
{
	atomic_init(&atomic->val, value);
#if ENABLE_DEBUG
	atomic->is_initialized = 1;
#endif
}

static inline int atomic_add(atomic_t *atomic, int val)
{
	ATOMIC_IS_INITIALIZED(atomic);
	return atomic_fetch_add_explicit(&atomic->val,
			val, memory_order_acq_rel) + 1;
}

static inline int atomic_sub(atomic_t *atomic, int val)
{
	ATOMIC_IS_INITIALIZED(atomic);
	return atomic_fetch_sub_explicit(&atomic->val,
			val, memory_order_acq_rel) - 1;
}

#else

typedef struct {
	fastlock_t lock;
	int val;
#if ENABLE_DEBUG
	int is_initialized;
#endif
} atomic_t;

static inline int atomic_inc(atomic_t *atomic)
{
	int v;

	ATOMIC_IS_INITIALIZED(atomic);
	fastlock_acquire(&atomic->lock);
	v = ++(atomic->val);
	fastlock_release(&atomic->lock);
	return v;
}

static inline int atomic_dec(atomic_t *atomic)
{
	int v;

	ATOMIC_IS_INITIALIZED(atomic);
	fastlock_acquire(&atomic->lock);
	v = --(atomic->val);
	fastlock_release(&atomic->lock);
	return v;
}

static inline int atomic_set(atomic_t *atomic, int value)
{
	ATOMIC_IS_INITIALIZED(atomic);
	fastlock_acquire(&atomic->lock);
	atomic->val = value;
	fastlock_release(&atomic->lock);
	return value;
}

/* avoid using "atomic_init" so we don't conflict with symbol/macro from stdatomic.h */
static inline void atomic_initialize(atomic_t *atomic, int value)
{
	fastlock_init(&atomic->lock);
	atomic->val = value;
#if ENABLE_DEBUG
	atomic->is_initialized = 1;
#endif
}

static inline int atomic_get(atomic_t *atomic)
{
	ATOMIC_IS_INITIALIZED(atomic);
	return atomic->val;
}

static inline int atomic_add(atomic_t *atomic, int val)
{
	int v;

	ATOMIC_IS_INITIALIZED(atomic);
	fastlock_acquire(&atomic->lock);
	atomic->val += val;
	v = atomic->val;
	fastlock_release(&atomic->lock);
	return v;
}

static inline int atomic_sub(atomic_t *atomic, int val)
{
	int v;

	ATOMIC_IS_INITIALIZED(atomic);
	fastlock_acquire(&atomic->lock);
	atomic->val -= val;
	v = atomic->val;
	fastlock_release(&atomic->lock);
	return v;
}

#endif // HAVE_ATOMICS

/* non exported symbols */
int fi_read_file(const char *dir, const char *file, char *buf, size_t size);
int fi_poll_fd(int fd, int timeout);
int fi_wait_cond(pthread_cond_t *cond, pthread_mutex_t *mut, int timeout);

size_t fi_datatype_size(enum fi_datatype datatype);
uint64_t fi_tag_bits(uint64_t mem_tag_format);
uint64_t fi_tag_format(uint64_t tag_bits);

int fi_send_allowed(uint64_t caps);
int fi_recv_allowed(uint64_t caps);
int fi_rma_initiate_allowed(uint64_t caps);
int fi_rma_target_allowed(uint64_t caps);

uint64_t fi_gettime_ms(void);
int fi_fd_nonblock(int fd);

#define RDMA_CONF_DIR  SYSCONFDIR "/" RDMADIR
#define FI_CONF_DIR RDMA_CONF_DIR "/fabric"

#define DEFAULT_ABI "FABRIC_1.0"

#if  HAVE_ALIAS_ATTRIBUTE == 1
#define DEFAULT_SYMVER_PRE(a) a##_
#else
#define DEFAULT_SYMVER_PRE(a) a
#endif

/* symbol -> external symbol mappings */
#if HAVE_SYMVER_SUPPORT

#  define SYMVER(name, api, ver) \
        asm(".symver " #name "," #api "@" #ver)
#  define DEFAULT_SYMVER(name, api) \
        asm(".symver " #name "," #api "@@" DEFAULT_ABI)
#else
#  define SYMVER(Name, api, ver)
#if  HAVE_ALIAS_ATTRIBUTE == 1
#  define DEFAULT_SYMVER(name, api) \
        extern typeof (name) api __attribute__((alias(#name)));
#else
#  define DEFAULT_SYMVER(name, api)
#endif  /* HAVE_ALIAS_ATTRIBUTE == 1*/

#endif /* HAVE_SYMVER_SUPPORT */

#ifdef __cplusplus
}
#endif

#endif /* _FI_H_ */
