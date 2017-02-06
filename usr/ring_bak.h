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

#ifndef __SHARED_RING_BUFFER__
#define __SHARED_RING_BUFFER__

#include <pthread.h>
#include <stdint.h>

#define CMD_DEPTH 16
#define CMD_MASK (CMD_DEPTH - 1)
#define CMD_DATA_SIZE 20971520 //20M
#define MAGIC 0x654321

#define STATE_FINISH   0x0
#define STATE_SEND     0x1
#define STATE_HANDLE   0x2

enum uint32_t {
	TypeRead     = 0x12,
	TypeWrite    = 0x13,
	TypeOK       = 0x80,
	TypeError    = 0x81,
	TypeEOF      = 0x82,
	TypeTimeout  = 0x83
};

#pragma pack(1)

struct cmnd {
    uint32_t state;   //命令状态
    uint32_t length;  //命令数据长度
    uint64_t seq;     //命令序号
    uint32_t type;    //命令类型
    uint32_t rtype;   //命令返回类型
    uint32_t off;     //命令在数据区域的偏移
    int64_t  offset;  //命令在磁盘的偏移

    pthread_cond_t  cond; //命令完成通知
    pthread_mutex_t mutex;
};

struct ring {
    uint32_t magic;              //魔数
    struct cmnd cmds[CMD_DEPTH]; //命令区域
    char data[0];                //数据区域
};

struct ringbuffer {
    uint32_t sendix; //下一个发送命令所在数组的编号
    uint32_t recvix; //下一个完成命令所在数据的编号
    uint32_t restix; //命令区域空闲命令数量
    uint32_t head;  //数据区域可写起始位置
    uint32_t tail;  //数据区域可写结束为止
    uint32_t skip;  //为了连续内存空间数据区域跳过的尾部区域大小
    uint32_t size;  //共享内存区域大小
    uint32_t datasize; //数据区域大小
    uint32_t full;  //数据区域满
    pthread_mutex_t mutex; //保护ring结构
    struct ring *buffer;
};

struct ringbuffer * alloc(uint32_t size, char *path);
int destroy(struct ringbuffer *ringbuf);
struct cmnd * add(struct ringbuffer *rbuf, void *buf, uint32_t length, int64_t offset,
                uint32_t type, uint64_t seq, uint32_t *idx);
int del(struct ringbuffer *rbuf, uint32_t idx);
struct cmnd *get(struct ringbuffer *rbuf, uint32_t idx);


#define RING_DEL_ITER(rbuf, cmd)          \
    for((cmd) = &((rbuf)->buffer->cmds[(rbuf)->recvix]); (rbuf)->restix > 0; \
        (cmd) = &((rbuf)->buffer->cmds[(rbuf)->recvix]))

#endif /* __SHARED_RING_BUFFER__ */
