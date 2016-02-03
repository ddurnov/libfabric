#include "fi_shm_utils.h"
#include <assert.h>
int fi_shm_cq_init(struct fi_shm_cq *cq, int cq_format, int cq_size)
{
        cq->format = cq_format;
        mpool_init(&cq->events_free_list, sizeof(struct fi_shm_cq_event), 32*2);
        cq->event_queue.head = cq->event_queue.tail = NULL;
        switch (cq_format) {
        case FI_CQ_FORMAT_UNSPEC:
                cq->entry_size = sizeof(struct fi_cq_tagged_entry);
		break;

	case FI_CQ_FORMAT_CONTEXT:
                cq->entry_size = sizeof(struct fi_cq_entry);
		break;

	case FI_CQ_FORMAT_TAGGED:
                cq->entry_size = sizeof(struct fi_cq_tagged_entry);
		break;
        default:
		FI_SHM_DEBUG(("%s: attr->format=%d, supported=%d...%d\n", __func__, cq_format,
                              FI_CQ_FORMAT_UNSPEC, FI_CQ_FORMAT_TAGGED));
		return -FI_EINVAL;

        }
        return 0;
}
int fi_shm_cq_fini(struct fi_shm_cq *cq)
{
        free(cq->events_free_list);
        return 0;
}
int fi_shm_ep_bind_cq(struct shm_ep *ep, struct fi_shm_cq *cq, uint64_t flags)
{
        if (flags & FI_SEND) {
                ep->send_cq = cq;
                cq->ep = ep;
                if (flags & FI_EVENT)
                        ep->send_cq_event_flag = 1;
        }
        if (flags & FI_RECV) {
                ep->recv_cq = cq;
                cq->ep = ep;
                if (flags & FI_EVENT)
                        ep->recv_cq_event_flag = 1;
        }
        return 0;
}


#define FI_SHM_CQ_EMPTY(cq) (!cq->event_queue.head)

void fi_shm_cq_enqueue_event(struct fi_shm_cq *cq, struct fi_shm_cq_event *event)
{
	struct fi_shm_cq_event_queue *ceq = &cq->event_queue;
        event->next = NULL;
	if (ceq->tail) {
		ceq->tail->next = event;
		ceq->tail = event;
	}
	else {
		ceq->head = ceq->tail = event;
	}
	ceq->count++;
}

static struct fi_shm_cq_event *fi_shm_cq_dequeue_event(struct fi_shm_cq *cq)
{
	struct fi_shm_cq_event_queue *ceq = &cq->event_queue;
	struct fi_shm_cq_event *event;

	if (!ceq->head)
		return NULL;

	event = ceq->head;
	ceq->head = event->next;
	ceq->count--;
	if (!ceq->head)
		ceq->tail = NULL;

	event->next = NULL;
	return event;
}

struct fi_shm_cq_event *fi_shm_cq_create_event(struct fi_shm_cq *cq,
                                               void *op_context, void *buf,
                                               uint64_t flags, size_t len,
                                               uint64_t data, uint64_t tag,
                                               size_t olen, int err)
{
	struct fi_shm_cq_event *event;

        MPOOL_ALLOC(cq->events_free_list,
                    struct fi_shm_cq_event,
                    event);

	if ((event->error = !!err)) {
		event->cqe.err.op_context = op_context;
		event->cqe.err.err = -err;
		event->cqe.err.data = data;
		event->cqe.err.tag = tag;
		event->cqe.err.olen = olen;
		event->cqe.err.prov_errno = 0;
		goto out;
	}

	switch (cq->format) {
	case FI_CQ_FORMAT_CONTEXT:
		event->cqe.context.op_context = op_context;
		break;

	case FI_CQ_FORMAT_TAGGED:
		event->cqe.tagged.op_context = op_context;
		event->cqe.tagged.buf        = buf;
		event->cqe.tagged.flags      = flags;
		event->cqe.tagged.len        = len;
		event->cqe.tagged.data       = data;
		event->cqe.tagged.tag        = tag;
		break;

	default:
		fprintf(stderr, "%s: unsupported CC format %d\n", __func__, cq->format);
		return NULL;
	}

out:
	return event;
}

static inline int
fi_shm_flush_connection_send_queue(struct fi_shm_cq   * cq,
				   fi_shm_connection_t    * connection,
				   struct fi_cq_tagged_entry *cqe)
{
        int busy_wait_count = fi_shm_env.busy_wait_count;

        fi_shm_request_t * request_iterator = NULL;

	int send_index = connection->last_send_index;

	while ((connection->send_ptr->buffers[send_index].header.is_busy) && (--busy_wait_count));

	if (busy_wait_count)
	{
		/* Try to flush postponed send request */

		request_iterator = TAILQ_FIRST(&(connection->postponed_send_request_queue));

		FI_SHM_TAGGED_SEND(conenction,
				   send_index,
				   request_iterator->buf,
				   request_iterator->len,
				   request_iterator->left_len,
				   request_iterator->tag);

		FI_SHM_DEBUG(("%s: Sent postponed. len = %llu, tag = %llx\n",
			      __func__, request_iterator->len, request_iterator->tag));

		if (request_iterator->left_len == 0)
		{
			FI_SHM_DEBUG(("%s: Send request completed: %llu\n", __func__, request_iterator->tag));

			/* Remove completed request from queue */

			TAILQ_REMOVE(&(connection->postponed_send_request_queue), request_iterator, requests);

			if (cqe)
			{
				/* Fill user's competion event from completed request */

				cqe->op_context = request_iterator->context;
				cqe->buf        = request_iterator->buf;
				cqe->len        = request_iterator->len;
				cqe->tag        = request_iterator->tag;
			}
			else
			{
				/* Allocate new completion event and enqueue it */

				struct fi_shm_cq_event *event = NULL;

				MPOOL_ALLOC(cq->events_free_list,
                                            fi_shm_cq_event_t,
                                            event);

                                event->cqe.tagged.op_context = request_iterator->context;
                                event->cqe.tagged.buf        = request_iterator->buf;
                                event->cqe.tagged.len        = request_iterator->len;
                                event->cqe.tagged.tag        = request_iterator->tag;

                                event->src_addr = (fi_addr_t)connection;

                                fi_shm_cq_enqueue_event(cq, event);
			}

			/* Release request */

			MPOOL_RETURN(cq->ep->request_free_list,
				     fi_shm_request_t,
				     request_iterator);

			return FI_SHM_OK;
		}
	}

	return FI_SHM_NOK;
}

static int
fi_shm_poll_connection(struct fi_shm_cq   * cq,
                       struct fi_cq_tagged_entry *cqe,
                       fi_shm_connection_t    * connection,
                       fi_addr_t *src_addr)
{
        size_t size_to_recv = 0;
        size_t fragment_size_to_recv = 0;

        int recv_index = 0;

        int max_busy_wait_count = fi_shm_env.busy_wait_count;
        int busy_wait_count     = max_busy_wait_count;

        fi_shm_request_t * request_iterator = NULL;

        /* Check for incoming connection requests */

        if (!connection->recv_ptr)
        {
                (void)fi_shm_ep_handle_incoming_connections(cq->ep);
                return FI_SHM_NOK;
        }

        /* Posted recv path */

        recv_index = connection->last_recv_index;

        busy_wait_count = max_busy_wait_count;

        while ((!connection->recv_ptr->buffers[recv_index].header.is_busy) &&
	       (--busy_wait_count));

        if (busy_wait_count)
        {
                /* There is an incomming message */

                if (!TAILQ_EMPTY(&(cq->ep->posted_recv_request_queue)))
                {
                        /* Try to match with existing messages or add to unexpected list */

                        TAILQ_FOREACH(request_iterator,
				      &(cq->ep->posted_recv_request_queue),
				      requests)
                        {
                                if (!((request_iterator->tag ^
				       connection->recv_ptr->buffers[recv_index].header.tag) &
				      (~request_iterator->ignore)))
                                {
                                        size_to_recv = connection->recv_ptr->buffers[recv_index].header.size;

                                        if (FI_SHM_IS_FIRST_FRAGMENT(size_to_recv))
                                        {
                                                if (FI_SHM_GET_TOTAL_SIZE(size_to_recv) <= request_iterator->len)
                                                {
                                                        request_iterator->len = FI_SHM_GET_TOTAL_SIZE(size_to_recv);
                                                        request_iterator->left_len = request_iterator->len;
                                                }
                                                else
                                                {
                                                        FI_SHM_DEBUG(("%s: Recv overflow: received (%llu) > expected (%llu)",
								      __func__,
                                                                      FI_SHM_GET_TOTAL_SIZE(size_to_recv), request_iterator->len));
                                                        exit(-1);
                                                }
                                        }

                                        FI_SHM_DEBUG(("%s: TAG matched with incomming message: tag %llx, ignore %llx, connection %p, header.tag %llx header.size = %llu, connection = %p\n",
						      __func__,
                                                      request_iterator->tag,
                                                      request_iterator->ignore,
                                                      request_iterator->connection,
                                                      connection->recv_ptr->buffers[recv_index].header.tag,
                                                      FI_SHM_GET_TOTAL_SIZE(size_to_recv),
                                                      connection));

                                        FI_SHM_TAGGED_RECV(connection,
                                                           recv_index,
                                                           request_iterator->buf,
                                                           request_iterator->len,
                                                           request_iterator->left_len,
                                                           request_iterator->tag,
                                                           fn_recv_overflow);
                                        break;
                                }
                        }

                        if (request_iterator)
                        {
                                if (request_iterator->left_len == 0)
                                {
                                        /* Remove completed request from queue */

                                        TAILQ_REMOVE(&(cq->ep->posted_recv_request_queue), request_iterator, requests);

					if (cqe)
					{
						/* Fill user's competion event from completed request */

						cqe->op_context = request_iterator->context;
						cqe->buf        = request_iterator->buf;
						cqe->len        = request_iterator->len;
						cqe->tag        = request_iterator->tag;
					}
					else
					{
						/* Allocate new completion event and enqueue it */

						struct fi_shm_cq_event *event = NULL;

						MPOOL_ALLOC(cq->events_free_list,
							    fi_shm_cq_event_t,
							    event);

						event->cqe.tagged.op_context = request_iterator->context;
						event->cqe.tagged.buf        = request_iterator->buf;
						event->cqe.tagged.len        = request_iterator->len;
						event->cqe.tagged.tag        = request_iterator->tag;

						event->src_addr = (fi_addr_t)connection;

						fi_shm_cq_enqueue_event(cq, event);
					}

                                        if (src_addr)
					{
                                                *src_addr = (fi_addr_t)connection;
					}

                                        FI_SHM_DEBUG(("%s: Recv request completed: %p\n", __func__, request_iterator));

                                        /* Release request */

                                        MPOOL_RETURN(cq->ep->request_free_list,
                                                     fi_shm_request_t,
                                                     request_iterator);

                                        return FI_SHM_OK;
                                }
                        }
                        else
                        {
                                goto unexpected_message;
                        }
            
                }
                else
                {
                        goto unexpected_message;
                }
        }

        /* Postponed send path */

        if (!TAILQ_EMPTY(&(connection->postponed_send_request_queue)))
        {
		if (fi_shm_flush_connection_send_queue(cq, connection, cqe) == FI_SHM_OK)
		{
			if (src_addr)
			{
				*src_addr = (fi_addr_t)connection;
			}

			return FI_SHM_OK;
		}
        }

fn_exit:

        return FI_SHM_NOK;

unexpected_message:

        FI_SHM_DEBUG(("%s: Uexpected message handling\n", __func__));

        size_to_recv = connection->recv_ptr->buffers[recv_index].header.size;

        if (FI_SHM_IS_FIRST_FRAGMENT(size_to_recv))
        {
                /* Create new request and allocate memory for buffer */

                MPOOL_ALLOC(cq->ep->request_free_list,
                            fi_shm_request_t,
                            request_iterator);

                request_iterator->type = FI_SHM_REQUEST_TAGGED_RECV;

                request_iterator->ue_buf = malloc(FI_SHM_GET_TOTAL_SIZE(size_to_recv));

                if (!request_iterator->ue_buf)
                {
                        MPOOL_RETURN(cq->ep->request_free_list,
                                     fi_shm_request_t,
                                     request_iterator);

                        FI_SHM_DEBUG(("%s: Can't allocate memory for unexpected message\n", __func__));

                        return FI_SHM_NOK;
                }

                request_iterator->len        = FI_SHM_GET_TOTAL_SIZE(size_to_recv);
                request_iterator->tag        = connection->recv_ptr->buffers[recv_index].header.tag;
                request_iterator->connection = connection;

                request_iterator->left_len   = FI_SHM_GET_TOTAL_SIZE(size_to_recv);

                FI_SHM_DEBUG(("%s: Created new request and allocated internal memory buffer. [header.len %llu, header.tag %llx, connection %p, left_len %llu]\n",
                              __func__,
                              request_iterator->len,
                              request_iterator->tag,
                              request_iterator->connection,
                              request_iterator->left_len));

                TAILQ_INSERT_TAIL(&(cq->ep->unexpected_recv_request_queue),
                                  request_iterator,
                                  requests);

                connection->last_unexpected_request = request_iterator;
        }

        fragment_size_to_recv = FI_SHM_GET_TOTAL_SIZE(size_to_recv) > FI_SHM_TRANSPORT_BUFFER_SIZE ?
                FI_SHM_TRANSPORT_BUFFER_SIZE : FI_SHM_GET_TOTAL_SIZE(size_to_recv);

        /* Receive fragment to unexpected buffer */

        FI_SHM_DEBUG(("%s: Receive fragment to unexpected buffer: [header.len %llu, header.tag %llx, connection %p, left_len %llu] \n",
                      __func__,
                      fragment_size_to_recv,
                      connection->recv_ptr->buffers[recv_index].header.tag,
                      connection,
                      connection->last_unexpected_request->left_len));

        fi_shm_transport_buffer_memcpy(connection->last_unexpected_request->ue_buf + 
                                       (connection->last_unexpected_request->len -
                                        connection->last_unexpected_request->left_len),
                                       connection->recv_ptr->buffers[recv_index].payload,
                                       fragment_size_to_recv);

        connection->recv_ptr->buffers[recv_index].header.is_busy = 0; /* Free buffer */

        connection->last_recv_index = (recv_index + 1) % FI_SHM_TRANSPORT_BUFFER_FIFO_SIZE;

        connection->last_unexpected_request->left_len -= fragment_size_to_recv;

        goto fn_exit;

fn_recv_overflow:

        FI_SHM_DEBUG(("%s: Request overflow: tag = %llx, expected len = %llu, connection = %p\n",
                      __func__, request_iterator->tag, request_iterator->len, request_iterator->connection));
        fi_shm_debug_dump_ep(cq->ep); /* DEBUG */
        FI_SHM_DEBUG(("%s: Emergency exit in 10 seconds\n", __func__));

        sleep(10);

        exit(-1); /* DEBUG */

        goto fn_exit;
}

int
fi_shm_cq_poll_all(struct fi_shm_cq *cq, struct fi_cq_tagged_entry *cqe,
                   int count, fi_addr_t *src_addr)
{
        /* Poll all directions */

        FI_SHM_DEBUG(("%s: poll all directions\n", __func__));
        if (!count)
        {
                return 0;
        }

        (void)fi_shm_ep_handle_incoming_connections(cq->ep);

        fi_shm_connection_t * connection_iterator = NULL;

        SLIST_FOREACH(connection_iterator, &(cq->ep->total_connection_list), total_connections)
        {
                FI_SHM_DEBUG(("%s: poll direction: %d\n", __func__, connection_iterator->connBlockId));

                if (fi_shm_poll_connection(cq, cqe, connection_iterator, src_addr) == FI_SHM_OK)
                {
                        return 1;
                }
        }

        return 0;
}

ssize_t fi_shm_cq_readfrom(struct fi_shm_cq *cq, void *buf, size_t count, fi_addr_t *src_addr)
{
        struct fi_shm_cq_event *event;
	int ret;
        ssize_t read_count;
        struct fi_cq_tagged_entry *cqe =
                (struct fi_cq_tagged_entry*)buf;
        assert(cq->entry_size == sizeof(*cqe));
        if (FI_SHM_CQ_EMPTY(cq) || !buf) {
                ret = fi_shm_cq_poll_all(cq, cqe,
                                         1, src_addr);

                if (ret > 0) {
                        return ret;
                }
	}

	if (!buf && count)
		return -FI_EINVAL;

	read_count = 0;
	while (count--) {
                event = fi_shm_cq_dequeue_event(cq);
		if (event) {
			if (!event->error) {
                                memcpy(buf, (void *)&event->cqe, cq->entry_size);

                                if (src_addr)
                                {
                                        *src_addr = event->src_addr;
                                        src_addr++;
                                }

                                MPOOL_RETURN(cq->events_free_list,
                                             struct fi_shm_cq_event,
                                             event);

				read_count++;
                                buf += cq->entry_size;
				
				continue;
			}
			else {
                                cq->pending_error = event;
				if (!read_count)
					read_count = -FI_EAVAIL;
				break;
			}
		}
		else {
			break;
		}
	}
        return read_count;
}

ssize_t fi_shm_cq_readerr(struct fi_shm_cq *cq, struct fi_cq_err_entry *buf, uint64_t flags)
{
        if (cq->pending_error) {
                memcpy(buf, &cq->pending_error->cqe, sizeof *buf);
		free(cq->pending_error);
                cq->pending_error = NULL;
		return sizeof *buf;
	}
        return 0;
}
