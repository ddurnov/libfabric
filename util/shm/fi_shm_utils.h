#ifndef _FI_SHM_UTILS_H
#define _FI_SHM_UTILS_H

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
#include <rdma/fi_cm.h>
#include "fi_shm_utils_internal.h"


int fi_shm_init_ep(struct shm_ep *ep);
int fi_shm_fini_ep(struct shm_ep *ep);
int fi_shm_getname(struct shm_ep *ep, void *addr, size_t *addrlen);
int fi_shm_connect(struct shm_ep *ep, void *addr, fi_addr_t *fi_addr);
size_t fi_shm_addrlen();

int fi_shm_cq_init(struct fi_shm_cq *cq, int cq_format, int cq_size);
int fi_shm_cq_fini(struct fi_shm_cq *cq);
int fi_shm_ep_bind_cq(struct shm_ep *ep, struct fi_shm_cq *cq, uint64_t flags);

static inline
int fi_shm_tagged_recv(struct shm_ep *ep, void *buf, size_t len,
		       fi_addr_t fi_addr, uint64_t tag,
		       uint64_t ignore, void *context);

static inline
int fi_shm_tagged_send(struct shm_ep *ep, const void * buf,
		       size_t len, fi_addr_t fi_addr,
		       uint64_t tag, void *context);
static inline
int fi_shm_cancel_recv(struct shm_ep *ep, void *context);
static inline
int fi_shm_cancel_send(struct shm_ep *ep, void *context);

ssize_t fi_shm_cq_readfrom(struct fi_shm_cq *cq, void *buf, size_t count, fi_addr_t *src_addr);
ssize_t fi_shm_cq_readerr(struct fi_shm_cq *cq, struct fi_cq_err_entry *buf, uint64_t flags);

#include "fi_shm_tagged.h"

#endif /*_FI_SHM_UTILS_H*/
