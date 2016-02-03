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

static void shmx_ep_optimize_ops(struct shmx_fid_ep *ep)
{
    return;
}

static ssize_t shmx_ep_cancel(fid_t fid, void *context)
{
	struct shmx_fid_ep *ep;

	struct fi_context *fi_context = context;
        int err = 0;

	ep = container_of(fid, struct shmx_fid_ep, ep.fid);
	if (!ep->domain)
		return -EBADF;

	if (!fi_context)
		return -EINVAL;

	return shmx_errno(err);
}

static int shmx_ep_getopt(fid_t fid, int level, int optname,
			void *optval, size_t *optlen)
{
	struct shmx_fid_ep *ep;

	ep = container_of(fid, struct shmx_fid_ep, ep.fid);

    (void)ep;

	if (level != FI_OPT_ENDPOINT)
		return -ENOPROTOOPT;

	return 0;
}

static int shmx_ep_setopt(fid_t fid, int level, int optname,
			const void *optval, size_t optlen)
{
	struct shmx_fid_ep *ep;

	ep = container_of(fid, struct shmx_fid_ep, ep.fid);

    (void)ep;

	if (level != FI_OPT_ENDPOINT)
		return -ENOPROTOOPT;

	return 0;
}

static int shmx_ep_close(fid_t fid)
{
	struct shmx_fid_ep *ep_priv = NULL;

	ep_priv = container_of(fid, struct shmx_fid_ep, ep.fid);

	shmx_domain_disable_ep(ep_priv->domain, ep_priv);

        fi_shm_fini_ep(&ep_priv->shm_ep);
	free(ep_priv);

	return 0;
}

static int shmx_ep_bind(struct fid *fid, struct fid *bfid, uint64_t flags)
{
	struct shmx_fid_ep *ep;
	struct shmx_fid_av *av;
	struct shmx_fid_cq *cq;

	ep = container_of(fid, struct shmx_fid_ep, ep.fid);

	if (!bfid)
		return -EINVAL;
	switch (bfid->fclass) {
	case FI_CLASS_EQ:
		return -FI_ENOSYS;

	case FI_CLASS_CQ:
		cq = container_of(bfid, struct shmx_fid_cq, cq.fid);
		if (ep->domain != cq->domain)
                        return -EINVAL;
                fi_shm_ep_bind_cq(&ep->shm_ep, &cq->shm_cq, flags);
                shmx_ep_optimize_ops(ep);
		break;

	case FI_CLASS_MR:
		/* if (!bfid->ops || !bfid->ops->bind) */
		/* 	return -FI_EINVAL; */
		/* err = bfid->ops->bind(bfid, fid, flags); */
		/* if (err) */
		/* 	return err; */
		break;

	case FI_CLASS_AV:
		av = container_of(bfid,
				struct shmx_fid_av, av.fid);
		if (ep->domain != av->domain)
			return -EINVAL;
		ep->av = av;
        av->ep = ep; /* Cross connection */
		shmx_ep_optimize_ops(ep);
		break;

	default:
		return -ENOSYS;
	}

	return 0;
}


static int shmx_ep_control(fid_t fid, int command, void *arg)
{
	struct fi_alias *alias;
	struct shmx_fid_ep *ep, *new_ep;
	ep = container_of(fid, struct shmx_fid_ep, ep.fid);

	switch (command) {
	case FI_ALIAS:
		new_ep = (struct shmx_fid_ep *) calloc(1, sizeof *ep);
		if (!new_ep)
			return -FI_ENOMEM;
		alias = arg;
		*new_ep = *ep;
		new_ep->flags = alias->flags;
		shmx_ep_optimize_ops(new_ep);
		*alias->fid = &new_ep->ep.fid;
		break;

	case FI_SETFIDFLAG:
		ep->flags = *(uint64_t *)arg;
		shmx_ep_optimize_ops(ep);
		break;

	case FI_GETFIDFLAG:
		if (!arg)
			return -FI_EINVAL;
		*(uint64_t *)arg = ep->flags;
		break;

	case FI_ENABLE:
		return 0;

	default:
		return -FI_ENOSYS;
	}

	return 0;
}

static struct fi_ops shmx_fi_ops = {
	.size = sizeof(struct fi_ops),
	.close = shmx_ep_close,
	.bind = shmx_ep_bind,
	.control = shmx_ep_control,
	.ops_open = fi_no_ops_open,
};

static struct fi_ops_ep shmx_ep_ops = {
	.size = sizeof(struct fi_ops_ep),
	.cancel = shmx_ep_cancel,
	.getopt = shmx_ep_getopt,
	.setopt = shmx_ep_setopt,
	.tx_ctx = fi_no_tx_ctx,
	.rx_ctx = fi_no_rx_ctx,
	.rx_size_left = fi_no_rx_size_left,
	.tx_size_left = fi_no_tx_size_left,
};

int
shmx_ep_open(struct fid_domain *domain, struct fi_info *info,
             struct fid_ep **ep, void *context)
{
	struct shmx_fid_domain * domain_priv = NULL;
	struct shmx_fid_ep     * ep_priv     = NULL;
	int err;
	uint64_t ep_cap;

        if (info)
    {
		ep_cap = info->caps;
    }
	else
    {
		ep_cap = FI_TAGGED;
    }

	domain_priv = container_of(domain, struct shmx_fid_domain, domain.fid);

	if (!domain_priv)
    {
		return -EINVAL;
    }

	err = shmx_domain_check_features(domain_priv, ep_cap);

	if (err)
    {
		return err;
    }

	ep_priv = (struct shmx_fid_ep *) calloc(1, sizeof *ep_priv);

	if (!ep_priv)
    {
		return -ENOMEM;
    }

	ep_priv->ep.fid.fclass  = FI_CLASS_EP;
	ep_priv->ep.fid.context = context;
	ep_priv->ep.fid.ops     = &shmx_fi_ops;
	ep_priv->ep.ops         = &shmx_ep_ops;
	ep_priv->ep.cm          = &shmx_cm_ops;
	ep_priv->domain         = domain_priv;

	if (ep_cap & FI_TAGGED)
    {
		ep_priv->ep.tagged = &shmx_tagged_ops;
    }

	ep_priv->caps = ep_cap;

    /* Get id for own connection control block */

        err  = fi_shm_init_ep(&ep_priv->shm_ep);
        if (err)
                goto err_out_close_ep;
	if (info)
    {
		if (info->tx_attr)
        {
			ep_priv->flags = info->tx_attr->op_flags;
        }

		if (info->rx_attr)
        {
			ep_priv->flags |= info->rx_attr->op_flags;
        }
	}

	shmx_ep_optimize_ops(ep_priv);

	*ep = &ep_priv->ep;

 err_out_close_ep:

	return 0;
}
