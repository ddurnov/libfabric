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

#include "shmx.h"


static ssize_t
shmx_cq_readfrom(struct fid_cq *cq, void *buf, size_t count,
                 fi_addr_t *src_addr)
{
	struct shmx_fid_cq *cq_priv;
        cq_priv = container_of(cq, struct shmx_fid_cq, cq);
        return fi_shm_cq_readfrom(&cq_priv->shm_cq, buf, count, src_addr);
}

static ssize_t shmx_cq_read(struct fid_cq *cq, void *buf, size_t count)
{
	return shmx_cq_readfrom(cq, buf, count, NULL);
}

static ssize_t shmx_cq_readerr(struct fid_cq *cq, struct fi_cq_err_entry *buf,
			       uint64_t flags)
{
	struct shmx_fid_cq *cq_priv;

	cq_priv = container_of(cq, struct shmx_fid_cq, cq);
        return fi_shm_cq_readerr(&cq_priv->shm_cq, buf, flags);
}

#if 0
static ssize_t shmx_cq_write(struct fid_cq *cq, const void *buf, size_t len)
{
	struct shmx_fid_cq *cq_priv;
	struct shmx_cq_event *event;
	ssize_t written_len = 0;

	cq_priv = container_of(cq, struct shmx_fid_cq, cq);

	while (len >= cq_priv->entry_size) {
		event = calloc(1, sizeof(*event));
		if (!event) {
			fprintf(stderr, "%s: out of memory\n", __func__);
			return -ENOMEM;
		}

		memcpy((void *)&event->cqe, buf + written_len, cq_priv->entry_size);
		shmx_cq_enqueue_event(cq_priv, event);
		written_len += cq_priv->entry_size;
		len -= cq_priv->entry_size;
	}

	return written_len;
}

static ssize_t shmx_cq_writeerr(struct fid_cq *cq, struct fi_cq_err_entry *buf,
				size_t len, uint64_t flags)
{
	struct shmx_fid_cq *cq_priv;
	struct shmx_cq_event *event;
	ssize_t written_len = 0;

	cq_priv = container_of(cq, struct shmx_fid_cq, cq);

	while (len >= sizeof(*buf)) {
		event = calloc(1, sizeof(*event));
		if (!event) {
			fprintf(stderr, "%s: out of memory\n", __func__);
			return -ENOMEM;
		}

		memcpy((void *)&event->cqe, buf + written_len, sizeof(*buf));
		event->error = !!event->cqe.err.err;
		shmx_cq_enqueue_event(cq_priv, event);
		written_len += sizeof(*buf);
		len -= sizeof(*buf);
	}

	return written_len;
}
#endif /* 0 */

static const char *
shmx_cq_strerror(struct fid_cq *cq, int prov_errno, const void *prov_data,
                 char *buf, size_t len)
{
	return NULL /*psm_error_get_string(prov_errno)*/;
}

static int shmx_cq_close(fid_t fid)
{
	struct shmx_fid_cq *cq;

	cq = container_of(fid, struct shmx_fid_cq, cq.fid);

        fi_shm_cq_fini(&cq->shm_cq);

	free(cq);

	return 0;
}

static int shmx_cq_control(struct fid *fid, int command, void *arg)
{
	struct shmx_fid_cq *cq;
	int ret = 0;

	cq = container_of(fid, struct shmx_fid_cq, cq.fid);

        (void)cq;

	return ret;
}

static struct fi_ops shmx_fi_ops = {
	.size    = sizeof(struct fi_ops),
	.close   = shmx_cq_close,
	.bind    = fi_no_bind,
	.control = shmx_cq_control,
	.ops_open = fi_no_ops_open,
};

static struct fi_ops_cq shmx_cq_ops = {
	.size      = sizeof(struct fi_ops_cq),
	.read      = shmx_cq_read,
	.readfrom  = shmx_cq_readfrom,
	.readerr   = shmx_cq_readerr,
	.sread     = fi_no_cq_sread,
	.sreadfrom = fi_no_cq_sreadfrom,
	.strerror  = shmx_cq_strerror,
};

int
shmx_cq_open(struct fid_domain *domain, struct fi_cq_attr *attr,
             struct fid_cq **cq, void *context)
{
	struct shmx_fid_domain *domain_priv = NULL;
	struct shmx_fid_cq *cq_priv         = NULL;

	int entry_size;

	switch (attr->format) {
	case FI_CQ_FORMAT_UNSPEC:
		attr->format = FI_CQ_FORMAT_TAGGED;
		entry_size = sizeof(struct fi_cq_tagged_entry);
		break;

	case FI_CQ_FORMAT_CONTEXT:
		entry_size = sizeof(struct fi_cq_entry);
		break;

	case FI_CQ_FORMAT_TAGGED:
		entry_size = sizeof(struct fi_cq_tagged_entry);
		break;

	default:
                SHMX_DEBUG("%s: attr->format=%d, supported=%d...%d\n", __func__, attr->format,
                            FI_CQ_FORMAT_UNSPEC, FI_CQ_FORMAT_TAGGED);
		return -FI_EINVAL;
	}

	domain_priv = container_of(domain, struct shmx_fid_domain, domain);

	cq_priv = (struct shmx_fid_cq *) calloc(1, sizeof *cq_priv);

	if (!cq_priv) {
		return -FI_ENOMEM;
	}

	cq_priv->domain     = domain_priv;
        cq_priv->ep         = domain_priv->ep;
	cq_priv->format     = attr->format;
	cq_priv->entry_size = entry_size;

	cq_priv->cq.fid.fclass  = FI_CLASS_CQ;
	cq_priv->cq.fid.context = context;
	cq_priv->cq.fid.ops     = &shmx_fi_ops;
	cq_priv->cq.ops         = &shmx_cq_ops;

        fi_shm_cq_init(&cq_priv->shm_cq, attr->format, 64);
	*cq = &cq_priv->cq;
	return 0;
}

