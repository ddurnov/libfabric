#include <rdma/fi_errno.h>
#include "fi_shm_utils.h"

int fi_shm_getname(struct shm_ep *ep, void *addr, size_t *addrlen)
{
        if (*addrlen < sizeof(fi_shm_blockid_t)) {
		*addrlen = sizeof(fi_shm_blockid_t);
		return -FI_ETOOSMALL;
	}

        *(fi_shm_blockid_t *)addr = ep->myConnBlockId;
	*addrlen = sizeof(fi_shm_blockid_t);
        return 0;
}

size_t fi_shm_addrlen()
{
        return sizeof(fi_shm_blockid_t);
}

static int
fi_shm_ep_initiate_connection(struct shm_ep * ep,
                              fi_shm_blockid_t       remote_blockid,
                              fi_shm_connection_t ** remote_connection)
{
        int current_slot_index = 0;

        int result = -1;

        /* Allocate memory for connection from connections pool */

        MPOOL_ALLOC(ep->connections_pool,
                    fi_shm_connection_t,
                    *remote_connection);

        fi_shm_connection_t * connection_tmp = *remote_connection;

        /* Initialize queues */

        TAILQ_INIT(&(connection_tmp->postponed_send_request_queue));

        connection_tmp->last_unexpected_request = NULL;

        /* Try to attach to remote connection control block */

        connection_tmp->connBlockId = remote_blockid;
        connection_tmp->conn_ptr    = shmat(connection_tmp->connBlockId, NULL, 0);

        if (connection_tmp->conn_ptr == (void *)-1)
        {
                FI_SHM_DEBUG(("%s: shmat to %d failed with errno=%d (%s)\n",
                              __func__, remote_blockid, errno, strerror(errno)));
		return FI_SHM_NOK;
        }

        /* Book connection slot on remote side. All do atomic increment */

        current_slot_index = fi_shm_x86_atomic_fetch_and_add_int(&connection_tmp->conn_ptr->size, 1);

        if (current_slot_index >= FI_SHM_CONN_CTRL_POOL_MAX_SIZE)
        {
                fprintf(stderr, "%s: failed to find free slot for (%d) index.\n",
                        __func__, current_slot_index);
		goto err_out_detach;
        }

        /* Allocate send transport buffers block and put it into connection block */

        connection_tmp->sendBlockId = shmget(IPC_PRIVATE,
                                             sizeof(fi_shm_transport_buffer_fifo_t),
                                             IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);

        if (connection_tmp->sendBlockId == -1)
        {
                fprintf(stderr, "%s: shmget failed with errno=%d (%s)\n",
                        __func__, errno, strerror(errno));
		goto err_out_detach;
        }

        connection_tmp->send_ptr = shmat(connection_tmp->sendBlockId, NULL, 0);

        if (connection_tmp->send_ptr == (void *)-1)
        {
                fprintf(stderr, "%s: shmat failed with errno=%d (%s)\n",
                        __func__, errno, strerror(errno));
		goto err_out_detach;
        }

        result = shmctl(connection_tmp->sendBlockId, IPC_RMID, NULL);

        if (result == -1)
        {
                fprintf(stderr, "%s: shmctl failed with errno=%d (%s)\n",
                        __func__, errno, strerror(errno));
		goto err_out_detach;
        }

        if (fi_shm_env.numa_aware)
        {
                result = mbind(connection_tmp->send_ptr,
                               sizeof(fi_shm_transport_buffer_fifo_t),
                               MPOL_PREFERRED,
                               NULL,
                               0,
                               MPOL_MF_MOVE /*MPOL_MF_MOVE_ALL*/);

                if (result != 0)
                {
                        FI_SHM_DEBUG(("%s: NUMA: Unable to bind send queue memory region %p (%s)\n",
                                      __func__, connection_tmp->send_ptr, strerror(errno)));
                }
        }

        memset(connection_tmp->send_ptr, 0, sizeof(fi_shm_transport_buffer_fifo_t)); /* Touch send buffers */

        fi_shm_x86_sfence();

        FI_SHM_DEBUG(("%s: Filling connection block %d[%d] = %d, pid = %d\n",
                      __func__,
                      connection_tmp->connBlockId,
                      current_slot_index,
                      connection_tmp->sendBlockId,
                      getpid()));

        connection_tmp->conn_ptr->pool[current_slot_index].connBlockId = ep->myConnBlockId;
        connection_tmp->conn_ptr->pool[current_slot_index].sendBlockId = connection_tmp->sendBlockId;
        connection_tmp->conn_ptr->pool[current_slot_index].pid         = getpid();
        connection_tmp->conn_ptr->pool[current_slot_index].numa_node   = ep->myNumaNodeId;

        connection_tmp->last_send_index = 0;

        fi_shm_x86_sfence();
    
        connection_tmp->conn_ptr->pool[current_slot_index].isReady = 1;

        fi_shm_x86_sfence();

        /* Add connection to active polling list */
    
        SLIST_INSERT_HEAD(&(ep->total_connection_list), connection_tmp, total_connections);

        return FI_SHM_OK;

err_out_detach:

        shmdt(connection_tmp->conn_ptr);

        return FI_SHM_NOK;
}

int
fi_shm_ep_handle_incoming_connections(struct shm_ep * ep)
{
        fi_shm_conn_ctrl_entry_t * connection_ctrl_tmp = NULL;
        fi_shm_connection_t *      connection_tmp      = NULL;

        fi_shm_connection_t * connection_iterator = NULL;

        int conn_ctrl_pool_iter = 0;
        int conn_ctrl_pool_size = ep->my_conn_ptr->size; /* volatile int read. x86 atomic read */

        int incomming_connections_num = 0;

        if (conn_ctrl_pool_size != ep->my_current_conn_ctrl_pool_size)
        {
                incomming_connections_num = conn_ctrl_pool_size - ep->my_current_conn_ctrl_pool_size;

                FI_SHM_DEBUG(("%s: Connecting %d peers\n", __func__, incomming_connections_num));

                while (incomming_connections_num) {

                        for (conn_ctrl_pool_iter = 0; conn_ctrl_pool_iter < conn_ctrl_pool_size; conn_ctrl_pool_iter++)
                        {
                                if (ep->my_conn_ptr->pool[conn_ctrl_pool_iter].isReady)
                                {
                                        connection_ctrl_tmp = &(ep->my_conn_ptr->pool[conn_ctrl_pool_iter]);

                                        /* Lookup connection pool and find matching connection request */

                                        SLIST_FOREACH(connection_iterator, &(ep->total_connection_list), total_connections)
                                        {
                                                if (connection_iterator->connBlockId == connection_ctrl_tmp->connBlockId)
                                                {
                                                        /* Found match */

                                                        FI_SHM_DEBUG(("%s: Found match: connBlockId %d == %d\n", __func__,
                                                                      connection_iterator->connBlockId,
                                                                      connection_ctrl_tmp->connBlockId));
                                                        break;
                                                }
                                        }

                                        connection_tmp = connection_iterator;

                                        if (!connection_tmp)
                                        {
                                                /* We haven't initiated connection yet. Accept it and initiate connection back. */

                                                fi_shm_ep_initiate_connection(ep,
                                                                              connection_ctrl_tmp->connBlockId,
                                                                              &connection_tmp);
                                        }

                                        connection_tmp->recvBlockId = connection_ctrl_tmp->sendBlockId;
                                        connection_tmp->pid         = connection_ctrl_tmp->pid;
                                        connection_tmp->numa_node   = connection_ctrl_tmp->numa_node;

                                        /* Attempt to attach to recv transport buffers block */

                                        connection_tmp->recv_ptr = shmat(connection_tmp->recvBlockId, NULL, 0);

                                        if (connection_tmp->recv_ptr == (void *)-1)
                                        {
                                                fprintf(stderr, "%s: shmat failed with errno=%d (%s)\n",
                                                        __func__, errno, strerror(errno));
                                                return FI_SHM_NOK;
                                        }

                                        FI_SHM_DEBUG(("%s: Attached recv transport buffer for %d = %d/%d pid = %d. Table entry %p\n",
                                                      __func__,
                                                      connection_tmp->connBlockId,
                                                      connection_tmp->sendBlockId,
                                                      connection_tmp->recvBlockId,
                                                      connection_tmp->pid,
                                                      connection_tmp));

                                        ep->my_conn_ptr->pool[conn_ctrl_pool_iter].isReady = 0; /* Clear connection request flag */

                                        connection_tmp->last_recv_index = 0;
                
                                        incomming_connections_num--;
                                }
                        }
                }

                ep->my_current_conn_ctrl_pool_size = conn_ctrl_pool_size;
        }

        return FI_SHM_OK;
}

int fi_shm_connect(struct shm_ep *ep, void *addr, fi_addr_t *remote_connection) {
        fi_shm_blockid_t *remote_blockid =
                (fi_shm_blockid_t*)addr;
        fi_shm_connection_t * connection_iterator = NULL;

        if (ep->myConnBlockId ==  *remote_blockid)
        {
                *remote_connection = FI_SHM_SELF_ADDR;
        }

        SLIST_FOREACH(connection_iterator, &(ep->total_connection_list), total_connections)
        {
                if (connection_iterator->connBlockId == *remote_blockid)
                {
                        *remote_connection = (fi_addr_t)((void *)connection_iterator);

                        FI_SHM_DEBUG(("%s: Connection already exists\n", __func__));

                        return FI_SHM_OK;
                }
        }

        /* New connection request handling */

        (void)fi_shm_ep_initiate_connection(ep,
                                            *remote_blockid,
                                            (fi_shm_connection_t **)remote_connection);

        /* Try to establish connection. Check for incomming connections request */

        /* (void)fi_shm_ep_handle_incoming_connections(ep); */ /* DEBUG */

        return 0;

}
