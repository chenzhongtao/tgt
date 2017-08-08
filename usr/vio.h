/*
 * SCSI object storage device command processing
 *
 * Copyright (C) 2016 chaolu zhang <finals@126.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __VESPACE_VIO_BLOCK_DEVICE__
#define __VESPACE_VIO_BLOCK_DEVICE__

#include <pthread.h>

#include "ring.h"

int send_msg(int fd, uint32_t idx);
int receive_msg(int fd, uint32_t *idx);

struct vio_connection {
        int seq;  // must be atomic
        int fd;
        int notify_fd;
        int state;

        pthread_t response_thread;
		pthread_mutex_t mutex;

        struct ringbuffer *msg_buffer;
};

enum {
    CLIENT_CONN_STATE_OPEN  = 0,
    CLIENT_CONN_STATE_CLOSE = 1,
};

struct vio_connection *new_vio_connection(char *socket_path, char *shm_file);
int shutdown_vio_connection(struct vio_connection *conn);
void drain_cmnd(struct vio_connection *conn);

int read_at(struct vio_connection *conn, void *buf, size_t count, off_t offset);
int write_at(struct vio_connection *conn, void *buf, size_t count, off_t offset);

void start_response_processing(struct vio_connection *conn);


#endif /* __VESPACE_VIO_BLOCK_DEVICE__ */
