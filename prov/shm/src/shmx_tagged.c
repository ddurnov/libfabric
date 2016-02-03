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

static ssize_t shmx_tagged_recv(struct fid_ep *ep, void *buf, size_t len, void *desc,
				fi_addr_t src_addr,
				uint64_t tag, uint64_t ignore, void *context)
{
	struct shmx_fid_ep *ep_priv;

	ep_priv = container_of(ep, struct shmx_fid_ep, ep);

	return fi_shm_tagged_recv(&ep_priv->shm_ep, buf, len, src_addr, tag, ignore, context);
}

static ssize_t shmx_tagged_recvv(struct fid_ep *ep, const struct iovec *iov, void **desc,
				 size_t count, fi_addr_t src_addr,
				 uint64_t tag, uint64_t ignore, void *context)
{
	void *buf;
	size_t len;

	struct shmx_fid_ep *ep_priv;

	ep_priv = container_of(ep, struct shmx_fid_ep, ep);

	if (!iov || count > 1)
		return -EINVAL;

	if (count) {
		buf = iov[0].iov_base;
		len = iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

	return fi_shm_tagged_recv(&ep_priv->shm_ep, buf, len, src_addr, tag, ignore, context);
}

static ssize_t shmx_tagged_recvmsg(struct fid_ep *ep, const struct fi_msg_tagged *msg,
				   uint64_t flags)
{
	void *buf;
	size_t len;

	struct shmx_fid_ep *ep_priv;

	ep_priv = container_of(ep, struct shmx_fid_ep, ep);

	if (!msg || msg->iov_count > 1)
		return -FI_EINVAL;

	if (msg->iov_count) {
		buf = msg->msg_iov[0].iov_base;
		len = msg->msg_iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

	return fi_shm_tagged_recv(&ep_priv->shm_ep, buf, len,
				  msg->addr, msg->tag, msg->ignore, msg->context);
}

static ssize_t shmx_tagged_send(struct fid_ep *ep, const void *buf, size_t len,
				void *desc, fi_addr_t dest_addr,
				uint64_t tag, void *context)
{
	struct shmx_fid_ep *ep_priv;

	ep_priv = container_of(ep, struct shmx_fid_ep, ep);

	return fi_shm_tagged_send(&ep_priv->shm_ep, buf, len, dest_addr, tag, context);
}

static ssize_t shmx_tagged_sendv(struct fid_ep *ep, const struct iovec *iov, void **desc,
				 size_t count, fi_addr_t dest_addr, uint64_t tag, void *context)
{
	void *buf;
	size_t len;

	struct shmx_fid_ep *ep_priv;

	ep_priv = container_of(ep, struct shmx_fid_ep, ep);

	if (!iov || count > 1)
		return -EINVAL;

	if (count) {
		buf = iov[0].iov_base;
		len = iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

	return fi_shm_tagged_send(&ep_priv->shm_ep, buf, len, dest_addr, tag, context);
}

static ssize_t shmx_tagged_inject(struct fid_ep *ep, const void *buf, size_t len,
				  fi_addr_t dest_addr, uint64_t tag)
{
	struct shmx_fid_ep *ep_priv;

	ep_priv = container_of(ep, struct shmx_fid_ep, ep);

	return fi_shm_tagged_inject(&ep_priv->shm_ep, buf, len, dest_addr, tag);
}

/* general case */
struct fi_ops_tagged shmx_tagged_ops = {
	.size       = sizeof(struct fi_ops_tagged),
	.recv       = shmx_tagged_recv,
	.recvv      = shmx_tagged_recvv,
	.recvmsg    = shmx_tagged_recvmsg,
	.send       = shmx_tagged_send,
	.sendv      = shmx_tagged_sendv,
	.sendmsg    = fi_no_tagged_sendmsg,
	.inject     = shmx_tagged_inject,
	.senddata   = fi_no_tagged_senddata,
	.injectdata = fi_no_tagged_injectdata,
};
