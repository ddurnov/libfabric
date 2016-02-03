#ifndef _FI_SHM_TAGGED_H
#define _FI_SHM_TAGGED_H
#include <rdma/fi_errno.h>
#include "fi_shm_utils.h"

#define FI_SHM_TAGGED_SEND(conenction, send_buffer_index, user_buf, total_len, left_len, msg_tag) \
        {                                                               \
                (connection)->send_ptr->buffers[(send_buffer_index)].header.tag = (msg_tag); \
                size_t size_to_send = ((left_len) > FI_SHM_TRANSPORT_BUFFER_SIZE) ? \
                        FI_SHM_TRANSPORT_BUFFER_SIZE : (left_len);      \
                if ((total_len) == (left_len))                          \
                {                                                       \
                        /* Very first fragment contains total size */   \
                        (connection)->send_ptr->buffers[(send_buffer_index)].header.size = FI_SHM_SET_TOTAL_SIZE((total_len)); \
                }                                                       \
                else                                                    \
                {                                                       \
                        (connection)->send_ptr->buffers[(send_buffer_index)].header.size = size_to_send; \
                }                                                       \
                fi_shm_transport_buffer_memcpy((connection)->send_ptr->buffers[(send_buffer_index)].payload, \
                                               (user_buf) + ((total_len) - (left_len)), \
                                               size_to_send);           \
                (connection)->send_ptr->buffers[(send_buffer_index)].header.is_busy = 1; \
                (connection)->last_send_index = ((send_buffer_index) + 1) % FI_SHM_TRANSPORT_BUFFER_FIFO_SIZE; \
                (left_len) -= size_to_send;                             \
        }

#define FI_SHM_TAGGED_RECV(conenction, recv_buffer_index, user_buf, total_len, left_len, msg_tag, recv_overflow_handling_label) \
        {                                                               \
                size_t size_to_recv = 0;                                \
                if ((left_len) == (total_len))                          \
                {                                                       \
                        /* Very first fragment contains total size */   \
                        size_to_recv = FI_SHM_GET_TOTAL_SIZE((connection)->recv_ptr->buffers[(recv_buffer_index)].header.size); \
                        if (size_to_recv > (total_len))                 \
                        {                                               \
                                FI_SHM_DEBUG(("%s: Recv overflow: size_to_recv (%llu) > total_len (%llu)\n", __func__, size_to_recv, (total_len))); \
                                goto recv_overflow_handling_label;      \
                        }                                               \
                }                                                       \
                size_to_recv = ((left_len) > FI_SHM_TRANSPORT_BUFFER_SIZE) ? \
                        FI_SHM_TRANSPORT_BUFFER_SIZE : (left_len);      \
                fi_shm_transport_buffer_memcpy((user_buf) + ((total_len) - (left_len)), \
                                               (connection)->recv_ptr->buffers[(recv_buffer_index)].payload, \
                                               size_to_recv);           \
                (msg_tag) = (connection)->recv_ptr->buffers[(recv_buffer_index)].header.tag; /* Overwrite tag by value from header */ \
                (connection)->recv_ptr->buffers[(recv_buffer_index)].header.is_busy = 0; /* Free buffer */ \
                (connection)->last_recv_index = ((recv_buffer_index) + 1) % FI_SHM_TRANSPORT_BUFFER_FIFO_SIZE; \
                (left_len) -= size_to_recv;                             \
        }


static inline
int fi_shm_tagged_recv(struct shm_ep *ep, void *buf, size_t len,
                       fi_addr_t fi_addr, uint64_t tag,
                       uint64_t ignore, void *context)
{
        int recv_index;
        size_t left_to_recv = len;
        size_t size_to_recv = 0;
        int max_busy_wait_count = fi_shm_env.busy_wait_count;
        int busy_wait_count     = max_busy_wait_count;
        fi_shm_request_t * request_iterator = NULL;
        fi_shm_cq_event_t * event = NULL;
        fi_shm_connection_t *connection =
                (fi_shm_connection_t *) fi_addr;
        FI_SHM_DEBUG(("%s: receive from %d via %d/%d. connection = %p\n",
                      __func__,
                      (connection == FI_ADDR_UNSPEC) ? -1 : connection->connBlockId,
                      (connection == FI_ADDR_UNSPEC) ? -1 : connection->sendBlockId,
                      (connection == FI_ADDR_UNSPEC) ? -1 : connection->recvBlockId,
                      connection));

        FI_SHM_DEBUG(("%s: ep=%p, [buf=%p, len=%llu, src_addr=%p, tag=%llx, ignore=%llx, context=%p]\n",
                      __func__, ep, buf, len, connection, tag, ignore, context));

        TAILQ_FOREACH(request_iterator, &(ep->unexpected_recv_request_queue), requests)
        {
                if (connection != (void *)FI_ADDR_UNSPEC)
                {
                        if (request_iterator->connection != connection)
                        {
                                continue;
                        }
                }

                if (!((request_iterator->tag ^ tag) & (~ignore)))
                {
                        /* Found matching recv in unexpected recv queue */

                        FI_SHM_DEBUG(("%s: TAG matched in unexpected queue: queue.tag %llx\n",
				      __func__, request_iterator->tag));

                        if (request_iterator->len > len)
                        {
                                FI_SHM_DEBUG(("%s: Recv overflow! %llu > expected %llu\n",
					      __func__, request_iterator->len, len));

                                goto fn_recv_overflow;
                        }
                        else if (request_iterator->len < len)
                        {
                                FI_SHM_DEBUG(("%s: Recv truncated! %llu < expected %llu\n",
					      __func__, request_iterator->len, len));
                        }

                        request_iterator->buf     = buf;
                        request_iterator->context = context;

                        /* Copy currently available data from unexpected bufer to user bufer */

                        fi_shm_transport_buffer_memcpy(request_iterator->buf,
                                                       request_iterator->ue_buf,
                                                       request_iterator->len - request_iterator->left_len);

                        /* Release unexpected bufer */

                        free(request_iterator->ue_buf);

                        request_iterator->ue_buf = NULL;

                        /* Remove request from unexpected queue */

                        TAILQ_REMOVE(&(ep->unexpected_recv_request_queue), request_iterator, requests);

                        if (request_iterator->left_len == 0)
                        {
                                /* Request completed */

                                FI_SHM_DEBUG(("%s: Recv completed via unexpected match\n", __func__));

                                /* Create competion event from completed request and enqueue it */

                                MPOOL_ALLOC(ep->recv_cq->events_free_list,
                                            fi_shm_cq_event_t,
                                            event);

                                event->cqe.tagged.op_context = request_iterator->context;
                                event->cqe.tagged.buf        = request_iterator->buf;
                                event->cqe.tagged.len        = request_iterator->len;
                                event->cqe.tagged.tag        = tag;

                                event->src_addr = (fi_addr_t)connection;

                                fi_shm_cq_enqueue_event(ep->recv_cq, event);

                                /* Release request */

                                MPOOL_RETURN(ep->request_free_list,
                                             fi_shm_request_t,
                                             request_iterator);
                        }
                        else
                        {
                                /* Move request from unexpected to posted recv for further progression */

                                TAILQ_INSERT_TAIL(&(ep->posted_recv_request_queue),
                                                  request_iterator,
                                                  requests);
                        }

                        return 0;
                }
        }

        if (connection == (fi_shm_connection_t *)FI_ADDR_UNSPEC)
        {
                FI_SHM_DEBUG(("%s: FI_ADDR_UNSPEC. Enqueue the recv request\n", __func__));

                goto fn_enqueue;
        }

        if ((connection->recv_ptr == NULL) && (connection != FI_SHM_SELF_ADDR))
        {
                /* Connection hasn't been completely established. Enqueue the recv request */

                FI_SHM_DEBUG(("%s: Connection hasn't been completely established. Enqueue the recv request\n",
			      __func__));

                (void)fi_shm_ep_handle_incoming_connections(ep);

                goto fn_enqueue;
        }

        /* Try to handle recv immediately */

        do
        {
                recv_index = connection->last_recv_index;

                busy_wait_count = max_busy_wait_count;

                while ((!connection->recv_ptr->buffers[recv_index].header.is_busy) &&
		       (--busy_wait_count));

                if (!busy_wait_count)
                {
                        FI_SHM_DEBUG(("%s: Buffer is empty. Enqueue the recv request\n", __func__));

                        goto fn_enqueue;
                }

                if ((connection->recv_ptr->buffers[recv_index].header.tag ^ tag) & (~ignore))
                {
                        FI_SHM_DEBUG(("%s: Unexpected message!\n", __func__));

                        goto fn_enqueue;
                }

                FI_SHM_DEBUG(("%s: TAG matched immediately: tag %llx, ignore %llx, header.tag %llx\n",
			      __func__,
                              tag,
                              ignore,
                              connection->recv_ptr->buffers[recv_index].header.tag));

                size_to_recv = connection->recv_ptr->buffers[recv_index].header.size;

                if (FI_SHM_IS_FIRST_FRAGMENT(size_to_recv))
                {
                        if (FI_SHM_GET_TOTAL_SIZE(size_to_recv) <= len)
                        {
                                len = FI_SHM_GET_TOTAL_SIZE(size_to_recv);
                                left_to_recv = len;
                        }
                        else
                        {
                                FI_SHM_DEBUG(("%s: Recv overflow: received (%llu) > expected (%llu)", __func__,
                                              FI_SHM_GET_TOTAL_SIZE(size_to_recv), len));
                                exit(-1);
                        }
                }

                FI_SHM_TAGGED_RECV(connection, recv_index, buf, len, left_to_recv, tag, fn_recv_overflow);

                FI_SHM_DEBUG(("%s: Received %llu bytes, left %llu bytes\n", __func__, len - left_to_recv, left_to_recv));

        } while (left_to_recv);

        /* Request completed */

        /* Create competion event from immediately completed operation and enqueue it */

        MPOOL_ALLOC(ep->recv_cq->events_free_list,
                    fi_shm_cq_event_t,
                    event);

        event->cqe.tagged.op_context = context;
        event->cqe.tagged.buf        = buf;
        event->cqe.tagged.len        = len;
        event->cqe.tagged.tag        = tag;

        event->src_addr = (fi_addr_t)connection;

        FI_SHM_DEBUG(("%s: Recv completed immediately. Event: %p, %p, %llu, %llx\n",
		      __func__,
                      event->cqe.tagged.op_context,
                      event->cqe.tagged.buf,
                      event->cqe.tagged.len,
                      event->cqe.tagged.tag));

        fi_shm_cq_enqueue_event(ep->recv_cq, event);

fn_exit:

        return 0;

fn_enqueue:

        MPOOL_ALLOC(ep->request_free_list,
                    fi_shm_request_t,
                    request_iterator);

        request_iterator->type = FI_SHM_REQUEST_TAGGED_RECV;

        request_iterator->buf     = buf;
        request_iterator->len     = len;
        request_iterator->tag     = tag;
        request_iterator->ignore  = ignore;
        request_iterator->context = context;

        request_iterator->connection = connection;

        request_iterator->left_len = left_to_recv;

        TAILQ_INSERT_TAIL(&(ep->posted_recv_request_queue),
                          request_iterator,
                          requests);

        goto fn_exit;

fn_recv_overflow:

        fi_shm_debug_dump_ep(ep); /* DEBUG */
        FI_SHM_DEBUG(("%s: Emergency exit in 10 seconds\n", __func__));

        sleep(10);

        exit(-1); /* DEBUG */

        goto fn_exit;

}

static inline int
fi_shm_tagged_inject(struct shm_ep * ep,
		     const void * buf,
		     size_t len,
		     fi_addr_t fi_addr,
		     uint64_t tag)
{
        int send_index;

        fi_shm_connection_t *connection = (fi_shm_connection_t *)fi_addr;

        FI_SHM_DEBUG(("%s: inject to %d via %d/%d. connection = %p\n",
                      __func__,
		      connection->connBlockId,
		      connection->sendBlockId, connection->recvBlockId,
		      connection));

        FI_SHM_DEBUG(("%s: ep=%p, [buf=%p, len=%llu, dest_addr=%p, tag=%llx]\n",
                      __func__, ep, buf, len, connection, tag));

	size_t left_to_send = len;

	if (left_to_send <= FI_SHM_TRANSPORT_BUFFER_SIZE)
	{
		if (TAILQ_EMPTY(&(connection->postponed_send_request_queue)))
		{
			int max_busy_wait_count = fi_shm_env.busy_wait_count;
			int busy_wait_count     = max_busy_wait_count;

			send_index = connection->last_send_index;

			busy_wait_count = max_busy_wait_count;

			while ((connection->send_ptr->buffers[send_index].header.is_busy) &&
			       (--busy_wait_count));

			if (!busy_wait_count)
			{
				FI_SHM_DEBUG(("%s: Buffer is full\n", __func__));

				goto poll_all_and_eagain;
			}

			FI_SHM_TAGGED_SEND(connection,
					   send_index,
					   buf, len, left_to_send, tag);

			FI_SHM_DEBUG(("%s: Injected %llu bytes, tag %llx\n",
				      __func__, len, tag));

		}
		else
		{
			FI_SHM_DEBUG(("%s: Queue is busy\n", __func__));

			goto poll_all_and_eagain;
		}
	}
	else
	{
		return -FI_EINVAL;
	}

	return 0;

 poll_all_and_eagain:

	fi_shm_cq_poll_all(ep->send_cq, NULL, 1, NULL);

	if (ep->send_cq != ep->recv_cq)
	{
		fi_shm_cq_poll_all(ep->recv_cq, NULL, 1, NULL);
	}

	return -FI_EAGAIN;
}

static inline int
fi_shm_tagged_send(struct shm_ep * ep,
                   const void * buf,
                   size_t len,
                   fi_addr_t fi_addr,
                   uint64_t tag,
                   void *context)
{
        int send_index;

        fi_shm_connection_t *connection = (fi_shm_connection_t*)fi_addr;

        FI_SHM_DEBUG(("%s: send to %d via %d/%d. connection = %p\n",
                      __func__,
		      connection->connBlockId,
		      connection->sendBlockId, connection->recvBlockId,
		      connection));

        FI_SHM_DEBUG(("%s: ep=%p, [buf=%p, len=%llu, dest_addr=%p, tag=%llx, context=%p]\n",
                      __func__, ep, buf, len, connection, tag, context));

        size_t left_to_send = len;

        if (!TAILQ_EMPTY(&(connection->postponed_send_request_queue)))
        {
                FI_SHM_DEBUG(("%s: Queue is busy. Enqueue the send request\n",
			      __func__));
                goto fn_enqueue;
        }

        int max_busy_wait_count = fi_shm_env.busy_wait_count;
        int busy_wait_count     = max_busy_wait_count;

        fi_shm_request_t * request_iterator = NULL;

        fi_shm_cq_event_t * event = NULL;

        do
        {
                send_index = connection->last_send_index;

                busy_wait_count = max_busy_wait_count;

                while ((connection->send_ptr->buffers[send_index].header.is_busy) &&
		       (--busy_wait_count));

                if (!busy_wait_count)
                {
                        FI_SHM_DEBUG(("%s: Buffer is full. Enqueue the send request\n", __func__));

                        goto fn_enqueue;
                }

                FI_SHM_TAGGED_SEND(connection, send_index, buf, len, left_to_send, tag);

                FI_SHM_DEBUG(("%s: Sent %llu bytes, left %llu bytes, tag %llx\n",
			      __func__, len - left_to_send, left_to_send, tag));

        } while (left_to_send);

        /* Request completed immediately. Create event and enqueue it */

        MPOOL_ALLOC(ep->send_cq->events_free_list,
                    fi_shm_cq_event_t,
                    event);
        event->cqe.tagged.op_context = context;
        event->cqe.tagged.buf        = (void *)buf;
        event->cqe.tagged.len        = len;
        event->cqe.tagged.tag        = tag;

        event->src_addr = (fi_addr_t)connection;

        fi_shm_cq_enqueue_event(ep->send_cq, event);

        FI_SHM_DEBUG(("%s: Send completed immediately: %p, %p, %llu, %llx\n",
		      __func__, context, (void *)buf, len, tag));

fn_exit:

        return 0;

fn_enqueue:

        MPOOL_ALLOC(ep->request_free_list,
                    fi_shm_request_t,
                    request_iterator);

        request_iterator->type = FI_SHM_REQUEST_TAGGED_SEND;

        request_iterator->buf     = (void *)buf;
        request_iterator->len     = len;
        request_iterator->tag     = tag;
        request_iterator->context = context;

        request_iterator->left_len = left_to_send;

        TAILQ_INSERT_TAIL(&(connection->postponed_send_request_queue),
                          request_iterator,
                          requests);

        goto fn_exit;
}

static inline
int fi_shm_cancel(struct shm_ep *ep, void *context, fi_shm_req_queue_t *queue) {
        fi_shm_request_t * iter = NULL;
        /* TODO the first check is not necessary ?? */
        if (!TAILQ_EMPTY(queue))
        {
                TAILQ_FOREACH(iter, queue, requests)
                {
                        if (iter->context == context)
                                break;
                }
                if (iter) {
                        TAILQ_REMOVE(queue, iter, requests);
                        return 0;
                }
        }
        return -FI_ENOMSG;
}

static inline
int fi_shm_cancel_recv(struct shm_ep *ep, void *context) {
        return fi_shm_cancel(ep, context, &(ep->posted_recv_request_queue));
}
static inline
int fi_shm_cancel_send(struct shm_ep *ep, void *context) {
        fi_shm_connection_t * conn = NULL;
        SLIST_FOREACH(conn, &(ep->total_connection_list), total_connections) {
                if (0 == fi_shm_cancel(ep, context, &(conn->postponed_send_request_queue))) {
                        return 0;
                }
        }
        return -FI_ENOMSG;
}
#endif /*_FI_SHM_TAGGED_H*/
