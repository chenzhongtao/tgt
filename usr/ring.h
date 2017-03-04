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

#include "list.h"

#define CMD_DEPTH 16
#define CMD_MASK (CMD_DEPTH - 1)
#define CMD_DATA_SIZE 20971520 //20M
#define CMD_COMMON_LEN 1310720 //1.25M
#define CMD_HUGE_LEN   4194304 //4M
#define CMD_HUGE_IDX   99999
#define MAGIC 0x654321

#define STATE_FREE     0x0
#define STATE_SEND     0x1
#define STATE_HANDLE   0x2

enum uint32_t {
	TypeRead     = 0x12,
	TypeWrite    = 0x13,
	TypeOK       = 0x80,
	TypeError    = 0x81,
	TypeEOF      = 0x82,
	TypeTimeout  = 0x83,
	TypeClose    = 0x86
};

#pragma pack(1)

struct cmnd {
    uint32_t state;   //命令状态
    uint32_t length;  //命令数据长度
    uint64_t seq;     //命令序号
    uint32_t type;    //命令类型
    uint32_t rtype;   //命令返回类型
    int64_t  offset;  //命令在磁盘的偏移
    char     data[CMD_COMMON_LEN];

    pthread_cond_t  cond; //命令完成通知
    pthread_mutex_t mutex;
};

struct cmnd_idx {
    uint32_t idx;          //命令所在的数组下标
    struct list_head free; //链入ringbuffer中的空闲链表
    struct list_head used; //链入ringbuffer中的使用链表
    struct cmnd *cmd;      //指向共享内存对应命令的地址
};

struct ring {
    uint32_t magic;               //魔数
    struct cmnd cmds[CMD_DEPTH];  //common命令区域
    struct cmnd huge[0];         //huge命令区域
};

struct ringbuffer {
    uint32_t sendix; //下一个发送命令所在数组的编号
    uint32_t recvix; //下一个完成命令所在数据的编号
    uint32_t restix; //命令区域空闲命令数量
    uint32_t size;   //共享内存区域大小
    struct ring *buffer;
    struct list_head free_list; //命令空闲链表
    struct list_head used_list; //命令使用链表
    char *shm_file;
    pthread_mutex_t mutex;      //保护ringbuffer结构
};

struct ringbuffer *init(char *shm_file);
int destroy(struct ringbuffer *ringbuf);
struct cmnd * add(struct ringbuffer *rbuf, void *buf, uint32_t length, int64_t offset,
                uint32_t type, uint64_t seq, uint32_t *idx);
int del(struct ringbuffer *rbuf, uint32_t idx);
struct cmnd *get(struct ringbuffer *rbuf, uint32_t idx);

#endif /* __SHARED_RING_BUFFER__ */
