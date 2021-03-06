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

#include "psmx.h"

ssize_t _psmx_tagged_peek(struct fid_ep *ep, void *buf, size_t len,
			  void *desc, fi_addr_t src_addr,
			  uint64_t tag, uint64_t ignore,
			  void *context, uint64_t flags)
{
	struct psmx_fid_ep *ep_priv;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_status2_t psm_status2;
	psm_mq_tag_t psm_tag2, psm_tagsel2;
	psm_mq_req_t req;
	struct psmx_fid_av *av;
	size_t idx;
	psm_epaddr_t psm_src_addr;
#else
	psm_mq_status_t psm_status;
#endif
	uint64_t psm_tag, psm_tagsel;
	struct psmx_cq_event *event;
	int err;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	if (tag & ep_priv->domain->reserved_tag_bits) {
		FI_WARN(&psmx_prov, FI_LOG_EP_DATA,
			"using reserved tag bits."
			"tag=%lx. reserved_bits=%lx.\n", tag,
			ep_priv->domain->reserved_tag_bits);
	}

	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
	psm_tagsel = (~ignore) | ep_priv->domain->reserved_tag_bits;

#if (PSM_VERNO_MAJOR >= 2)
	if (src_addr != FI_ADDR_UNSPEC) {
		av = ep_priv->av;
		if (av && av->type == FI_AV_TABLE) {
			idx = (size_t)src_addr;
			if (idx >= av->last)
				return -FI_EINVAL;

			psm_src_addr = av->psm_epaddrs[idx];
		}
		else {
			psm_src_addr = (psm_epaddr_t)src_addr;
		}
	}
	else {
		psm_src_addr = NULL;
	}

	PSMX_SET_TAG(psm_tag2, psm_tag, 0);
	PSMX_SET_TAG(psm_tagsel2, psm_tagsel, 0);

	if (flags & (FI_CLAIM | FI_DISCARD))
		err = psm_mq_improbe2(ep_priv->domain->psm_mq,
				      psm_src_addr,
				      &psm_tag2, &psm_tagsel2,
				      &req, &psm_status2);
	else
		err = psm_mq_iprobe2(ep_priv->domain->psm_mq,
				     psm_src_addr,
				     &psm_tag2, &psm_tagsel2,
				     &psm_status2);
#else
	if (flags & (FI_CLAIM | FI_DISCARD))
		return -FI_EOPNOTSUPP;

	err = psm_mq_iprobe(ep_priv->domain->psm_mq, psm_tag, psm_tagsel,
			    &psm_status);
#endif
	switch (err) {
	case PSM_OK:
		if (ep_priv->recv_cq) {
#if (PSM_VERNO_MAJOR >= 2)
			if ((flags & FI_CLAIM) && context)
				PSMX_CTXT_REQ((struct fi_context *)context) = req;

			tag = psm_status2.msg_tag.tag0 | (((uint64_t)psm_status2.msg_tag.tag1) << 32);
			len = psm_status2.msg_length;
			src_addr = (fi_addr_t)psm_status2.msg_peer;
#else
			tag = psm_status.msg_tag;
			len = psm_status.msg_length;
			src_addr = 0;
#endif
			event = psmx_cq_create_event(
					ep_priv->recv_cq,
					context,		/* op_context */
					NULL,			/* buf */
					flags|FI_RECV|FI_TAGGED,/* flags */
					len,			/* len */
					0,			/* data */
					tag,			/* tag */
					len,			/* olen */
					0);			/* err */

			if (!event)
				return -FI_ENOMEM;

			event->source = src_addr;
			psmx_cq_enqueue_event(ep_priv->recv_cq, event);
		}
		return 0;

	case PSM_MQ_NO_COMPLETIONS:
		if (ep_priv->recv_cq) {
			event = psmx_cq_create_event(
					ep_priv->recv_cq,
					context,		/* op_context */
					NULL,			/* buf */
					flags|FI_RECV|FI_TAGGED,/* flags */
					len,			/* len */
					0,			/* data */
					tag,			/* tag */
					len,			/* olen */
					-FI_ENOMSG);		/* err */

			if (!event)
				return -FI_ENOMEM;

			event->source = 0;
			psmx_cq_enqueue_event(ep_priv->recv_cq, event);
		}
		return 0;

	default:
		return psmx_errno(err);
	}
}

ssize_t _psmx_tagged_recv(struct fid_ep *ep, void *buf, size_t len,
			  void *desc, fi_addr_t src_addr,
			  uint64_t tag, uint64_t ignore,
			  void *context, uint64_t flags)
{
	struct psmx_fid_ep *ep_priv;
	psm_mq_req_t psm_req;
	uint64_t psm_tag, psm_tagsel;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_tag_t psm_tag2, psm_tagsel2;
	struct psmx_fid_av *av;
	size_t idx;
#endif
	struct fi_context *fi_context;
	int err;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	if (flags & FI_PEEK)
		return _psmx_tagged_peek(ep, buf, len, desc, src_addr,
					 tag, ignore, context, flags);

	if (flags & FI_TRIGGER) {
		struct psmx_trigger *trigger;
		struct fi_triggered_context *ctxt = context;

		trigger = calloc(1, sizeof(*trigger));
		if (!trigger)
			return -FI_ENOMEM;

		trigger->op = PSMX_TRIGGERED_TRECV;
		trigger->cntr = container_of(ctxt->trigger.threshold.cntr,
					     struct psmx_fid_cntr, cntr);
		trigger->threshold = ctxt->trigger.threshold.threshold;
		trigger->trecv.ep = ep;
		trigger->trecv.buf = buf;
		trigger->trecv.len = len;
		trigger->trecv.desc = desc;
		trigger->trecv.src_addr = src_addr;
		trigger->trecv.tag = tag;
		trigger->trecv.ignore = ignore;
		trigger->trecv.context = context;
		trigger->trecv.flags = flags & ~FI_TRIGGER;

		psmx_cntr_add_trigger(trigger->cntr, trigger);
		return 0;
	}

#if (PSM_VERNO_MAJOR >= 2)
	if (flags & FI_CLAIM) {
		if (!context)
			return -FI_EINVAL;

		/* TODO: handle FI_DISCARD */

		fi_context = context;
		psm_req = PSMX_CTXT_REQ(fi_context);
		PSMX_CTXT_TYPE(fi_context) = PSMX_TRECV_CONTEXT;
		PSMX_CTXT_USER(fi_context) = buf;
		PSMX_CTXT_EP(fi_context) = ep_priv;

		err = psm_mq_imrecv(ep_priv->domain->psm_mq, 0, /*flags*/
				    buf, len, context, &psm_req);
		if (err != PSM_OK)
			return psmx_errno(err);

		PSMX_CTXT_REQ(fi_context) = psm_req;
		return 0;
	}
#endif

	if (tag & ep_priv->domain->reserved_tag_bits) {
		FI_WARN(&psmx_prov, FI_LOG_EP_DATA,
			"using reserved tag bits."
			"tag=%lx. reserved_bits=%lx.\n", tag,
			ep_priv->domain->reserved_tag_bits);
	}

	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
	psm_tagsel = (~ignore) | ep_priv->domain->reserved_tag_bits;

	if (ep_priv->recv_selective_completion && !(flags & FI_COMPLETION)) {
		fi_context = &ep_priv->nocomp_recv_context;
	}
	else {
		if (!context)
			return -FI_EINVAL;

		fi_context = context;
		PSMX_CTXT_TYPE(fi_context) = PSMX_TRECV_CONTEXT;
		PSMX_CTXT_USER(fi_context) = buf;
		PSMX_CTXT_EP(fi_context) = ep_priv;
	}

#if (PSM_VERNO_MAJOR >= 2)
	if ((ep_priv->caps & FI_DIRECTED_RECV) && src_addr != FI_ADDR_UNSPEC) {
		av = ep_priv->av;
		if (av && av->type == FI_AV_TABLE) {
			idx = (size_t)src_addr;
			if (idx >= av->last)
				return -FI_EINVAL;

			src_addr = (fi_addr_t)av->psm_epaddrs[idx];
		}
	}
	else {
		src_addr = 0;
	}

	PSMX_SET_TAG(psm_tag2, psm_tag, 0);
	PSMX_SET_TAG(psm_tagsel2, psm_tagsel, 0);

	err = psm_mq_irecv2(ep_priv->domain->psm_mq,
			   (psm_epaddr_t)src_addr,
			   &psm_tag2, &psm_tagsel2, 0, /* flags */
			   buf, len, (void *)fi_context, &psm_req);
#else
	err = psm_mq_irecv(ep_priv->domain->psm_mq,
			   psm_tag, psm_tagsel, 0, /* flags */
			   buf, len, (void *)fi_context, &psm_req);
#endif

	if (err != PSM_OK)
		return psmx_errno(err);

	if (fi_context == context)
		PSMX_CTXT_REQ(fi_context) = psm_req;

	return 0;
}

ssize_t psmx_tagged_recv_no_flag_av_map(struct fid_ep *ep, void *buf,
					size_t len, void *desc,
					fi_addr_t src_addr,
					uint64_t tag, uint64_t ignore,
					void *context)
{
	struct psmx_fid_ep *ep_priv;
	psm_mq_req_t psm_req;
	uint64_t psm_tag, psm_tagsel;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_tag_t psm_tag2, psm_tagsel2;
#endif
	struct fi_context *fi_context;
	int err;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
	psm_tagsel = (~ignore) | ep_priv->domain->reserved_tag_bits;

	fi_context = context;
	PSMX_CTXT_TYPE(fi_context) = PSMX_TRECV_CONTEXT;
	PSMX_CTXT_USER(fi_context) = buf;
	PSMX_CTXT_EP(fi_context) = ep_priv;

#if (PSM_VERNO_MAJOR >= 2)
	if (! ((ep_priv->caps & FI_DIRECTED_RECV) && src_addr != FI_ADDR_UNSPEC))
		src_addr = 0;

	PSMX_SET_TAG(psm_tag2, psm_tag, 0);
	PSMX_SET_TAG(psm_tagsel2, psm_tagsel, 0);

	err = psm_mq_irecv2(ep_priv->domain->psm_mq,
			   (psm_epaddr_t)src_addr,
			   &psm_tag2, &psm_tagsel2, 0, /* flags */
			   buf, len, (void *)fi_context, &psm_req);
#else
	err = psm_mq_irecv(ep_priv->domain->psm_mq,
			   psm_tag, psm_tagsel, 0, /* flags */
			   buf, len, (void *)fi_context, &psm_req);
#endif
	if (err != PSM_OK)
		return psmx_errno(err);

	PSMX_CTXT_REQ(fi_context) = psm_req;
	return 0;
}

ssize_t psmx_tagged_recv_no_flag_av_table(struct fid_ep *ep, void *buf,
					  size_t len, void *desc,
					  fi_addr_t src_addr,
					  uint64_t tag, uint64_t ignore,
					  void *context)
{
	struct psmx_fid_ep *ep_priv;
	psm_mq_req_t psm_req;
	uint64_t psm_tag, psm_tagsel;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_tag_t psm_tag2, psm_tagsel2;
	struct psmx_fid_av *av;
	psm_epaddr_t psm_epaddr;
	size_t idx;
#endif
	struct fi_context *fi_context;
	int err;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
	psm_tagsel = (~ignore) | ep_priv->domain->reserved_tag_bits;

	fi_context = context;
	PSMX_CTXT_TYPE(fi_context) = PSMX_TRECV_CONTEXT;
	PSMX_CTXT_USER(fi_context) = buf;
	PSMX_CTXT_EP(fi_context) = ep_priv;

#if (PSM_VERNO_MAJOR >= 2)
	if ((ep_priv->caps & FI_DIRECTED_RECV) && src_addr != FI_ADDR_UNSPEC) {
		av = ep_priv->av;
		idx = (size_t)src_addr;
		if (idx >= av->last)
			return -FI_EINVAL;

		psm_epaddr = av->psm_epaddrs[idx];
	}
	else {
		psm_epaddr = NULL;
	}

	PSMX_SET_TAG(psm_tag2, psm_tag, 0);
	PSMX_SET_TAG(psm_tagsel2, psm_tagsel, 0);

	err = psm_mq_irecv2(ep_priv->domain->psm_mq,
			   psm_epaddr,
			   &psm_tag2, &psm_tagsel2, 0, /* flags */
			   buf, len, (void *)fi_context, &psm_req);
#else
	err = psm_mq_irecv(ep_priv->domain->psm_mq,
			   psm_tag, psm_tagsel, 0, /* flags */
			   buf, len, (void *)fi_context, &psm_req);
#endif
	if (err != PSM_OK)
		return psmx_errno(err);

	PSMX_CTXT_REQ(fi_context) = psm_req;
	return 0;
}

ssize_t psmx_tagged_recv_no_event_av_map(struct fid_ep *ep, void *buf,
					 size_t len, void *desc,
					 fi_addr_t src_addr,
					 uint64_t tag, uint64_t ignore,
					 void *context)
{
	struct psmx_fid_ep *ep_priv;
	psm_mq_req_t psm_req;
	uint64_t psm_tag, psm_tagsel;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_tag_t psm_tag2, psm_tagsel2;
#endif
	struct fi_context *fi_context;
	int err;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
	psm_tagsel = (~ignore) | ep_priv->domain->reserved_tag_bits;

	fi_context = &ep_priv->nocomp_recv_context;

#if (PSM_VERNO_MAJOR >= 2)
	if (! ((ep_priv->caps & FI_DIRECTED_RECV) && src_addr != FI_ADDR_UNSPEC))
		src_addr = 0;

	PSMX_SET_TAG(psm_tag2, psm_tag, 0);
	PSMX_SET_TAG(psm_tagsel2, psm_tagsel, 0);

	err = psm_mq_irecv2(ep_priv->domain->psm_mq,
			   (psm_epaddr_t)src_addr,
			   &psm_tag2, &psm_tagsel2, 0, /* flags */
			   buf, len, (void *)fi_context, &psm_req);
#else
	err = psm_mq_irecv(ep_priv->domain->psm_mq,
			   psm_tag, psm_tagsel, 0, /* flags */
			   buf, len, (void *)fi_context, &psm_req);
#endif

	return psmx_errno(err);
}

ssize_t psmx_tagged_recv_no_event_av_table(struct fid_ep *ep, void *buf,
					   size_t len, void *desc,
					   fi_addr_t src_addr,
					   uint64_t tag, uint64_t ignore,
					   void *context)
{
	struct psmx_fid_ep *ep_priv;
	psm_mq_req_t psm_req;
	uint64_t psm_tag, psm_tagsel;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_tag_t psm_tag2, psm_tagsel2;
	struct psmx_fid_av *av;
	psm_epaddr_t psm_epaddr;
	size_t idx;
#endif
	struct fi_context *fi_context;
	int err;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
	psm_tagsel = (~ignore) | ep_priv->domain->reserved_tag_bits;

	fi_context = &ep_priv->nocomp_recv_context;

#if (PSM_VERNO_MAJOR >= 2)
	if ((ep_priv->caps & FI_DIRECTED_RECV) && src_addr != FI_ADDR_UNSPEC) {
		av = ep_priv->av;
		idx = (size_t)src_addr;
		if (idx >= av->last)
			return -FI_EINVAL;

		psm_epaddr = av->psm_epaddrs[idx];
	}
	else {
		psm_epaddr = NULL;
	}

	PSMX_SET_TAG(psm_tag2, psm_tag, 0);
	PSMX_SET_TAG(psm_tagsel2, psm_tagsel, 0);

	err = psm_mq_irecv2(ep_priv->domain->psm_mq,
			   psm_epaddr,
			   &psm_tag2, &psm_tagsel2, 0, /* flags */
			   buf, len, (void *)fi_context, &psm_req);
#else
	err = psm_mq_irecv(ep_priv->domain->psm_mq,
			   psm_tag, psm_tagsel, 0, /* flags */
			   buf, len, (void *)fi_context, &psm_req);
#endif

	return psmx_errno(err);
}

static ssize_t psmx_tagged_recv(struct fid_ep *ep, void *buf, size_t len, void *desc,
				fi_addr_t src_addr,
				uint64_t tag, uint64_t ignore, void *context)
{
	struct psmx_fid_ep *ep_priv;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	return _psmx_tagged_recv(ep, buf, len, desc, src_addr, tag, ignore,
				 context, ep_priv->flags);
}

static ssize_t psmx_tagged_recvmsg(struct fid_ep *ep, const struct fi_msg_tagged *msg,
				   uint64_t flags)
{
	void *buf;
	size_t len;

	if (!msg || (msg->iov_count && !msg->msg_iov))
		return -FI_EINVAL;

	if (msg->iov_count > 1) {
		return -FI_EINVAL;
	}
	else if (msg->iov_count) {
		buf = msg->msg_iov[0].iov_base;
		len = msg->msg_iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

	return _psmx_tagged_recv(ep, buf, len,
				 msg->desc ? msg->desc[0] : NULL,
				 msg->addr, msg->tag, msg->ignore,
				 msg->context, flags);
}

static ssize_t psmx_tagged_recv_no_flag(struct fid_ep *ep, void *buf,
					size_t len, void *desc, fi_addr_t src_addr,
					uint64_t tag, uint64_t ignore,
					void *context)
{
	return psmx_tagged_recv_no_flag_av_map(
					ep, buf, len, desc, src_addr,
					tag, ignore, context);
}

static ssize_t psmx_tagged_recv_no_event(struct fid_ep *ep, void *buf,
					size_t len, void *desc, fi_addr_t src_addr,
					uint64_t tag, uint64_t ignore,
					void *context)
{
	return psmx_tagged_recv_no_event_av_map(
					ep, buf, len, desc, src_addr,
					tag, ignore, context);
}

static ssize_t psmx_tagged_recvv(struct fid_ep *ep, const struct iovec *iov, void **desc,
				 size_t count, fi_addr_t src_addr,
				 uint64_t tag, uint64_t ignore, void *context)
{
	void *buf;
	size_t len;

	if (count && !iov)
		return -FI_EINVAL;

	if (count > 1) {
		return -FI_EINVAL;
	}
	else if (count) {
		buf = iov[0].iov_base;
		len = iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

	return psmx_tagged_recv(ep, buf, len,
				desc ? desc[0] : NULL, src_addr, tag, ignore, context);
}

static ssize_t psmx_tagged_recvv_no_flag(struct fid_ep *ep, const struct iovec *iov,
					 void **desc, size_t count, fi_addr_t src_addr,
					 uint64_t tag, uint64_t ignore, void *context)
{
	void *buf;
	size_t len;

	if (count && !iov)
		return -FI_EINVAL;

	if (count > 1) {
		return -FI_EINVAL;
	}
	else if (count) {
		buf = iov[0].iov_base;
		len = iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

	return psmx_tagged_recv_no_flag(ep, buf, len,
					desc ? desc[0] : NULL, src_addr,
					tag, ignore, context);
}

static ssize_t psmx_tagged_recvv_no_event(struct fid_ep *ep, const struct iovec *iov,
					 void **desc, size_t count, fi_addr_t src_addr,
					 uint64_t tag, uint64_t ignore, void *context)
{
	void *buf;
	size_t len;

	if (count && !iov)
		return -FI_EINVAL;

	if (count > 1) {
		return -FI_EINVAL;
	}
	else if (count) {
		buf = iov[0].iov_base;
		len = iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

	return psmx_tagged_recv_no_event(ep, buf, len,
					desc ? desc[0] : NULL, src_addr,
					tag, ignore, context);
}

#if (PSM_VERNO_MAJOR >= 2)
ssize_t _psmx_tagged_send(struct fid_ep *ep, const void *buf, size_t len,
			  void *desc, fi_addr_t dest_addr, uint64_t tag,
			  void *context, uint64_t flags, uint32_t data)
#else
ssize_t _psmx_tagged_send(struct fid_ep *ep, const void *buf, size_t len,
			  void *desc, fi_addr_t dest_addr, uint64_t tag,
			  void *context, uint64_t flags)
#endif
{
	struct psmx_fid_ep *ep_priv;
	struct psmx_fid_av *av;
	psm_epaddr_t psm_epaddr;
	psm_mq_req_t psm_req;
	uint64_t psm_tag;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_tag_t psm_tag2;
#endif
	struct fi_context *fi_context;
	int err;
	size_t idx;
	int no_completion = 0;
	struct psmx_cq_event *event;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	if (flags & FI_TRIGGER) {
		struct psmx_trigger *trigger;
		struct fi_triggered_context *ctxt = context;

		trigger = calloc(1, sizeof(*trigger));
		if (!trigger)
			return -FI_ENOMEM;

		trigger->op = PSMX_TRIGGERED_TSEND;
		trigger->cntr = container_of(ctxt->trigger.threshold.cntr,
					     struct psmx_fid_cntr, cntr);
		trigger->threshold = ctxt->trigger.threshold.threshold;
		trigger->tsend.ep = ep;
		trigger->tsend.buf = buf;
		trigger->tsend.len = len;
		trigger->tsend.desc = desc;
		trigger->tsend.dest_addr = dest_addr;
		trigger->tsend.tag = tag;
		trigger->tsend.context = context;
		trigger->tsend.flags = flags & ~FI_TRIGGER;
#if (PSM_VERNO_MAJOR >= 2)
		trigger->tsend.data = data;
#endif

		psmx_cntr_add_trigger(trigger->cntr, trigger);
		return 0;
	}

	if (tag & ep_priv->domain->reserved_tag_bits) {
		FI_WARN(&psmx_prov, FI_LOG_EP_DATA,
			"using reserved tag bits."
			"tag=%lx. reserved_bits=%lx.\n", tag,
			ep_priv->domain->reserved_tag_bits);
	}

	av = ep_priv->av;
	if (av && av->type == FI_AV_TABLE) {
		idx = (size_t)dest_addr;
		if (idx >= av->last)
			return -FI_EINVAL;

		psm_epaddr = av->psm_epaddrs[idx];
	}
	else  {
		psm_epaddr = (psm_epaddr_t) dest_addr;
	}

	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
#if (PSM_VERNO_MAJOR >= 2)
	PSMX_SET_TAG(psm_tag2, psm_tag, data);
#endif

	if ((flags & PSMX_NO_COMPLETION) ||
	    (ep_priv->send_selective_completion && !(flags & FI_COMPLETION)))
		no_completion = 1;

	if (flags & FI_INJECT) {
		if (len > PSMX_INJECT_SIZE)
			return -FI_EMSGSIZE;

#if (PSM_VERNO_MAJOR >= 2)
		err = psm_mq_send2(ep_priv->domain->psm_mq, psm_epaddr, 0,
				   &psm_tag2, buf, len);
#else
		err = psm_mq_send(ep_priv->domain->psm_mq, psm_epaddr, 0,
				  psm_tag, buf, len);
#endif

		if (err != PSM_OK)
			return psmx_errno(err);

		if (ep_priv->send_cntr)
			psmx_cntr_inc(ep_priv->send_cntr);

		if (ep_priv->send_cq && !no_completion) {
			event = psmx_cq_create_event(
					ep_priv->send_cq,
					context, (void *)buf, flags, len,
#if (PSM_VERNO_MAJOR >= 2)
					(uint64_t) data, psm_tag,
#else
					0 /* data */, psm_tag,
#endif
					0 /* olen */,
					0 /* err */);

			if (event)
				psmx_cq_enqueue_event(ep_priv->send_cq, event);
			else
				return -FI_ENOMEM;
		}

		return 0;
	}

	if (no_completion && !context) {
		fi_context = &ep_priv->nocomp_send_context;
	}
	else {
		if (!context)
			return -FI_EINVAL;

		fi_context = context;
		PSMX_CTXT_TYPE(fi_context) = PSMX_TSEND_CONTEXT;
		PSMX_CTXT_USER(fi_context) = (void *)buf;
		PSMX_CTXT_EP(fi_context) = ep_priv;
	}

#if (PSM_VERNO_MAJOR >= 2)
	err = psm_mq_isend2(ep_priv->domain->psm_mq, psm_epaddr, 0,
				&psm_tag2, buf, len, (void*)fi_context, &psm_req);
#else
	err = psm_mq_isend(ep_priv->domain->psm_mq, psm_epaddr, 0,
				psm_tag, buf, len, (void*)fi_context, &psm_req);
#endif

	if (err != PSM_OK)
		return psmx_errno(err);

	if (fi_context == context)
		PSMX_CTXT_REQ(fi_context) = psm_req;

	return 0;
}

ssize_t psmx_tagged_send_no_flag_av_map(struct fid_ep *ep, const void *buf,
					size_t len, void *desc,
					fi_addr_t dest_addr, uint64_t tag,
				        void *context)
{
	struct psmx_fid_ep *ep_priv;
	psm_epaddr_t psm_epaddr;
	psm_mq_req_t psm_req;
	uint64_t psm_tag;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_tag_t psm_tag2;
#endif
	struct fi_context *fi_context;
	int err;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	psm_epaddr = (psm_epaddr_t) dest_addr;
	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
#if (PSM_VERNO_MAJOR >= 2)
	PSMX_SET_TAG(psm_tag2, psm_tag, 0);
#endif

	fi_context = context;
	PSMX_CTXT_TYPE(fi_context) = PSMX_TSEND_CONTEXT;
	PSMX_CTXT_USER(fi_context) = (void *)buf;
	PSMX_CTXT_EP(fi_context) = ep_priv;

#if (PSM_VERNO_MAJOR >= 2)
	err = psm_mq_isend2(ep_priv->domain->psm_mq, psm_epaddr, 0,
			    &psm_tag2, buf, len, (void*)fi_context, &psm_req);
#else
	err = psm_mq_isend(ep_priv->domain->psm_mq, psm_epaddr, 0,
			   psm_tag, buf, len, (void*)fi_context, &psm_req);
#endif

	if (err != PSM_OK)
		return psmx_errno(err);

	PSMX_CTXT_REQ(fi_context) = psm_req;
	return 0;
}

ssize_t psmx_tagged_send_no_flag_av_table(struct fid_ep *ep, const void *buf,
					  size_t len, void *desc,
					  fi_addr_t dest_addr, uint64_t tag,
					  void *context)
{
	struct psmx_fid_ep *ep_priv;
	struct psmx_fid_av *av;
	psm_epaddr_t psm_epaddr;
	psm_mq_req_t psm_req;
	uint64_t psm_tag;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_tag_t psm_tag2;
#endif
	struct fi_context *fi_context;
	int err;
	size_t idx;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	av = ep_priv->av;
	idx = (size_t)dest_addr;
	if (idx >= av->last)
		return -FI_EINVAL;

	psm_epaddr = av->psm_epaddrs[idx];
	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
#if (PSM_VERNO_MAJOR >= 2)
	PSMX_SET_TAG(psm_tag2, psm_tag, 0);
#endif

	fi_context = context;
	PSMX_CTXT_TYPE(fi_context) = PSMX_TSEND_CONTEXT;
	PSMX_CTXT_USER(fi_context) = (void *)buf;
	PSMX_CTXT_EP(fi_context) = ep_priv;

#if (PSM_VERNO_MAJOR >= 2)
	err = psm_mq_isend2(ep_priv->domain->psm_mq, psm_epaddr, 0,
			    &psm_tag2, buf, len, (void*)fi_context, &psm_req);
#else
	err = psm_mq_isend(ep_priv->domain->psm_mq, psm_epaddr, 0,
			   psm_tag, buf, len, (void*)fi_context, &psm_req);
#endif

	if (err != PSM_OK)
		return psmx_errno(err);

	PSMX_CTXT_REQ(fi_context) = psm_req;
	return 0;
}

ssize_t psmx_tagged_send_no_event_av_map(struct fid_ep *ep, const void *buf,
					 size_t len, void *desc,
					 fi_addr_t dest_addr, uint64_t tag,
				         void *context)
{
	struct psmx_fid_ep *ep_priv;
	psm_epaddr_t psm_epaddr;
	psm_mq_req_t psm_req;
	uint64_t psm_tag;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_tag_t psm_tag2;
#endif
	struct fi_context *fi_context;
	int err;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	psm_epaddr = (psm_epaddr_t) dest_addr;
	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
#if (PSM_VERNO_MAJOR >= 2)
	PSMX_SET_TAG(psm_tag2, psm_tag, 0);
#endif

	fi_context = &ep_priv->nocomp_send_context;

#if (PSM_VERNO_MAJOR >= 2)
	err = psm_mq_isend2(ep_priv->domain->psm_mq, psm_epaddr, 0,
			    &psm_tag2, buf, len, (void*)fi_context, &psm_req);
#else
	err = psm_mq_isend(ep_priv->domain->psm_mq, psm_epaddr, 0,
			   psm_tag, buf, len, (void*)fi_context, &psm_req);
#endif

	if (err != PSM_OK)
		return psmx_errno(err);

	return 0;
}

ssize_t psmx_tagged_send_no_event_av_table(struct fid_ep *ep, const void *buf,
					   size_t len, void *desc,
					   fi_addr_t dest_addr, uint64_t tag,
					   void *context)
{
	struct psmx_fid_ep *ep_priv;
	struct psmx_fid_av *av;
	psm_epaddr_t psm_epaddr;
	psm_mq_req_t psm_req;
	uint64_t psm_tag;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_tag_t psm_tag2;
#endif
	struct fi_context *fi_context;
	int err;
	size_t idx;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	av = ep_priv->av;
	idx = (size_t)dest_addr;
	if (idx >= av->last)
		return -FI_EINVAL;

	psm_epaddr = av->psm_epaddrs[idx];
	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
#if (PSM_VERNO_MAJOR >= 2)
	PSMX_SET_TAG(psm_tag2, psm_tag, 0);
#endif

	fi_context = &ep_priv->nocomp_send_context;

#if (PSM_VERNO_MAJOR >= 2)
	err = psm_mq_isend2(ep_priv->domain->psm_mq, psm_epaddr, 0,
			    &psm_tag2, buf, len, (void*)fi_context, &psm_req);
#else
	err = psm_mq_isend(ep_priv->domain->psm_mq, psm_epaddr, 0,
			   psm_tag, buf, len, (void*)fi_context, &psm_req);
#endif

	if (err != PSM_OK)
		return psmx_errno(err);

	return 0;
}

ssize_t psmx_tagged_inject_no_flag_av_map(struct fid_ep *ep, const void *buf, size_t len,
					  fi_addr_t dest_addr, uint64_t tag)
{
	struct psmx_fid_ep *ep_priv;
	psm_epaddr_t psm_epaddr;
	uint64_t psm_tag;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_tag_t psm_tag2;
#endif
	int err;

	if (len > PSMX_INJECT_SIZE)
		return -FI_EMSGSIZE;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	psm_epaddr = (psm_epaddr_t) dest_addr;
	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
#if (PSM_VERNO_MAJOR >= 2)
	PSMX_SET_TAG(psm_tag2, psm_tag, 0);
#endif

#if (PSM_VERNO_MAJOR >= 2)
	err = psm_mq_send2(ep_priv->domain->psm_mq, psm_epaddr, 0, &psm_tag2, buf, len);
#else
	err = psm_mq_send(ep_priv->domain->psm_mq, psm_epaddr, 0, psm_tag, buf, len);
#endif

	if (err != PSM_OK)
		return psmx_errno(err);

	if (ep_priv->send_cntr)
		psmx_cntr_inc(ep_priv->send_cntr);

	return 0;
}

ssize_t psmx_tagged_inject_no_flag_av_table(struct fid_ep *ep, const void *buf, size_t len,
					    fi_addr_t dest_addr, uint64_t tag)
{
	struct psmx_fid_ep *ep_priv;
	struct psmx_fid_av *av;
	psm_epaddr_t psm_epaddr;
	uint64_t psm_tag;
#if (PSM_VERNO_MAJOR >= 2)
	psm_mq_tag_t psm_tag2;
#endif
	int err;
	size_t idx;

	if (len > PSMX_INJECT_SIZE)
		return -FI_EMSGSIZE;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	av = ep_priv->av;
	idx = (size_t)dest_addr;
	if (idx >= av->last)
		return -FI_EINVAL;

	psm_epaddr = av->psm_epaddrs[idx];
	psm_tag = tag & (~ep_priv->domain->reserved_tag_bits);
#if (PSM_VERNO_MAJOR >= 2)
	PSMX_SET_TAG(psm_tag2, psm_tag, 0);
#endif

#if (PSM_VERNO_MAJOR >= 2)
	err = psm_mq_send2(ep_priv->domain->psm_mq, psm_epaddr, 0, &psm_tag2, buf, len);
#else
	err = psm_mq_send(ep_priv->domain->psm_mq, psm_epaddr, 0, psm_tag, buf, len);
#endif

	if (err != PSM_OK)
		return psmx_errno(err);

	if (ep_priv->send_cntr)
		psmx_cntr_inc(ep_priv->send_cntr);

	return 0;
}

static ssize_t psmx_tagged_send(struct fid_ep *ep, const void *buf, size_t len,
				void *desc, fi_addr_t dest_addr,
				uint64_t tag, void *context)
{
	struct psmx_fid_ep *ep_priv;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

#if (PSM_VERNO_MAJOR >= 2)
	return _psmx_tagged_send(ep, buf, len, desc, dest_addr, tag, context,
				 ep_priv->flags, 0);
#else
	return _psmx_tagged_send(ep, buf, len, desc, dest_addr, tag, context,
				 ep_priv->flags);
#endif
}

static ssize_t psmx_tagged_sendmsg(struct fid_ep *ep, const struct fi_msg_tagged *msg,
				   uint64_t flags)
{
	void *buf;
	size_t len;

	if (!msg || (msg->iov_count && !msg->msg_iov))
		return -FI_EINVAL;

	if (msg->iov_count > 1) {
		return -FI_EINVAL;
	}
	else if (msg->iov_count) {
		buf = msg->msg_iov[0].iov_base;
		len = msg->msg_iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

#if (PSM_VERNO_MAJOR >= 2)
	return _psmx_tagged_send(ep, buf, len,
				 msg->desc ? msg->desc[0] : NULL, msg->addr,
				 msg->tag, msg->context, flags, (uint32_t) msg->data);
#else
	return _psmx_tagged_send(ep, buf, len,
				 msg->desc ? msg->desc[0] : NULL, msg->addr,
				 msg->tag, msg->context, flags);
#endif
}

static ssize_t psmx_tagged_sendv(struct fid_ep *ep, const struct iovec *iov, void **desc,
				 size_t count, fi_addr_t dest_addr, uint64_t tag, void *context)
{
	void *buf;
	size_t len;

	if (count && !iov)
		return -FI_EINVAL;

	if (count > 1) {
		return -FI_EINVAL;
	}
	else if (count) {
		buf = iov[0].iov_base;
		len = iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

	return psmx_tagged_send(ep, buf, len,
				desc ? desc[0] : NULL, dest_addr, tag, context);
}

static ssize_t psmx_tagged_sendv_no_flag_av_map(struct fid_ep *ep, const struct iovec *iov,
						void **desc, size_t count,
						fi_addr_t dest_addr,
						uint64_t tag, void *context)
{
	void *buf;
	size_t len;

	if (count && !iov)
		return -FI_EINVAL;

	if (count > 1) {
		return -FI_EINVAL;
	}
	else if (count) {
		buf = iov[0].iov_base;
		len = iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

	return psmx_tagged_send_no_flag_av_map(ep, buf, len,
					       desc ? desc[0] : NULL, dest_addr,
					       tag, context);
}

static ssize_t psmx_tagged_sendv_no_flag_av_table(struct fid_ep *ep, const struct iovec *iov,
						  void **desc, size_t count,
						  fi_addr_t dest_addr,
						  uint64_t tag, void *context)
{
	void *buf;
	size_t len;

	if (count && !iov)
		return -FI_EINVAL;

	if (count > 1) {
		return -FI_EINVAL;
	}
	else if (count) {
		buf = iov[0].iov_base;
		len = iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

	return psmx_tagged_send_no_flag_av_table(ep, buf, len,
					         desc ? desc[0] : NULL, dest_addr,
					         tag, context);
}

static ssize_t psmx_tagged_sendv_no_event_av_map(struct fid_ep *ep, const struct iovec *iov,
						void **desc, size_t count,
						fi_addr_t dest_addr,
						uint64_t tag, void *context)
{
	void *buf;
	size_t len;

	if (count && !iov)
		return -FI_EINVAL;

	if (count > 1) {
		return -FI_EINVAL;
	}
	else if (count) {
		buf = iov[0].iov_base;
		len = iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

	return psmx_tagged_send_no_event_av_map(ep, buf, len,
					       desc ? desc[0] : NULL, dest_addr,
					       tag, context);
}

static ssize_t psmx_tagged_sendv_no_event_av_table(struct fid_ep *ep, const struct iovec *iov,
						  void **desc, size_t count,
						  fi_addr_t dest_addr,
						  uint64_t tag, void *context)
{
	void *buf;
	size_t len;

	if (count && !iov)
		return -FI_EINVAL;

	if (count > 1) {
		return -FI_EINVAL;
	}
	else if (count) {
		buf = iov[0].iov_base;
		len = iov[0].iov_len;
	}
	else {
		buf = NULL;
		len = 0;
	}

	return psmx_tagged_send_no_event_av_table(ep, buf, len,
					         desc ? desc[0] : NULL,
					         dest_addr, tag, context);
}

static ssize_t psmx_tagged_inject(struct fid_ep *ep, const void *buf, size_t len,
				  fi_addr_t dest_addr, uint64_t tag)
{
	struct psmx_fid_ep *ep_priv;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

#if (PSM_VERNO_MAJOR >= 2)
	return _psmx_tagged_send(ep, buf, len, NULL, dest_addr, tag, NULL,
				 ep_priv->flags | FI_INJECT | PSMX_NO_COMPLETION, 0);
#else
	return _psmx_tagged_send(ep, buf, len, NULL, dest_addr, tag, NULL,
				 ep_priv->flags | FI_INJECT | PSMX_NO_COMPLETION);
#endif
}

#if (PSM_VERNO_MAJOR >= 2)
static ssize_t psmx_tagged_senddata(struct fid_ep *ep, const void *buf, size_t len,
                                    void *desc, uint64_t data, fi_addr_t dest_addr,
                                    uint64_t tag, void *context)
{
	struct psmx_fid_ep *ep_priv;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	return _psmx_tagged_send(ep, buf, len, desc, dest_addr, tag, context,
				 ep_priv->flags,  (uint32_t)data);
}

static ssize_t psmx_tagged_injectdata(struct fid_ep *ep, const void *buf, size_t len,
				      uint64_t data, fi_addr_t dest_addr, uint64_t tag)
{
	struct psmx_fid_ep *ep_priv;

	ep_priv = container_of(ep, struct psmx_fid_ep, ep);

	return _psmx_tagged_send(ep, buf, len, NULL, dest_addr, tag, NULL,
				 ep_priv->flags | FI_INJECT | PSMX_NO_COMPLETION,
				 (uint32_t)data);
}
#endif

/* general case */
struct fi_ops_tagged psmx_tagged_ops = {
	.size = sizeof(struct fi_ops_tagged),
	.recv = psmx_tagged_recv,
	.recvv = psmx_tagged_recvv,
	.recvmsg = psmx_tagged_recvmsg,
	.send = psmx_tagged_send,
	.sendv = psmx_tagged_sendv,
	.sendmsg = psmx_tagged_sendmsg,
	.inject = psmx_tagged_inject,
#if (PSM_VERNO_MAJOR >= 2)
	.senddata = psmx_tagged_senddata,
	.injectdata = psmx_tagged_injectdata,
#else
	.senddata = fi_no_tagged_senddata,
	.injectdata = fi_no_tagged_injectdata,
#endif
};

/* op_flags=0, no event suppression, FI_AV_MAP */
struct fi_ops_tagged psmx_tagged_ops_no_flag_av_map = {
	.size = sizeof(struct fi_ops_tagged),
	.recv = psmx_tagged_recv_no_flag,
	.recvv = psmx_tagged_recvv_no_flag,
	.recvmsg = psmx_tagged_recvmsg,
	.send = psmx_tagged_send_no_flag_av_map,
	.sendv = psmx_tagged_sendv_no_flag_av_map,
	.sendmsg = psmx_tagged_sendmsg,
	.inject = psmx_tagged_inject_no_flag_av_map,
#if (PSM_VERNO_MAJOR >= 2)
	.senddata = psmx_tagged_senddata,
	.injectdata = psmx_tagged_injectdata,
#else
	.senddata = fi_no_tagged_senddata,
	.injectdata = fi_no_tagged_injectdata,
#endif
};

/* op_flags=0, no event suppression, FI_AV_TABLE */
struct fi_ops_tagged psmx_tagged_ops_no_flag_av_table = {
	.size = sizeof(struct fi_ops_tagged),
	.recv = psmx_tagged_recv_no_flag,
	.recvv = psmx_tagged_recvv_no_flag,
	.recvmsg = psmx_tagged_recvmsg,
	.send = psmx_tagged_send_no_flag_av_table,
	.sendv = psmx_tagged_sendv_no_flag_av_table,
	.sendmsg = psmx_tagged_sendmsg,
	.inject = psmx_tagged_inject_no_flag_av_table,
#if (PSM_VERNO_MAJOR >= 2)
	.senddata = psmx_tagged_senddata,
	.injectdata = psmx_tagged_injectdata,
#else
	.senddata = fi_no_tagged_senddata,
	.injectdata = fi_no_tagged_injectdata,
#endif
};

/* op_flags=0, event suppression, FI_AV_MAP */
struct fi_ops_tagged psmx_tagged_ops_no_event_av_map = {
	.size = sizeof(struct fi_ops_tagged),
	.recv = psmx_tagged_recv_no_event,
	.recvv = psmx_tagged_recvv_no_event,
	.recvmsg = psmx_tagged_recvmsg,
	.send = psmx_tagged_send_no_event_av_map,
	.sendv = psmx_tagged_sendv_no_event_av_map,
	.sendmsg = psmx_tagged_sendmsg,
	.inject = psmx_tagged_inject_no_flag_av_map,
#if (PSM_VERNO_MAJOR >= 2)
	.senddata = psmx_tagged_senddata,
	.injectdata = psmx_tagged_injectdata,
#else
	.senddata = fi_no_tagged_senddata,
	.injectdata = fi_no_tagged_injectdata,
#endif
};

/* op_flags=0, event suppression, FI_AV_TABLE */
struct fi_ops_tagged psmx_tagged_ops_no_event_av_table = {
	.size = sizeof(struct fi_ops_tagged),
	.recv = psmx_tagged_recv_no_event,
	.recvv = psmx_tagged_recvv_no_event,
	.recvmsg = psmx_tagged_recvmsg,
	.send = psmx_tagged_send_no_event_av_table,
	.sendv = psmx_tagged_sendv_no_event_av_table,
	.sendmsg = psmx_tagged_sendmsg,
	.inject = psmx_tagged_inject_no_flag_av_table,
#if (PSM_VERNO_MAJOR >= 2)
	.senddata = psmx_tagged_senddata,
	.injectdata = psmx_tagged_injectdata,
#else
	.senddata = fi_no_tagged_senddata,
	.injectdata = fi_no_tagged_injectdata,
#endif
};

/* op_flags=0, send event suppression, FI_AV_MAP */
struct fi_ops_tagged psmx_tagged_ops_no_send_event_av_map = {
	.size = sizeof(struct fi_ops_tagged),
	.recv = psmx_tagged_recv_no_flag,
	.recvv = psmx_tagged_recvv_no_flag,
	.recvmsg = psmx_tagged_recvmsg,
	.send = psmx_tagged_send_no_event_av_map,
	.sendv = psmx_tagged_sendv_no_event_av_map,
	.sendmsg = psmx_tagged_sendmsg,
	.inject = psmx_tagged_inject_no_flag_av_map,
#if (PSM_VERNO_MAJOR >= 2)
	.senddata = psmx_tagged_senddata,
	.injectdata = psmx_tagged_injectdata,
#else
	.senddata = fi_no_tagged_senddata,
	.injectdata = fi_no_tagged_injectdata,
#endif
};

/* op_flags=0, send event suppression, FI_AV_TABLE */
struct fi_ops_tagged psmx_tagged_ops_no_send_event_av_table = {
	.size = sizeof(struct fi_ops_tagged),
	.recv = psmx_tagged_recv_no_flag,
	.recvv = psmx_tagged_recvv_no_flag,
	.recvmsg = psmx_tagged_recvmsg,
	.send = psmx_tagged_send_no_event_av_table,
	.sendv = psmx_tagged_sendv_no_event_av_table,
	.sendmsg = psmx_tagged_sendmsg,
	.inject = psmx_tagged_inject_no_flag_av_table,
#if (PSM_VERNO_MAJOR >= 2)
	.senddata = psmx_tagged_senddata,
	.injectdata = psmx_tagged_injectdata,
#else
	.senddata = fi_no_tagged_senddata,
	.injectdata = fi_no_tagged_injectdata,
#endif
};

/* op_flags=0, recv event suppression, FI_AV_MAP */
struct fi_ops_tagged psmx_tagged_ops_no_recv_event_av_map = {
	.size = sizeof(struct fi_ops_tagged),
	.recv = psmx_tagged_recv_no_event,
	.recvv = psmx_tagged_recvv_no_event,
	.recvmsg = psmx_tagged_recvmsg,
	.send = psmx_tagged_send_no_flag_av_map,
	.sendv = psmx_tagged_sendv_no_flag_av_map,
	.sendmsg = psmx_tagged_sendmsg,
	.inject = psmx_tagged_inject_no_flag_av_map,
#if (PSM_VERNO_MAJOR >= 2)
	.senddata = psmx_tagged_senddata,
	.injectdata = psmx_tagged_injectdata,
#else
	.senddata = fi_no_tagged_senddata,
	.injectdata = fi_no_tagged_injectdata,
#endif
};

/* op_flags=0, recv event suppression, FI_AV_TABLE */
struct fi_ops_tagged psmx_tagged_ops_no_recv_event_av_table = {
	.size = sizeof(struct fi_ops_tagged),
	.recv = psmx_tagged_recv_no_event,
	.recvv = psmx_tagged_recvv_no_event,
	.recvmsg = psmx_tagged_recvmsg,
	.send = psmx_tagged_send_no_flag_av_table,
	.sendv = psmx_tagged_sendv_no_flag_av_table,
	.sendmsg = psmx_tagged_sendmsg,
	.inject = psmx_tagged_inject_no_flag_av_table,
#if (PSM_VERNO_MAJOR >= 2)
	.senddata = psmx_tagged_senddata,
	.injectdata = psmx_tagged_injectdata,
#else
	.senddata = fi_no_tagged_senddata,
	.injectdata = fi_no_tagged_injectdata,
#endif
};
