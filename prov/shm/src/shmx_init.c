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
#include "fi.h"
#include "prov.h"



static int shmx_getinfo(uint32_t version, const char *node, const char *service,
			uint64_t flags, struct fi_info *hints, struct fi_info **info)
{
	struct fi_info *shmx_info = NULL;
	void *dest_addr = NULL;
	int ep_type = FI_EP_RDM;
	int caps = 0;
	uint64_t max_tag_value = 0;
	int err = -ENODATA;

	SHMX_DEBUG("Enter %s\n", __func__);

	*info = NULL;

	if (hints) {
		if (hints->ep_attr) {
            switch (hints->ep_attr->type) {
            case FI_EP_UNSPEC:
            case FI_EP_RDM:
                break;
            default:
                SHMX_DEBUG("%s: hints->ep_attr->type=%d, supported=%d,%d.\n",
                            __func__, hints->ep_attr->type, FI_EP_UNSPEC, FI_EP_RDM);
                goto err_out;
            }


			switch (hints->ep_attr->protocol) {
			case FI_PROTO_UNSPEC:
				break;
			default:
                                SHMX_DEBUG("%s: hints->protocol=%d, supported=%d\n",
                            __func__, hints->ep_attr->protocol,
                            FI_PROTO_UNSPEC);
				goto err_out;
			}
		}

		if ((hints->caps & SHMX_CAPS) != hints->caps) {
                        SHMX_DEBUG("%s: hints->caps=0x%llx, supported=0x%llx\n",
                        __func__, hints->caps, SHMX_CAPS);
			goto err_out;
		}

		if (hints->tx_attr &&
		    (hints->tx_attr->op_flags & SHMX_OP_FLAGS) !=
		     hints->tx_attr->op_flags) {
                        SHMX_DEBUG("%s: hints->tx->flags=0x%llx, supported=0x%llx\n",
                        __func__, hints->tx_attr->op_flags, SHMX_OP_FLAGS);
			goto err_out;
		}

		if (hints->rx_attr &&
		    (hints->rx_attr->op_flags & SHMX_OP_FLAGS) !=
		     hints->rx_attr->op_flags) {
                        SHMX_DEBUG("%s: hints->rx->flags=0x%llx, supported=0x%llx\n",
                        __func__, hints->rx_attr->op_flags, SHMX_OP_FLAGS);
			goto err_out;
		}

		if ((hints->mode & SHMX_MODE) != SHMX_MODE) {
                        SHMX_DEBUG("%s: hints->mode=0x%llx, required=0x%llx\n",
                        __func__, hints->mode, SHMX_MODE);
			goto err_out;
		}

		if (hints->fabric_attr && hints->fabric_attr->name &&
		    strncmp(hints->fabric_attr->name, SHMX_FABRIC_NAME, SHMX_FABRIC_NAME_LEN)) {
                        SHMX_DEBUG("%s: hints->fabric_name=%s, supported=psm\n",
                        __func__, hints->fabric_attr->name);
			goto err_out;
		}

		if (hints->domain_attr && hints->domain_attr->name &&
		    strncmp(hints->domain_attr->name, SHMX_DOMAIN_NAME, SHMX_DOMAIN_NAME_LEN)) {
                        SHMX_DEBUG("%s: hints->domain_name=%s, supported=psm\n",
                        __func__, hints->domain_attr->name);
			goto err_out;
		}

		if (hints->ep_attr) {
			if (hints->ep_attr->max_msg_size > SHMX_MAX_MSG_SIZE) {
                                SHMX_DEBUG("%s: hints->ep_attr->max_msg_size=%ld,"
                            "supported=%ld.\n", __func__,
                            hints->ep_attr->max_msg_size,
                            SHMX_MAX_MSG_SIZE);
				goto err_out;
			}

			max_tag_value = fi_tag_bits(hints->ep_attr->mem_tag_format);
		}

		caps = hints->caps;

		/* TODO: check other fields of hints */
	}

	shmx_info = fi_allocinfo();

	if (!shmx_info) {
		err = -ENOMEM;
		goto err_out;
	}

	shmx_info->tx_attr->op_flags = (hints && hints->tx_attr && hints->tx_attr->op_flags)
					? hints->tx_attr->op_flags : 0;
	shmx_info->rx_attr->op_flags = (hints && hints->rx_attr && hints->tx_attr->op_flags)
					? hints->tx_attr->op_flags : 0;

	shmx_info->ep_attr->protocol            = FI_PROTO_UNSPEC;
	shmx_info->ep_attr->max_msg_size        = SHMX_MAX_MSG_SIZE;
	shmx_info->ep_attr->mem_tag_format      = fi_tag_format(max_tag_value);

	shmx_info->domain_attr->threading        = FI_THREAD_COMPLETION;
	shmx_info->domain_attr->control_progress = FI_PROGRESS_MANUAL;
	shmx_info->domain_attr->data_progress    = FI_PROGRESS_MANUAL;
	shmx_info->domain_attr->name             = strdup(SHMX_DOMAIN_NAME);

	shmx_info->next    = NULL;
	shmx_info->ep_attr->type = ep_type;
	shmx_info->caps    = (hints && hints->caps) ? hints->caps : caps;
	shmx_info->mode    = SHMX_MODE;
	shmx_info->addr_format  = FI_FORMAT_UNSPEC;
	shmx_info->src_addrlen  = 0;
        shmx_info->dest_addrlen = fi_shm_addrlen();
	shmx_info->src_addr     = NULL;
	shmx_info->dest_addr    = dest_addr;
	shmx_info->fabric_attr->name = strdup(SHMX_FABRIC_NAME);
	shmx_info->fabric_attr->prov_name = strdup(SHMX_PROV_NAME);

    shmx_info->tx_attr->caps = shmx_info->caps;
	shmx_info->tx_attr->mode = shmx_info->mode;
	shmx_info->tx_attr->op_flags = (hints && hints->tx_attr && hints->tx_attr->op_flags)
					? hints->tx_attr->op_flags : 0;
	shmx_info->tx_attr->msg_order = FI_ORDER_SAS;
	shmx_info->tx_attr->comp_order = FI_ORDER_NONE;
	shmx_info->tx_attr->inject_size = SHMX_INJECT_SIZE;
	shmx_info->tx_attr->size = UINT64_MAX;
	shmx_info->tx_attr->iov_limit = 1;

	shmx_info->rx_attr->caps = shmx_info->caps;
	shmx_info->rx_attr->mode = shmx_info->mode;
	shmx_info->rx_attr->op_flags = (hints && hints->rx_attr && hints->tx_attr->op_flags)
					? hints->tx_attr->op_flags : 0;
	shmx_info->rx_attr->msg_order = FI_ORDER_SAS;
	shmx_info->rx_attr->comp_order = FI_ORDER_NONE;
	shmx_info->rx_attr->total_buffered_recv = ~(0ULL);
	shmx_info->rx_attr->size = UINT64_MAX;
	shmx_info->rx_attr->iov_limit = 1;

	*info = shmx_info;

	return 0;

err_out:
	return err;
}

static int shmx_fabric_close(fid_t fid)
{
	free(fid);
	return 0;
}

static struct fi_ops shmx_fabric_fi_ops = {
	.size  = sizeof(struct fi_ops),
	.close = shmx_fabric_close,
};

static struct fi_ops_fabric shmx_fabric_ops = {
	.size = sizeof(struct fi_ops_fabric),
	.domain = shmx_domain_open,
};

static int shmx_fabric(struct fi_fabric_attr *attr,
		       struct fid_fabric **fabric, void *context)
{
	struct shmx_fid_fabric *fabric_priv;

    if (strncmp(attr->name, SHMX_FABRIC_NAME, SHMX_FABRIC_NAME_LEN))
		return -FI_ENODATA;

	fabric_priv = calloc(1, sizeof(*fabric_priv));

	if (!fabric_priv)
		return -FI_ENOMEM;

	fabric_priv->fabric.fid.fclass  = FI_CLASS_FABRIC;
	fabric_priv->fabric.fid.context = context;
	fabric_priv->fabric.fid.ops     = &shmx_fabric_fi_ops;
	fabric_priv->fabric.ops         = &shmx_fabric_ops;

	*fabric = &fabric_priv->fabric;

	return 0;
}

static void shmx_fini(void)
{
    return;
}

struct fi_provider shmx_prov = {
	.name = SHMX_PROV_NAME,
	.version = FI_VERSION(0, 9),
	.fi_version = FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION),
	.getinfo = shmx_getinfo,
	.fabric = shmx_fabric,
	.cleanup = shmx_fini
};

SHM_INI
{
	FI_INFO(&shmx_prov, FI_LOG_CORE, "\n");

	fi_log_init();

	return (&shmx_prov);
}

