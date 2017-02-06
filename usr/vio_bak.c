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

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "log.h"
#include "vio.h"

const int interval = 5;
const int retry_times = 5;

int readn(int fd, void *buf, int len) {
    int readed = 0;
    int ret;

    while (readed < len) {
        ret = read(fd, buf + readed, len - readed);
        if (ret < 0) {
            //eprintf("read return %d", ret);
            return ret;
        }
        readed += ret;
    }
    return readed;
}

int writen(int fd, void *buf, int len) {
    int wrote = 0;
    int ret;

    while (wrote < len) {
        ret = write(fd, buf + wrote, len - wrote);
        if (ret < 0) {
            //eprintf("write return %d", ret);
            return ret;
        }
        wrote += ret;
     }
     return wrote;
}

int send_msg(int fd, struct Message *msg) {
    int n = 0;
	//eprintf("send_msg type:%u offset:%ld length:%u", msg->Type, (long int)msg->Offset, msg->DataLength);
	 n = writen(fd, &msg->DataLength, sizeof(msg->DataLength));
	if (n != sizeof(msg->DataLength)) {
	    eprintf("fail to write datalength\n");
		return -EINVAL;
    }

    n = writen(fd, &msg->Seq, sizeof(msg->Seq));
    if (n != sizeof(msg->Seq)) {
        eprintf("fail to write seq\n");
        return -EINVAL;
    }

    n = writen(fd, &msg->Type, sizeof(msg->Type));
    if (n != sizeof(msg->Type)) {
        eprintf("fail to write type\n");
        return -EINVAL;
    }

    n = writen(fd, &msg->Offset, sizeof(msg->Offset));
    if (n != sizeof(msg->Offset)) {
        eprintf("fail to write offset\n");
        return -EINVAL;
    }

	if (msg->DataLength != 0 && msg->Type != TypeRead) {
	//if (msg->DataLength != 0 && msg->Type != TypeRead) {
		n = writen(fd, msg->Data, msg->DataLength);
		if (n != msg->DataLength) {
            if (n < 0) {
                perror("fail writing data");
            }
			eprintf("fail to write data, written %d expected %d\n", n, msg->DataLength);
            return -EINVAL;
		}
	}

    return 0;
}

// Caller need to release msg->Data
int receive_msg(int fd, struct Message *msg) {
	int n;
	//eprintf("receive_msg type:%u offset:%ld length:%u", msg->Type, (long int)msg->Offset, msg->DataLength);

    bzero(msg, sizeof(struct Message));

    // There is only one thread reading the response, and socket is
    // full-duplex, so no need to lock
    n = readn(fd, &msg->DataLength, sizeof(msg->DataLength));
    if (n != sizeof(msg->DataLength)) {
        eprintf("fail to read datalength\n");
		return -EINVAL;
    }

	n = readn(fd, &msg->Seq, sizeof(msg->Seq));
    if (n != sizeof(msg->Seq)) {
        eprintf("fail to write seq\n");
		return -EINVAL;
    }

    n = readn(fd, &msg->Type, sizeof(msg->Type));
    if (n != sizeof(msg->Type)) {
        eprintf("fail to read type\n");
		return -EINVAL;
    }

    n = readn(fd, &msg->Offset, sizeof(msg->Offset));
    if (n != sizeof(msg->Offset)) {
        eprintf("fail to read offset\n");
		return -EINVAL;
    }

	if (msg->DataLength > 0 && msg->Type != TypeWrite) {
		msg->Data = malloc(msg->DataLength);
        if (msg->Data == NULL) {
            perror("cannot allocate memory for data");
            return -EINVAL;
        }

        //if (msg->Type != TypeRead) {
		    n = readn(fd, msg->Data, msg->DataLength);
		    if (n != msg->DataLength) {
                eprintf("Cannot read full from fd, %d vs %d\n", msg->DataLength, n);
			    free(msg->Data);
			    return -EINVAL;
	    	}
		//}
	}
	return 0;
}

int send_request(struct vio_connection *conn, struct Message *req) {
    int rc = 0;
	//eprintf("send_request");

    pthread_mutex_lock(&conn->mutex);
    rc = send_msg(conn->fd, req);
    pthread_mutex_unlock(&conn->mutex);
    return rc;
}

int receive_response(struct vio_connection *conn, struct Message *resp) {
    int rc = 0;

    rc = receive_msg(conn->fd, resp);
    return rc;
}

void* response_process(void *arg) {
    struct vio_connection *conn = arg;
    struct Message *req, *resp;
    int ret = 0;

	resp = malloc(sizeof(struct Message));
    if (resp == NULL) {
        perror("cannot allocate memory for resp");
        return NULL;
    }

	//ret = receive_response(conn, resp);
    while (1) {
        ret = receive_response(conn, resp);
        if (ret != 0) {
            break;
        }

        switch (resp->Type) {
        case TypeOK:
            break;
        case TypeError:
            eprintf("Receive error for response %d of seq %lu: %s\n",
                                        resp->Type, (unsigned long)resp->Seq, (char *)resp->Data);
            /* fall through so we can response to caller */
            break;
        case TypeEOF:
            eprintf("Receive eof for response %d of seq %lu: %s\n",
                                        resp->Type, (unsigned long)resp->Seq, (char *)resp->Data);
            break;
        case TypeTimeout:
            eprintf("Receive timeout for response %d of seq %lu: %s\n",
                                        resp->Type, (unsigned long)resp->Seq, (char *)resp->Data);
            break;
        default:
            eprintf("Unknown message type %d\n", resp->Type);
        }

        pthread_mutex_lock(&conn->mutex);
        HASH_FIND_INT(conn->msg_table, &resp->Seq, req);
        if (req != NULL) {
            HASH_DEL(conn->msg_table, req);
        }

        pthread_mutex_unlock(&conn->mutex);

        if (req == NULL) {
            eprintf("Unknow response sequence %lu\n", (unsigned long)resp->Seq);
            free(resp->Data);
            continue;
        }

        pthread_mutex_lock(&req->mutex);
        if (resp->Type == TypeOK || resp->Type == TypeEOF) {
            req->DataLength = resp->DataLength;
            if (req->Type == TypeRead) {
				memcpy(req->Data, resp->Data, req->DataLength);
            }
            req->Type = resp->Type;
        } else if (resp->Type == TypeError) {
            req->Type = TypeError;
        }

        free(resp->Data);
        pthread_mutex_unlock(&req->mutex);
        pthread_cond_signal(&req->cond);
        //ret = receive_response(conn, resp);
    }

    free(resp);
    if (ret != 0) {
        eprintf("Receive response returned error");
    }
    return NULL;
}

void start_response_processing(struct vio_connection *conn) {
    int rc;

    rc = pthread_create(&conn->response_thread, NULL, &response_process, conn);
    if (rc < 0) {
        perror("Fail to create response thread");
        exit(-1);
    }
}

int new_seq(struct vio_connection *conn) {
    return __sync_fetch_and_add(&conn->seq, 1);
}

int process_request(struct vio_connection *conn, void *buf, size_t count, off_t offset,
                uint32_t type) {
    struct Message *req = malloc(sizeof(struct Message));
    int rc = 0;

    if (req == NULL) {
        perror("cannot allocate memory for req");
        return -EINVAL;
    }

    if (type != TypeRead && type != TypeWrite) {
        eprintf("BUG: Invalid type for process_request %d\n", type);
        rc = -EFAULT;
        goto free;
    }

    req->Seq = new_seq(conn);
    req->Type = type;
    req->Offset = offset;
    req->DataLength = count;
    req->Data = buf;
    //eprintf("[process_request] type:%u offset:%ld length:%u\n", req->Type, (long)req->Offset, req->DataLength);
    if (req->Type == TypeRead) {
        bzero(req->Data, count);
    }

    rc = pthread_cond_init(&req->cond, NULL);
    if (rc < 0) {
        perror("Fail to init phread_cond");
        rc = -EFAULT;
        goto free;
    }

    rc = pthread_mutex_init(&req->mutex, NULL);
    if (rc < 0) {
        perror("Fail to init phread_mutex");
        rc = -EFAULT;
        goto free;
    }

    pthread_mutex_lock(&conn->mutex);
    if (conn->state != CLIENT_CONN_STATE_OPEN) {
        eprintf("Cannot queue in more request, Connection is closed");
        rc = -EFAULT;
        goto free;
    }

    HASH_ADD_INT(conn->msg_table, Seq, req);
    pthread_mutex_unlock(&conn->mutex);

    pthread_mutex_lock(&req->mutex);
    rc = send_request(conn, req);
    if (rc < 0) {
        eprintf("[process_request] type:%u send_request return:%d\n", req->Type, rc);
        goto out;
    }

    pthread_cond_wait(&req->cond, &req->mutex);

    if (req->Type != TypeOK) {
        eprintf("[process_request] type:%u offset:%ld length:%u\n", req->Type, (long)req->Offset, req->DataLength);
        rc = -EFAULT;
    }
out:
    pthread_mutex_unlock(&req->mutex);
free:
    free(req);
    return rc;
}

int read_at(struct vio_connection *conn, void *buf, size_t count, off_t offset) {
    return process_request(conn, buf, count, offset, TypeRead);
}

int write_at(struct vio_connection *conn, void *buf, size_t count, off_t offset) {
    return process_request(conn, buf, count, offset, TypeWrite);
}

struct vio_connection *new_vio_connection(char *socket_path) {
    //eprintf("new_vio_connection socket_path:%s", socket_path);
    struct sockaddr_un addr;
    int fd, rc = 0;
    int i, connected = 0;
    struct vio_connection *conn = NULL;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket error");
        exit(-1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(socket_path) >= 108) {
        eprintf("socket path is too long, more than 108 characters");
        exit(-EINVAL);
    }

    strncpy(addr.sun_path, socket_path, strlen(socket_path));

    for (i = 0; i < retry_times; i++) {
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            connected = 1;
            break;
        }

        perror("connect error, retrying");
        sleep(interval);
    }

    if (connected == 0) {
        perror("connect error");
        exit(-EFAULT);
    }

    conn = malloc(sizeof(struct vio_connection));
    if (conn == NULL) {
        perror("cannot allocate memory for conn");
        return NULL;
    }

    conn->fd = fd;
    conn->seq = 0;
    conn->msg_table = NULL;

    rc = pthread_mutex_init(&conn->mutex, NULL);
    if (rc < 0) {
        perror("fail to init conn->mutex");
        exit(-EFAULT);
    }

    //³õÊ¼»¯ring buffer

	//eprintf("new_vio_connection socket_path:%s successfully", socket_path);
    conn->state = CLIENT_CONN_STATE_OPEN;
    return conn;
}

int shutdown_vio_connection(struct vio_connection *conn) {
	struct Message *req, *tmp;
	pthread_mutex_lock(&conn->mutex);
	conn->state = CLIENT_CONN_STATE_CLOSE; //prevent future requests

	//clean up and fail all pending requests
	HASH_ITER(hh, conn->msg_table, req, tmp) {
        HASH_DEL(conn->msg_table, req);

        pthread_mutex_lock(&req->mutex);
        req->Type = TypeError;
        eprintf("Cancel request %lu due to disconnection", (unsigned long)req->Seq);
        pthread_mutex_unlock(&req->mutex);
        pthread_cond_signal(&req->cond);
	}
	pthread_mutex_unlock(&conn->mutex);

    close(conn->fd);
    free(conn);
    return 0;
}

