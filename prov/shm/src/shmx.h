#ifndef _FI_SHM_H
#define _FI_SHM_H

#ifdef __cplusplus
extern "C" {
#endif

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <complex.h>
#include <rdma/fabric.h>
#include <rdma/fi_prov.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_atomic.h>
#include <rdma/fi_trigger.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_errno.h>

#include "fi.h"
#include "fi_enosys.h"
#include "fi_list.h"
#include <rdma/fi_log.h>

#include <sys/types.h>
#include <sys/shm.h>

#include <numa.h>
#include <numaif.h>

#include <sys/queue.h>
#include "shm/fi_shm_utils.h"
#define SHMX_PROV_NAME		"shm"
#define SHMX_PROV_NAME_LEN	3
#define SHMX_DOMAIN_NAME	"shm"
#define SHMX_DOMAIN_NAME_LEN	3
#define SHMX_FABRIC_NAME	"shm"
#define SHMX_FABRIC_NAME_LEN	3



#define SHMX_OP_FLAGS	(FI_INJECT)

#define SHMX_CAP_EXT	(0)

#define SHMX_CAPS	(FI_TAGGED | FI_MSG | FI_MULTI_RECV | FI_INJECT | FI_RMA | FI_ATOMICS | \
                     FI_SEND | FI_RECV | \
                     SHMX_CAP_EXT)

#define SHMX_MODE	(FI_CONTEXT)

#define SHMX_MAX_MSG_SIZE	((0x1ULL << 32) - 1)
#define SHMX_INJECT_SIZE	(64)


/* Forward declaration section */

struct shmx_fid_ep;
struct shmx_fid_domain;
struct shmx_fid_cq;
struct shmx_fid_av;
struct shmx_connection;


struct shmx_fid_fabric {
	struct fid_fabric fabric;

    /* no customization */
};

struct shmx_fid_domain {
	struct fid_domain domain;

    /* shm provider customization */

	struct shmx_fid_ep * ep;

	uint64_t		mode;
};


struct shmx_fid_cq {
	struct fid_cq cq;
    
    /* SHM provider customization */

	struct shmx_fid_domain	  * domain;
	struct shmx_fid_ep        * ep;


	int 		        		format;
	int				            entry_size;
        struct fi_shm_cq shm_cq;
};

struct shmx_fid_av {
	struct fid_av av;

    /* SHM provider customization */

	struct shmx_fid_domain * domain;
	struct shmx_fid_ep     * ep;

	int			           type;
	size_t			       addrlen;

	size_t			       count;
};


struct shmx_fid_ep {
	struct fid_ep ep;

    /* SHM provider customization */

	struct shmx_fid_domain	* domain;
	struct shmx_fid_av	    * av;

	struct shmx_fid_cq	    * send_cq;
	struct shmx_fid_cq	    * recv_cq;


	uint64_t		flags;
	uint64_t		caps;

        struct shm_ep shm_ep;
};


struct shmx_fid_mr {
	struct fid_mr		mr;
	struct shmx_fid_domain	*domain;
	struct shmx_fid_cq	*cq;
	struct shmx_fid_cntr	*cntr;
	uint64_t		access;
	uint64_t		flags;
	uint64_t		offset;
	size_t			iov_count;
	struct iovec		iov[0];	/* must be the last field */
};


extern struct fi_ops_cm		shmx_cm_ops;
extern struct fi_ops_tagged	shmx_tagged_ops;
extern struct shmx_env      shmx_env;



int
shmx_domain_open(struct fid_fabric *fabric, struct fi_info *info,
                 struct fid_domain **domain, void *context);
int
shmx_ep_open(struct fid_domain *domain, struct fi_info *info,
		     struct fid_ep **ep, void *context);

int
shmx_cq_open(struct fid_domain *domain, struct fi_cq_attr *attr,
		     struct fid_cq **cq, void *context);

int
shmx_av_open(struct fid_domain *domain, struct fi_av_attr *attr,
		     struct fid_av **av, void *context);

int
shmx_domain_check_features(struct shmx_fid_domain *domain, int ep_cap);


void
shmx_domain_disable_ep(struct shmx_fid_domain *domain, struct shmx_fid_ep *ep);

int
shmx_errno(int err);


ssize_t
_shmx_tagged_send(struct fid_ep *ep, const void *buf, size_t len,
                  void *desc, fi_addr_t dest_addr, uint64_t tag,
                  void *context, uint64_t flags);

ssize_t
_shmx_tagged_recv(struct fid_ep *ep, void *buf, size_t len,
                  void *desc, fi_addr_t src_addr, uint64_t tag,
                  uint64_t ignore, void *context, uint64_t flags);

extern struct fi_provider shmx_prov;
#define SHMX_DEBUG(...) FI_DBG(&shmx_prov, FI_LOG_CORE, ##__VA_ARGS__)
#ifdef __cplusplus
}
#endif

#endif /* _FI_SHM_H */

