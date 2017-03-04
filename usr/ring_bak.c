/*
 * shared memory ring buffer
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <sys/mman.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include "ring.h"
#include "log.h"

#define FILE_NAME_LEN 32
#define FILE_PATH_LEN 48
#define SHM_DIR "/dev/shm/"

struct ringbuffer *alloc(uint32_t size, char *shm_file)
{
    struct ringbuffer *ringbuf = (struct ringbuffer *)malloc(sizeof(struct ringbuffer));
    int i = 0, rc = 0;
    struct cmnd *cmd = NULL;
    ringbuf->size = sizeof(uint32_t) + sizeof(struct cmnd) * CMD_DEPTH + size;
    eprintf("!!!!");
    int fd = open(shm_file, O_RDWR | O_NONBLOCK | O_CREAT, 0666);
    if (fd < 0) {
        eprintf("open file: %s return %d err:%s", shm_file, fd, strerror(errno));
        goto error;
    }
	eprintf("@@@ fd:%d size:%d", fd, ringbuf->size);

	if (ftruncate(fd, ringbuf->size) < 0) {
        eprintf("ftruncate error");
        goto error;
	}

    ringbuf->buffer = (struct ring *)mmap(NULL, ringbuf->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ringbuf->buffer == (void *)-1) {
        eprintf("mmap size:%u return -1", (unsigned int)(ringbuf->size));
        goto error;
    }
    eprintf("####");
    ringbuf->sendix = 0;
    ringbuf->recvix = 0;
    ringbuf->restix = CMD_DEPTH;
    ringbuf->head = 0;
    ringbuf->tail = 0;
    ringbuf->skip = 0;
    ringbuf->full = 0;
    ringbuf->datasize = size;
    eprintf("111: %p", ringbuf->buffer);
    eprintf("222: %p", &ringbuf->buffer->magic);
    eprintf("333: %p", ringbuf->buffer->cmds);
    eprintf("333: %p", ringbuf->buffer->data);
    ringbuf->buffer->magic = MAGIC;

    eprintf("$$$");
    rc = pthread_mutex_init(&ringbuf->mutex, NULL);
    if (rc < 0) {
        eprintf("pthread_mutex_init return -1");
        goto error;
    }
	eprintf("^^^");

    //初始化命令区域
    for (i = 0; i < CMD_DEPTH; i++) {
        cmd = &(ringbuf->buffer->cmds[i]);
        cmd->state = STATE_FINISH;

        rc = pthread_cond_init(&cmd->cond, NULL);
	    if (rc < 0) {
		    eprintf("Fail to init phread_cond");
		    goto error;
	    }

    	rc = pthread_mutex_init(&cmd->mutex, NULL);
        if (rc < 0) {
            eprintf("Fail to init phread_mutex");
            goto error;
        }
    }
	eprintf("&&&");

    return ringbuf;

error:
    free(ringbuf);

    return NULL;
}

int destroy(struct ringbuffer *ringbuf)
{
	if (NULL != ringbuf) {
		if (NULL != ringbuf->buffer) {
			munmap(ringbuf->buffer, ringbuf->size);
		}
		free(ringbuf);
	}

    return 0;
}

struct cmnd * add(struct ringbuffer *rbuf, void *buf, uint32_t length, int64_t offset,
                uint32_t type, uint64_t seq, uint32_t *idx)
{
    struct cmnd *cmd = NULL;

    do {
        pthread_mutex_lock(&rbuf->mutex);
        eprintf("add before sendix:%u recvix:%u restix:%u head:%u tail:%u skip:%u size:%u datasize:%u length:%u",
                rbuf->sendix, rbuf->recvix, rbuf->restix, rbuf->head, rbuf->tail, rbuf->skip, rbuf->size, rbuf->datasize, length);

        if (rbuf->restix > 0) {
            if (rbuf->head > rbuf->tail) {
                if (rbuf->head + length > rbuf->datasize) { //需要连续的内存空间
                    if (rbuf->tail > length) { //数据区域尾部不足以分配该命令的内存空间
                        rbuf->skip = rbuf->datasize - rbuf->head;
                        rbuf->head = 0;
                    } else {
                        goto wait; //暂时没有足够的空间
                    }
                }
            } else if (rbuf->head < rbuf->tail) {
                if (rbuf->head + length > rbuf->tail) {
                    goto wait;  //暂时没有足够的空间
                }
            } else { //rbuf->head == rbuf->tail 表示缓冲区满或空
                if (rbuf->full) {
                    goto wait;
                }
            }

			*idx = rbuf->sendix;
			cmd = &(rbuf->buffer->cmds[*idx]);
            cmd->state = STATE_SEND;
            cmd->off = rbuf->head;
            rbuf->head += length;
            if (rbuf->head >= rbuf->datasize) {
                rbuf->head = rbuf->head - rbuf->datasize;
            }
            if (rbuf->head == rbuf->tail) {
                rbuf->full = 1;
            }
            rbuf->sendix = (*idx + 1) & CMD_MASK;
            rbuf->restix--;
            eprintf("add after sendix:%u recvix:%u restix:%u head:%u tail:%u skip:%u size:%u datasize:%u length:%u",
                rbuf->sendix, rbuf->recvix, rbuf->restix, rbuf->head, rbuf->tail, rbuf->skip, rbuf->size, rbuf->datasize, length);
            pthread_mutex_unlock(&rbuf->mutex);
            break;
        }

wait:
        pthread_mutex_unlock(&rbuf->mutex);
		//wait for a minite
		sched_yield();
		eprintf("add cmd sched_yield");
    }while(1);

    cmd->length = length;
    cmd->seq = seq;
    cmd->type = type;
    cmd->offset = offset;

	eprintf("add cmd idx:%u, state:%u, off:%u, length:%u, seq:%llu, type:%u, offset:%lld", *idx,
			cmd->state, cmd->off, cmd->length, (long long unsigned)cmd->seq, cmd->type, (long long int)cmd->offset);

    if (cmd->type == TypeWrite) {
		memcpy(&(rbuf->buffer->data[cmd->off]), buf, length); //将数据拷贝到共享内存中
    }
    eprintf("add cmd finish");
    return cmd;
}

int del(struct ringbuffer *rbuf, uint32_t idx)
{
    struct cmnd *cmd = NULL;
    if (idx >= CMD_DEPTH) {
        return -1;
    }

    while (1) {
		pthread_mutex_lock(&rbuf->mutex);
		eprintf("del idx:%d recvix:%d", idx, rbuf->recvix);
		if (idx == rbuf->recvix) { //命令完成的顺序必须与进入buffer顺序一致
		    cmd = &(rbuf->buffer->cmds[idx]);
			eprintf("del before seq:%llu sendix:%u recvix:%u restix:%u head:%u tail:%u skip:%u size:%u datasize:%u", (long long unsigned)cmd->seq,
                rbuf->sendix, rbuf->recvix, rbuf->restix, rbuf->head, rbuf->tail, rbuf->skip, rbuf->size, rbuf->datasize);

			rbuf->recvix = (rbuf->recvix + 1) & CMD_MASK;
			rbuf->restix++;
			rbuf->tail += cmd->length + rbuf->skip;
			rbuf->skip = 0;
			if (rbuf->tail >= rbuf->datasize) {
                rbuf->tail = rbuf->tail - rbuf->datasize;
			}
            if (rbuf->full) {
                rbuf->full = 0;
            }
			cmd->state = STATE_FINISH;
			eprintf("del after seq:%llu sendix:%u recvix:%u restix:%u head:%u tail:%u skip:%u size:%u datasize:%u", (long long unsigned)cmd->seq,
                rbuf->sendix, rbuf->recvix, rbuf->restix, rbuf->head, rbuf->tail, rbuf->skip, rbuf->size, rbuf->datasize);
			pthread_mutex_unlock(&rbuf->mutex);
			break;
		}
    	pthread_mutex_unlock(&rbuf->mutex);
    	//wait for a minite
    	sched_yield();
    	eprintf("del cmd sched_yield");
    }

    return 0;
}

struct cmnd *get(struct ringbuffer *rbuf, uint32_t idx)
{
    if (idx >= CMD_DEPTH) {
        return NULL;
    }

    return &(rbuf->buffer->cmds[idx]);
}

