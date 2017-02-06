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
    uint32_t state;   //����״̬
    uint32_t length;  //�������ݳ���
    uint64_t seq;     //�������
    uint32_t type;    //��������
    uint32_t rtype;   //���������
    uint32_t off;     //���������������ƫ��
    int64_t  offset;  //�����ڴ��̵�ƫ��

    pthread_cond_t  cond; //�������֪ͨ
    pthread_mutex_t mutex;
};

struct ring {
    uint32_t magic;              //ħ��
    struct cmnd cmds[CMD_DEPTH]; //��������
    char data[0];                //��������
};

struct ringbuffer {
    uint32_t sendix; //��һ������������������ı��
    uint32_t recvix; //��һ����������������ݵı��
    uint32_t restix; //�������������������
    uint32_t head;  //���������д��ʼλ��
    uint32_t tail;  //���������д����Ϊֹ
    uint32_t skip;  //Ϊ�������ڴ�ռ���������������β�������С
    uint32_t size;  //�����ڴ������С
    uint32_t datasize; //���������С
    uint32_t full;  //����������
    pthread_mutex_t mutex; //����ring�ṹ
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
