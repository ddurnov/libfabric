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




static int
shmx_av_insert(struct fid_av *av, const void *addr, size_t count,
               fi_addr_t *fi_addr, uint64_t flags, void *context)
{
	struct shmx_fid_av *av_priv = NULL;
        int err = 0;
	int i;

	av_priv = container_of(av, struct shmx_fid_av, av);

    for (i = 0; i < count; i++)
    {
        err = fi_shm_connect(&av_priv->ep->shm_ep,
                             (void*)((char *)addr + fi_shm_addrlen()*i),
                             fi_addr + i);
    }

	return shmx_errno(err);
}

static int
shmx_av_remove(struct fid_av *av, fi_addr_t *fi_addr, size_t count,
			  uint64_t flags)
{
	struct shmx_fid_av *av_priv;
        int err = 0;

	av_priv = container_of(av, struct shmx_fid_av, av);

    (void)av_priv;

	return shmx_errno(err);
}

static int
shmx_av_lookup(struct fid_av *av, fi_addr_t fi_addr, void *addr,
			  size_t *addrlen)
{

    SHMX_DEBUG("%s: not implemented\n", __func__);

	return 0;
}

static const char *
shmx_av_straddr(struct fid_av *av, const void *addr,
				   char *buf, size_t *len)
{
	int n;

	if (!buf || !len)
		return NULL;

	n = snprintf(buf, *len, "%lx", (uint64_t)(uintptr_t)addr);
	if (n < 0)
		return NULL;

	*len = n + 1;
	return buf;
}

static int
shmx_av_close(fid_t fid)
{
	struct shmx_fid_av *av;

	av = container_of(fid, struct shmx_fid_av, av.fid);

	free(av);

	return 0;
}

static struct fi_ops shmx_fi_ops = {
	.size     = sizeof(struct fi_ops),
	.close    = shmx_av_close,
	.bind     = fi_no_bind,
	.control  = fi_no_control,
	.ops_open = fi_no_ops_open,
};

static struct fi_ops_av shmx_av_ops = {
	.size    = sizeof(struct fi_ops_av),
	.insert  = shmx_av_insert,
	.remove  = shmx_av_remove,
	.lookup  = shmx_av_lookup,
	.straddr = shmx_av_straddr,
};

int shmx_av_open(struct fid_domain *domain, struct fi_av_attr *attr,
		 struct fid_av **av, void *context)
{
	struct shmx_fid_domain *domain_priv = NULL;
	struct shmx_fid_av *av_priv         = NULL;
	int type     = FI_AV_MAP;
	size_t count = 64;

	domain_priv = container_of(domain, struct shmx_fid_domain, domain);

	if (attr) {
		switch (attr->type) {
		case FI_AV_MAP:
		case FI_AV_TABLE:
			type = attr->type;
			break;
		default:
                        SHMX_DEBUG("%s: attr->type=%d, supported=%d %d\n",
                        __func__, attr->type, FI_AV_MAP, FI_AV_TABLE);
			return -EINVAL;
		}

		count = attr->count;
	}

	av_priv = (struct shmx_fid_av *) calloc(1, sizeof *av_priv);

	if (!av_priv)
		return -ENOMEM;

	av_priv->domain  = domain_priv;
	av_priv->type    = type;
        av_priv->addrlen = fi_shm_addrlen();
	av_priv->count   = count;

	av_priv->av.fid.fclass  = FI_CLASS_AV;
	av_priv->av.fid.context = context;
	av_priv->av.fid.ops     = &shmx_fi_ops;
	av_priv->av.ops         = &shmx_av_ops;

	*av = &av_priv->av;

	return 0;
}

