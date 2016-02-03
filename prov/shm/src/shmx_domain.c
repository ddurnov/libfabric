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

static int shmx_domain_close(fid_t fid)
{
	struct shmx_fid_domain *domain;
	/* int err; */

	domain = container_of(fid, struct shmx_fid_domain, domain.fid);

    /* TODO: iterate through all connections and detach/remove them */

	free(domain);

	return 0;
}

static struct fi_ops shmx_fi_ops = {
	.size    = sizeof(struct fi_ops),
	.close   = shmx_domain_close,
	.bind    = fi_no_bind,
	.control = fi_no_control,
};

static struct fi_ops_domain shmx_domain_ops = {
	.size = sizeof(struct fi_ops_domain),
	.av_open = shmx_av_open,
	.cq_open = shmx_cq_open,
	.endpoint = shmx_ep_open,
	.scalable_ep = fi_no_scalable_ep,
	.cntr_open = fi_no_cntr_open,
	.poll_open = fi_no_poll_open,
	.stx_ctx = fi_no_stx_context,
	.srx_ctx = fi_no_srx_context,
};

static int
shmx_mr_close(fid_t fid)
{
	struct shmx_fid_mr *mr;

    SHMX_DEBUG("%s: not implemented\n", __func__);

	mr = container_of(fid, struct shmx_fid_mr, mr.fid);
	free(mr);

	return 0;
}

static int
shmx_mr_bind(struct fid *fid, struct fid *bfid, uint64_t flags)
{
	struct shmx_fid_mr *mr;
	struct shmx_fid_cq *cq;
	struct shmx_fid_ep *ep;

    SHMX_DEBUG("%s: not implemented\n", __func__);

	mr = container_of(fid, struct shmx_fid_mr, mr.fid);

	if (!bfid)
		return -FI_EINVAL;
	switch (bfid->fclass) {
	case FI_CLASS_EP:
		ep = container_of(bfid, struct shmx_fid_ep, ep.fid);
		if (mr->domain != ep->domain)
			return -FI_EINVAL;
		break;

	case FI_CLASS_CQ:
		cq = container_of(bfid, struct shmx_fid_cq, cq.fid);
		if (mr->cq && mr->cq != cq)
			return -FI_EBUSY;
		if (mr->domain != cq->domain)
			return -FI_EINVAL;
		mr->cq = cq;
		break;

	default:
		return -FI_ENOSYS;
	}

	return 0;
}

static struct fi_ops shmx_mr_fi_ops = {
	.size     = sizeof(struct fi_ops),
	.close    = shmx_mr_close,
	.bind     = shmx_mr_bind,
	.control  = fi_no_control,
	.ops_open = fi_no_ops_open,
};

static int
shmx_mr_reg(struct fid *fid, const void *buf, size_t len,
			uint64_t access, uint64_t offset, uint64_t requested_key,
			uint64_t flags, struct fid_mr **mr, void *context)
{
	struct fid_domain *domain;
	struct shmx_fid_domain *domain_priv;
	struct shmx_fid_mr *mr_priv;
	uint64_t key;

    SHMX_DEBUG("%s: not implemented\n", __func__);

	if (fid->fclass != FI_CLASS_DOMAIN) {
		return -FI_EINVAL;
	}
	domain = container_of(fid, struct fid_domain, fid);

	domain_priv = container_of(domain, struct shmx_fid_domain, domain);

	mr_priv = (struct shmx_fid_mr *) calloc(1, sizeof(*mr_priv) + sizeof(struct iovec));
	if (!mr_priv)
		return -FI_ENOMEM;

	mr_priv->mr.fid.fclass = FI_CLASS_MR;
	mr_priv->mr.fid.context = context;
	mr_priv->mr.fid.ops = &shmx_mr_fi_ops;
	mr_priv->mr.mem_desc = mr_priv;

    key = (uint64_t)(uintptr_t)mr_priv;

	mr_priv->mr.key = key;
	mr_priv->domain = domain_priv;
	mr_priv->access = access;
	mr_priv->flags = flags;
	mr_priv->iov_count = 1;
	mr_priv->iov[0].iov_base = (void *)buf;
	mr_priv->iov[0].iov_len = len;
	mr_priv->offset = 0;

	*mr = &mr_priv->mr;

	return 0;
}

static int
shmx_mr_regv(struct fid *fid,
			const struct iovec *iov, size_t count,
			uint64_t access, uint64_t offset, uint64_t requested_key,
			uint64_t flags, struct fid_mr **mr, void *context)
{
	struct fid_domain *domain;
	struct shmx_fid_domain *domain_priv;
	struct shmx_fid_mr *mr_priv;
	int i;
	uint64_t key;

    SHMX_DEBUG("%s: not implemented\n", __func__);

	if (fid->fclass != FI_CLASS_DOMAIN) {
		return -FI_EINVAL;
	}
	domain = container_of(fid, struct fid_domain, fid);

	domain_priv = container_of(domain, struct shmx_fid_domain, domain);

	if (count == 0 || iov == NULL)
		return -FI_EINVAL;

	mr_priv = (struct shmx_fid_mr *)
			calloc(1, sizeof(*mr_priv) +
				  sizeof(struct iovec) * count);
	if (!mr_priv)
		return -FI_ENOMEM;

	mr_priv->mr.fid.fclass = FI_CLASS_MR;
	mr_priv->mr.fid.context = context;
	mr_priv->mr.fid.ops = &shmx_mr_fi_ops;
	mr_priv->mr.mem_desc = mr_priv;
    key = (uint64_t)(uintptr_t)mr_priv;
	mr_priv->mr.key = key;
	mr_priv->domain = domain_priv;
	mr_priv->access = access;
	mr_priv->flags = flags;
	mr_priv->iov_count = count;
	for (i=0; i<count; i++)
		mr_priv->iov[i] = iov[i];

	mr_priv->offset = 0;

	*mr = &mr_priv->mr;

	return 0;
}

static int
shmx_mr_regattr(struct fid *fid, const struct fi_mr_attr *attr,
			uint64_t flags, struct fid_mr **mr)
{
	struct fid_domain *domain;
	struct shmx_fid_domain *domain_priv;
	struct shmx_fid_mr *mr_priv;
	int i;
	uint64_t key;

    SHMX_DEBUG("%s: not implemented\n", __func__);

	if (fid->fclass != FI_CLASS_DOMAIN) {
		return -FI_EINVAL;
	}

	domain = container_of(fid, struct fid_domain, fid);

	domain_priv = container_of(domain, struct shmx_fid_domain, domain);

	if (!attr)
		return -FI_EINVAL;

	if (attr->iov_count == 0 || attr->mr_iov == NULL)
		return -FI_EINVAL;

	mr_priv = (struct shmx_fid_mr *)
			calloc(1, sizeof(*mr_priv) +
				  sizeof(struct iovec) * attr->iov_count);
	if (!mr_priv)
		return -FI_ENOMEM;

	mr_priv->mr.fid.fclass = FI_CLASS_MR;
	mr_priv->mr.fid.context = attr->context;
	mr_priv->mr.fid.ops = &shmx_mr_fi_ops;
	mr_priv->mr.mem_desc = mr_priv;
    key = (uint64_t)(uintptr_t)mr_priv;
	mr_priv->mr.key = key;
	mr_priv->domain = domain_priv;
	mr_priv->access = attr->access;
	mr_priv->flags = flags;
	mr_priv->iov_count = attr->iov_count;
	for (i=0; i<attr->iov_count; i++)
		mr_priv->iov[i] = attr->mr_iov[i];

	mr_priv->offset = 0;

	*mr = &mr_priv->mr;

	return 0;
}

struct fi_ops_mr shmx_mr_ops = {
	.size    = sizeof(struct fi_ops_mr),
	.reg     = shmx_mr_reg,
	.regv    = shmx_mr_regv,
	.regattr = shmx_mr_regattr,
};

int shmx_domain_open(struct fid_fabric *fabric, struct fi_info *info,
                     struct fid_domain **domain, void *context)
{
	struct shmx_fid_domain *domain_priv;

	int err = -ENOMEM;

        SHMX_DEBUG("%s\n", __func__);

	if (!info->domain_attr->name || strncmp(info->domain_attr->name, "shm", 3))
		return -EINVAL;

	domain_priv = (struct shmx_fid_domain *) calloc(1, sizeof *domain_priv);

	if (!domain_priv)
		goto err_out;

	domain_priv->domain.fid.fclass  = FI_CLASS_DOMAIN;
	domain_priv->domain.fid.context = context;
	domain_priv->domain.fid.ops     = &shmx_fi_ops;
	domain_priv->domain.ops         = &shmx_domain_ops;
	domain_priv->domain.mr          = &shmx_mr_ops;
	domain_priv->mode               = info->mode;


	*domain = &domain_priv->domain;
	return 0;

/* err_out_free_domain: */
	free(domain_priv);

err_out:
	return err;
}

int shmx_domain_check_features(struct shmx_fid_domain *domain, int ep_cap)
{
	if ((ep_cap & SHMX_CAPS) != ep_cap)
		return -EINVAL;

	if ((ep_cap & FI_TAGGED) && domain->ep)
		return -EBUSY;

	return 0;
}


void shmx_domain_disable_ep(struct shmx_fid_domain *domain, struct shmx_fid_ep *ep)
{
	if (!ep)
		return;

	if ((ep->caps & FI_TAGGED) && domain->ep == ep)
		domain->ep = NULL;
}

