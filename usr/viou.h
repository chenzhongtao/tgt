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
#include "uthash.h"

struct Message {
        uint64_t        Seq;
        uint32_t        Type;
        int64_t         Offset;
        uint32_t        DataLength;
        void*           Data;

	pthread_cond_t  cond;
	pthread_mutex_t mutex;

        UT_hash_handle hh;
};

enum uint32_t {
	TypeRead     = 0x12,
	TypeWrite    = 0x13,
	TypeOK       = 0x80,
	TypeError    = 0x81,
	TypeEOF      = 0x82,
	TypeTimeout  = 0x83,
	TypeClose    = 0x86
};

int send_msg(int fd, struct Message *msg);
int receive_msg(int fd, struct Message *msg);

struct viou_connection {
        int seq;  // must be atomic
        int fd;
        int notify_fd;
        int state;

        pthread_t response_thread;

        struct Message *msg_table;
        pthread_mutex_t mutex;
};

enum {
    CLIENT_CONN_STATE_OPEN  = 0,
    CLIENT_CONN_STATE_CLOSE = 1,
};

struct viou_connection *new_viou_connection(char *socket_path);
int shutdown_viou_connection(struct viou_connection *conn);
void drain_request(struct viou_connection *conn);

int read_at_unix(struct viou_connection *conn, void *buf, size_t count, off_t offset);
int write_at_unix(struct viou_connection *conn, void *buf, size_t count, off_t offset);

void start_response_processing_unix(struct viou_connection *conn);


#endif /* __VESPACE_VIO_BLOCK_DEVICE__ */
