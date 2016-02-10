#include <rdma/fi_errno.h>
#include "fi_shm_utils.h"

struct fi_shm_env fi_shm_env;

static int
fi_shm_get_int_env(char *name, int default_value)
{
	char *s;

	s = getenv(name);
	if (s) {
		if (s[0]>='0' && s[0]<='9')
			return atoi(s);

		if (!strcasecmp(s, "yes") || !strcasecmp(s, "on"))
			return 1;

		if (!strcasecmp(s, "no") || !strcasecmp(s, "off"))
			return 0;
	}

	return default_value;
}

int
fi_shm_init_ep(struct shm_ep *ep)
{
	int err = 0;

	fi_shm_env.debug             = fi_shm_get_int_env("OFI_SHM_DEBUG",   0);
	fi_shm_env.warning           = fi_shm_get_int_env("OFI_SHM_WARNING", 0);
	fi_shm_env.busy_wait_count   = fi_shm_get_int_env("OFI_SHM_BUSY_WAIT_COUNT", 1);
	fi_shm_env.active_poll_count = fi_shm_get_int_env("OFI_SHM_ACTIVE_POLL_COUNT", 64);
	fi_shm_env.numa_aware        = fi_shm_get_int_env("OFI_SHM_NUMA_AWARE", 1);

	ep->active_poll_count = 0;

	ep->myConnBlockId = shmget(IPC_PRIVATE,
				   sizeof(fi_shm_conn_ctrl_pool_t),
				   IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);

	if (ep->myConnBlockId == -1)
	{
		fprintf(stderr, "%s: shmget failed with errno=%d (%s)\n",
			__func__, errno, strerror(errno));
		err = errno;
		goto err_out_close_ep;
	}

	ep->my_conn_ptr = shmat(ep->myConnBlockId, NULL, 0);

	if (ep->my_conn_ptr == (void *)-1)
	{
		fprintf(stderr, "%s: shmat to %d failed with errno=%d (%s)\n",
			__func__, ep->myConnBlockId, errno, strerror(errno));
		err = errno;
		goto err_out_close_ep;
	}

	err = shmctl(ep->myConnBlockId, IPC_RMID, NULL);

	if (err == -1)
	{
		fprintf(stderr, "%s: shmctl failed with errno=%d (%s)\n",
			__func__, errno, strerror(errno));
		err = errno;
		goto err_out_close_ep;
	}

	ep->myNumaNodeId = 0;

	if (fi_shm_env.numa_aware)
	{
		struct bitmask * node_mask = NULL;
		node_mask = numa_get_run_node_mask();
		if (node_mask)
		{
			int i;
			for (i = 0; i < node_mask->size; i++)
			{
				if ((node_mask->maskp[i / ((sizeof(unsigned long) * 8))] >>
				     (i % (sizeof(unsigned long) * 8))) & 1)
				{
					ep->myNumaNodeId = i;
					break;
				}
			}
		}
	}

	if (fi_shm_env.numa_aware)
	{
		err = mbind(ep->my_conn_ptr,
			    sizeof(fi_shm_conn_ctrl_pool_t),
			    MPOL_PREFERRED,
			    NULL,
			    0,
			    MPOL_MF_MOVE | MPOL_MF_STRICT);
		if (err != 0)
		{
			FI_SHM_WARNING(("%s: NUMA: Unable to bind connection block memory region %p (%s)\n",
					__func__, ep->my_conn_ptr, strerror(errno)));
		}
	}

	FI_SHM_DEBUG(("%s: shmget/shmat -> %d/%p\n", __func__,
		      ep->myConnBlockId,
		      ep->my_conn_ptr));

	memset(ep->my_conn_ptr, 0, sizeof(fi_shm_conn_ctrl_pool_t));

	fi_shm_compiler_barrier();

	/* Initialize global posted and unexpected recv queues */

	TAILQ_INIT(&(ep->posted_recv_request_queue));
	TAILQ_INIT(&(ep->unexpected_recv_request_queue));

	/* Initialize global request pool */

	mpool_init(&ep->request_free_list,
		   sizeof(fi_shm_request_t),
		   FI_SHM_REQUEST_FREE_LIST_INIT_SIZE);

	mpool_init(&ep->connections_pool,
		   sizeof(fi_shm_connection_t),
		   FI_SHM_CONN_CTRL_POOL_MAX_SIZE);

	SLIST_INIT(&(ep->total_connection_list)); /* Init EP connection lists */
	LIST_INIT(&(ep->active_connection_list)); /* Init EP active connection lists */

	ep->last_polled_connection  = NULL;
	ep->last_flushed_connection = NULL;
	ep->min_activity_connection = NULL;

err_out_close_ep:
	return err;
}

int
fi_shm_fini_ep(struct shm_ep *ep)
{
	if (TAILQ_EMPTY(&(ep->unexpected_recv_request_queue)))
	{
		FI_SHM_DEBUG(("%s: Unexpected recv queue isn't empty!\n", __func__));
	}

	if (TAILQ_EMPTY(&(ep->posted_recv_request_queue)))
	{
		FI_SHM_DEBUG(("%s: Posted recv queue isn't empty!\n", __func__));
	}

	return 0;
}


#ifdef FI_SHM_DEBUG_MODE
void
fi_shm_debug_dump_connection(fi_shm_connection_t * connection)
{
	fi_shm_request_t * request_iterator = NULL;

	int i = 0;

	FI_SHM_DEBUG(("Connection last_send_index = %d, last_recv_index = %d\n",
		      connection->last_send_index,
		      connection->last_recv_index));

	FI_SHM_DEBUG(("Connection postponed_send_request_queue dump\n"));

	TAILQ_FOREACH(request_iterator, &(connection->postponed_send_request_queue), requests)
	{
		FI_SHM_DEBUG((" [%d]: type = %d, buf = %p, len = %llu, tag = %llx, context = %p, left_len = %llu, ue_buf = %p\n",
			      i++,
			      request_iterator->type,
			      request_iterator->buf,
			      request_iterator->len,
			      request_iterator->tag,
			      request_iterator->context,
			      request_iterator->left_len,
			      request_iterator->ue_buf));
	}

	i = 0;

	request_iterator = connection->last_unexpected_request;

	if (request_iterator)
	{
		FI_SHM_DEBUG((" [last unexpected]: type = %d, buf = %p, len = %llu, tag = %llx, context = %p, left_len = %llu, ue_buf = %p\n",
			      request_iterator->type,
			      request_iterator->buf,
			      request_iterator->len,
			      request_iterator->tag,
			      request_iterator->context,
			      request_iterator->left_len,
			      request_iterator->ue_buf));
	}
	else
	{
		FI_SHM_DEBUG((" [last unexpected]: nil\n"));
	}
}

void
fi_shm_debug_dump_ep(struct shm_ep * ep)
{
	fi_shm_request_t * request_iterator       = NULL;
	fi_shm_connection_t * connection_iterator = NULL;

	int i = 0;

	FI_SHM_DEBUG(("Global unexpected_recv_request_queue dump\n"));

	TAILQ_FOREACH(request_iterator, &(ep->unexpected_recv_request_queue), requests)
	{
		FI_SHM_DEBUG((" [%d]: type = %d, buf = %p, len = %llu, tag = %llx, context = %p, left_len = %llu, ue_buf = %p\n",
			      i++,
			      request_iterator->type,
			      request_iterator->buf,
			      request_iterator->len,
			      request_iterator->tag,
			      request_iterator->context,
			      request_iterator->left_len,
			      request_iterator->ue_buf));
	}

	i = 0;

	FI_SHM_DEBUG(("Global posted_recv_request_queue dump\n"));

	TAILQ_FOREACH(request_iterator, &(ep->posted_recv_request_queue), requests)
	{
		FI_SHM_DEBUG((" [%d]: type = %d, buf = %p, len = %llu, tag = %llx, context = %p, left_len = %llu, ue_buf = %p\n",
			      i++,
			      request_iterator->type,
			      request_iterator->buf,
			      request_iterator->len,
			      request_iterator->tag,
			      request_iterator->context,
			      request_iterator->left_len,
			      request_iterator->ue_buf));
	}

	FI_SHM_DEBUG(("Connections dump\n"));

	i = 0;

	SLIST_FOREACH(connection_iterator, &(ep->total_connection_list), total_connections)
	{
		FI_SHM_DEBUG((" dump connection [%d] %d\n", i++, connection_iterator->connBlockId));
		fi_shm_debug_dump_connection(connection_iterator);
	}

	int j, nptrs;
#define MAX_STACK_SIZE (100)
	void *buffer[MAX_STACK_SIZE];
	char **strings;

	nptrs = backtrace(buffer, MAX_STACK_SIZE);
	FI_SHM_DEBUG(("backtrace() returned %d addresses\n", nptrs));

	strings = backtrace_symbols(buffer, nptrs);

	for (j = 0; j < nptrs; j++)
	{
		FI_SHM_DEBUG(("%s\n", strings[j]));
	}

	free(strings);
}
#else /* FI_SHM_DEBUG_MODE */
void
fi_shm_debug_dump_connection(fi_shm_connection_t * connection) {}
void
fi_shm_debug_dump_ep(struct shm_ep * ep) {}
#endif /* FI_SHM_DEBUG_MODE */
