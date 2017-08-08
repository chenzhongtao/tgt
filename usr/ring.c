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

struct ringbuffer *init(char *shm_file)
{
    struct ringbuffer *ringbuf = (struct ringbuffer *)malloc(sizeof(struct ringbuffer));
    int i = 0, rc = 0;
    struct cmnd *cmd = NULL;
    struct cmnd_idx *cidx = NULL;

    //ringbuf->size = sizeof(uint32_t) + sizeof(struct cmnd) * CMD_DEPTH + sizeof(struct cmnd) * 4;
    ringbuf->shm_file = shm_file;
    ringbuf->size = 32 << 20; //32M
    int fd = open(ringbuf->shm_file, O_RDWR | O_NONBLOCK | O_CREAT, 0666);
    if (fd < 0) {
        Log_error("[init] open file: %s return %d err:%s\n", ringbuf->shm_file, fd, strerror(errno));
        goto error;
    }
	//eprintf("@@@ fd:%d size:%d", fd, ringbuf->size);

	if (ftruncate(fd, ringbuf->size) < 0) {
        Log_error("[init] ftruncate error\n");
        goto error;
	}

    ringbuf->buffer = (struct ring *)mmap(NULL, ringbuf->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ringbuf->buffer == (void *)-1) {
        Log_error("[init] mmap size:%u return -1\n", (unsigned int)(ringbuf->size));
        goto error;
    }
    Log_debug("[init] mmap size:%u fd:%d\n", (unsigned int)(ringbuf->size), fd);

    ringbuf->sendix = 0;
    ringbuf->recvix = 0;
    ringbuf->restix = CMD_DEPTH;
    ringbuf->buffer->magic = MAGIC;
    //eprintf("ring base: %p, cmds: %p, huge: %p", ringbuf->buffer, ringbuf->buffer->cmds, ringbuf->buffer->huge);
    INIT_LIST_HEAD(&ringbuf->free_list);
    INIT_LIST_HEAD(&ringbuf->used_list);

    rc = pthread_mutex_init(&ringbuf->mutex, NULL);
    if (rc < 0) {
        Log_error("[init] pthread_mutex_init return -1\n");
        goto error;
    }

    //初始化命令区域
    for (i = 0; i < CMD_DEPTH; i++) {
        cmd = &(ringbuf->buffer->cmds[i]);
        cmd->state = STATE_FREE;

        rc = pthread_cond_init(&cmd->cond, NULL);
	    if (rc < 0) {
		    Log_error("[init] Fail to init phread_cond\n");
		    goto error;
	    }

    	rc = pthread_mutex_init(&cmd->mutex, NULL);
        if (rc < 0) {
            Log_error("[init] Fail to init phread_mutex\n");
            goto error;
        }

        cidx = malloc(sizeof(struct cmnd_idx));
        if (cidx == NULL) {
           Log_error("[init] Fail malloc cmnd_idx\n");
           goto error;
        }
        cidx->idx = i;
        cidx->cmd = cmd;
        list_add_tail(&cidx->free, &ringbuf->free_list);

    }

    close(fd);
    return ringbuf;
error:
    free(ringbuf);

    close(fd);
    return NULL;
}

int destroy(struct ringbuffer *ringbuf)
{
    struct cmnd_idx *cidx = NULL;
	if (NULL != ringbuf) {
        while (!list_empty(&ringbuf->used_list)) {
	    	cidx = list_first_entry(&ringbuf->used_list, struct cmnd_idx, used);
	    	list_del(&cidx->used);
	    	free(cidx);
            cidx = NULL;
	    }

	    while (!list_empty(&ringbuf->free_list)) {
	    	cidx = list_first_entry(&ringbuf->free_list, struct cmnd_idx, free);
	    	list_del(&cidx->free);
	    	free(cidx);
            cidx = NULL;
	    }

		if (NULL != ringbuf->buffer) {
		    Log_debug("[destroy] size:%u\n", (unsigned int)(ringbuf->size));
			munmap(ringbuf->buffer, ringbuf->size);
		}

		//删除共享内存文件
        remove(ringbuf->shm_file);

		free(ringbuf);
	}

    return 0;
}

struct cmnd * add(struct ringbuffer *rbuf, void *buf, uint32_t length, int64_t offset,
                uint32_t type, uint64_t seq, uint32_t *idx)
{
    struct cmnd_idx *cidx = NULL;
    struct cmnd *cmd = NULL;
    struct list_head *item = NULL;

    do {
        if (length <= CMD_COMMON_LEN) {
            pthread_mutex_lock(&rbuf->mutex);
			item = rbuf->free_list.next;
			if (item == NULL) {  //空闲链表没有元素
			    pthread_mutex_unlock(&rbuf->mutex);
				goto wait;
			}
            cidx = container_of(item, struct cmnd_idx, free);
            list_del(&cidx->free); //脱离空闲链表
			list_add_tail(&cidx->used, &rbuf->used_list); //加入使用链表表尾
			pthread_mutex_unlock(&rbuf->mutex);
			cmd = cidx->cmd;
			*idx = cidx->idx;
			cmd->state = STATE_SEND;
		} else {
			pthread_mutex_lock(&rbuf->mutex);
			cmd = rbuf->buffer->huge;  //4个common命令组成一个huge命令
			if (cmd->state != STATE_FREE) { //huge命令是否被使用
				pthread_mutex_unlock(&rbuf->mutex);
				goto wait;
			}

			cmd->state = STATE_SEND;
			pthread_mutex_unlock(&rbuf->mutex);
			*idx = CMD_HUGE_IDX;
		}

		//根据参数填充cmnd命令
		cmd->length = length;
		cmd->seq = seq;
		cmd->type = type;
		cmd->offset = offset;

		//eprintf("add cmd idx:%u, state:%u, length:%u, seq:%llu, type:%u, offset:%lld", *idx,
		//				cmd->state, cmd->length, (long long unsigned)cmd->seq, cmd->type, (long long int)cmd->offset);

		if (cmd->type == TypeWrite) {
			memcpy(cmd->data, buf, length); //将数据拷贝到共享内存中
		}
		break;
wait:
		//wait for a minite
		sched_yield();
		Log_debug("add cmd sched_yield\n");
	} while(1);

	return cmd;
}

int del(struct ringbuffer *rbuf, uint32_t idx)
{
	struct cmnd_idx *cidx = NULL;
	struct cmnd *cmd = NULL;
	uint8_t flag = 0;

	if (idx != CMD_HUGE_IDX) {
		pthread_mutex_lock(&rbuf->mutex);
		list_for_each_entry(cidx, &rbuf->used_list, used) {
			if (idx == cidx->idx) {
				flag = 1;
				break;
			}
		}

		if (flag == 0) {
			Log_error("[del] Fatal error idx:%u not in used list\n", idx);
			pthread_mutex_unlock(&rbuf->mutex);
			return -1;
		}

		cidx->cmd->state = STATE_FREE;
		list_add_tail(&cidx->free, &rbuf->free_list);
		list_del(&cidx->used);
		pthread_mutex_unlock(&rbuf->mutex);
	} else {
		pthread_mutex_lock(&rbuf->mutex);
		cmd = get(rbuf, idx);
		cmd->state = STATE_FREE;
		pthread_mutex_unlock(&rbuf->mutex);
	}

	return 0;
}

struct cmnd *get(struct ringbuffer *rbuf, uint32_t idx)
{
	return &(rbuf->buffer->cmds[idx]);
}

