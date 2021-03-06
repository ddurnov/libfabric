/*
 * Copyright (c) 2013-2015 Intel Corporation, Inc.  All rights reserved.
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

#ifndef _VERBS_TAGGED_EP_RDM_STATES_H
#define _VERBS_TAGGED_EP_RDM_STATES_H

#include <stddef.h>
#include <stdint.h>

struct fi_ibv_rdm_tagged_request;

enum fi_ibv_rdm_tagged_request_eager_state {
	FI_IBV_STATE_EAGER_BEGIN = 0,      // must be 0

	FI_IBV_STATE_EAGER_SEND_POSTPONED,
	FI_IBV_STATE_EAGER_SEND_WAIT4LC,   // wait for local completion
	FI_IBV_STATE_EAGER_SEND_END,

	FI_IBV_STATE_EAGER_RECV_BEGIN,
	FI_IBV_STATE_EAGER_RECV_WAIT4PKT,
	FI_IBV_STATE_EAGER_RECV_WAIT4RECV,
	FI_IBV_STATE_EAGER_RECV_END,

	FI_IBV_STATE_EAGER_RMA_WAIT4LC,
	FI_IBV_STATE_EAGER_RMA_END,

	FI_IBV_STATE_EAGER_READY_TO_FREE,

	FI_IBV_STATE_EAGER_COUNT           // must be last
};

char *fi_ibv_rdm_tagged_req_eager_state_to_str(
		enum fi_ibv_rdm_tagged_request_eager_state state);

enum fi_ibv_rdm_tagged_request_rndv_state {
	FI_IBV_STATE_RNDV_NOT_USED = 0,    // must be 0
	FI_IBV_STATE_RNDV_SEND_BEGIN,
	//    FI_IBV_STATE_RNDV_SEND_WAIT4CTS, // not implemented yet
	FI_IBV_STATE_RNDV_SEND_WAIT4SEND,
	FI_IBV_STATE_RNDV_SEND_WAIT4ACK,
	FI_IBV_STATE_RNDV_SEND_END,

	FI_IBV_STATE_RNDV_RECV_BEGIN,
	FI_IBV_STATE_RNDV_RECV_WAIT4RES,
	FI_IBV_STATE_RNDV_RECV_WAIT4RECV,
	FI_IBV_STATE_RNDV_RECV_WAIT4LC,
	FI_IBV_STATE_RNDV_RECV_END,

	FI_IBV_STATE_RNDV_COUNT            // must be last
};

char *fi_ibv_rdm_tagged_req_rndv_state_to_str(
		enum fi_ibv_rdm_tagged_request_rndv_state state);

enum fi_ibv_rdm_tagged_request_event {
	FI_IBV_EVENT_SEND_START = 0,
	FI_IBV_EVENT_SEND_READY,
	FI_IBV_EVENT_SEND_COMPLETED,
	FI_IBV_EVENT_SEND_GOT_CTS,
	FI_IBV_EVENT_SEND_GOT_LC,

	FI_IBV_EVENT_RECV_START,
	FI_IBV_EVENT_RECV_GOT_PKT_PREPROCESS,
	FI_IBV_EVENT_RECV_GOT_PKT_PROCESS,
	FI_IBV_EVENT_RECV_GOT_ACK,

	FI_IBV_EVENT_RMA_START,

	FI_IBV_EVENT_COUNT                 // must be last
};

char *fi_ibv_rdm_tagged_req_event_to_str(
		enum fi_ibv_rdm_tagged_request_event event);

// Send service data types

enum ibv_rdm_send_type
{
	IBV_RDM_SEND_TYPE_UND = 0,
	IBV_RDM_SEND_TYPE_GEN,
	IBV_RDM_SEND_TYPE_INJ,
	IBV_RDM_SEND_TYPE_VEC
};

struct fi_ibv_rdm_tagged_send_start_data {
	struct fi_ibv_rdm_ep *ep_rdm;
	struct fi_ibv_rdm_tagged_conn *conn;
	void *context;
	size_t tag;
	size_t data_len;
	union {
		void *src_addr;
		struct iovec* iovec_arr;
	} buf;
	int iov_count;
	unsigned int imm;
	enum ibv_rdm_send_type stype;
};

struct fi_ibv_rdm_tagged_send_ready_data {
	struct fi_ibv_rdm_ep *ep;
};

struct fi_ibv_rdm_tagged_send_completed_data {
	struct fi_ibv_rdm_ep *ep;
};

// Recv service data types

struct fi_ibv_rdm_tagged_recv_start_data {
	size_t tag;
	size_t tagmask;
	struct fi_context *context;
	void *dest_addr;
	struct fi_ibv_rdm_tagged_conn *conn;
	struct fi_ibv_rdm_ep *ep;
	size_t data_len;
};

struct fi_ibv_recv_got_pkt_preprocess_data {
	struct fi_ibv_rdm_tagged_conn *conn;
	struct fi_ibv_rdm_ep *ep;
	void *rbuf;
	size_t arrived_len;
	uint64_t pkt_type;
	int imm_data;
};

struct fi_ibv_recv_got_pkt_process_data {
	struct fi_ibv_rdm_ep *ep;
} ;


struct fi_ibv_rdm_rma_start_data {
	struct fi_ibv_rdm_ep *ep_rdm;
	struct fi_ibv_rdm_tagged_conn *conn;
	void *context;
	size_t data_len;
	uint64_t rbuf;
	uintptr_t lbuf;
	uint32_t rkey;
	uint32_t lkey;
	enum ibv_wr_opcode op_code;
};


// Return codes

enum fi_rdm_tagged_req_hndl_ret {
    FI_EP_RDM_HNDL_SUCCESS = 0,
    FI_EP_RDM_HNDL_DELETED_REQUEST = 1,
    FI_EP_RDM_HNDL_ERROR = 2,
    FI_EP_RDM_HNDL_NOT_INIT = (int)-1
};

// Interfaces

enum fi_rdm_tagged_req_hndl_ret fi_ibv_rdm_tagged_req_hndls_init();
enum fi_rdm_tagged_req_hndl_ret fi_ibv_rdm_tagged_req_hndls_clean();
enum fi_rdm_tagged_req_hndl_ret
fi_ibv_rdm_tagged_req_hndl(struct fi_ibv_rdm_tagged_request *request,
			   enum fi_ibv_rdm_tagged_request_event event,
			   void *data);

#endif /* _VERBS_TAGGED_EP_RDM_STATES_H */
