/*
 * VeSpace block device backing store routine
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

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <pthread.h>
#include <limits.h>
#include <ctype.h>
#include <sys/un.h>

#include "list.h"
#include "tgtd.h"
#include "util.h"
#include "log.h"
#include "scsi.h"
#include "bs_thread.h"

#include "vio.h"

struct vio_info {
    struct vio_connection *conn;
    size_t size;
    char   *shm_file;
};

#define LOCATE_VIO_INFO(lu)	((struct vio_info *) \
			((char *)lu + \
			 sizeof(struct scsi_lu) + \
			 sizeof(struct bs_thread_info)) \
                )


static void set_medium_error(int *result, uint8_t *key, uint16_t *asc)
{
    *result = SAM_STAT_CHECK_CONDITION;
    *key = MEDIUM_ERROR;
    *asc = ASC_READ_ERROR;
}

static void bs_vio_request(struct scsi_cmd *cmd)
{
    int ret = 0;
    uint32_t length = 0;
    int result = SAM_STAT_GOOD;
    uint8_t  key = 0;
    uint16_t asc = 0;
    struct vio_info *vio = LOCATE_VIO_INFO(cmd->dev);

    switch (cmd->scb[0]) {
    case WRITE_6:
    case WRITE_10:
    case WRITE_12:
    case WRITE_16:
        length = scsi_get_out_length(cmd);
        //eprintf("write command length:%u, offset:%lu", length, cmd->offset);
        ret = write_at(vio->conn, scsi_get_out_buffer(cmd), length, cmd->offset);
        if (ret) {
            Log_error("[bs_vio_request] fail to write at %lx for %u\n", cmd->offset, length);
            set_medium_error(&result, &key, &asc);
        }
        break;
    case READ_6:
	case READ_10:
	case READ_12:
	case READ_16:
	    length = scsi_get_in_length(cmd);
	    //eprintf("read command length:%u, offset:%lu", length, cmd->offset);
	    ret = read_at(vio->conn, scsi_get_in_buffer(cmd), length, cmd->offset);
	    if (ret) {
            Log_error("fail to read at %lx for %u\n", cmd->offset, length);
            set_medium_error(&result, &key, &asc);
	    }
	    break;
	case SYNCHRONIZE_CACHE:
        Log_debug("cmd->scb[0]: %x(SYNCHRONIZE_CACHE)\n", cmd->scb[0]);
	    break;
    case WRITE_SAME:
	case WRITE_SAME_16:
        Log_debug("cmd->scb[0]: %x(WRITE_SAME)\n", cmd->scb[0]);
		/* WRITE_SAME used to punch hole in file */
		if (cmd->scb[1] & 0x08) {
			Log_debug("WRITE_SAME set UNMAP bit, length:%u, offset:%lu\n",cmd->tl, cmd->offset);
			break;
		}
        Log_debug("WRITE_SAME length:%u, offset:%lu\n",cmd->tl, cmd->offset);
        break;
	default:
	    Log_info("cmd->scb[0]: %x\n", cmd->scb[0]);
	    break;
    }

    //eprintf("io done %p %x %d %u\n", cmd, cmd->scb[0], ret, length);

    scsi_set_result(cmd, result);
    if (result != SAM_STAT_GOOD) {
		Log_error("[bs_vio_request] io error %p %x %d %d %" PRIu64 ", %m\n",
			cmd, cmd->scb[0], ret, length, cmd->offset);
		sense_data_build(cmd, key, asc);
	}
}

static int vio_open(struct vio_info *vio, char *socket_path)
{
	//eprintf("vio_open socket path: %s\n", socket_path);

    vio->conn = new_vio_connection(socket_path, vio->shm_file);
    if (NULL == vio->conn) {
        Log_error("[vio_open] Cannot estibalish connection\n");
        return -1;
    }

    start_response_processing(vio->conn);
    return 0;
}

static int bs_vio_open(struct scsi_lu *lu, char *path, int *fd, uint64_t *size)
{
    struct vio_info *vio = LOCATE_VIO_INFO(lu);
    int ret = vio_open(vio, path);
    if (ret) {
        return ret;
    }

    *size = vio->size;
    return 0;
}

static void bs_vio_close(struct scsi_lu *lu)
{
    if (LOCATE_VIO_INFO(lu)->conn) {
        shutdown_vio_connection(LOCATE_VIO_INFO(lu)->conn);
    }
}

static char *slurp_to_semi(char **p)
{
	char *end = index(*p, ';');
	char *ret;
	int len;

	if (end == NULL)
		end = *p + strlen(*p);
	len = end - *p;
	ret = malloc(len + 1);
	strncpy(ret, *p, len);
	ret[len] = '\0';
	*p = end;
	/* Jump past the semicolon, if we stopped at one */
	if (**p == ';')
		*p = end + 1;
	return ret;
}

static char *slurp_value(char **p)
{
	char *equal = index(*p, '=');
	if (equal) {
		*p = equal + 1;
		return slurp_to_semi(p);
	} else {
		return NULL;
	}
}

static int is_opt(const char *opt, char *p)
{
	int ret = 0;
	if ((strncmp(p, opt, strlen(opt)) == 0) &&
		(p[strlen(opt)] == '=')) {
		ret = 1;
	}
	return ret;
}

static tgtadm_err bs_vio_init(struct scsi_lu *lu, char *bsopts)
{
    struct bs_thread_info *info = BS_THREAD_I(lu);
    char *ssize = NULL;
    size_t size = 0;

    while (bsopts && strlen(bsopts)) {
        if (is_opt("size", bsopts)) {
            ssize = slurp_value(&bsopts);
            size = atoll(ssize);
        }
        if (is_opt("shm", bsopts)) {
            LOCATE_VIO_INFO(lu)->shm_file = slurp_value(&bsopts);
        }
    }

    LOCATE_VIO_INFO(lu)->size = size;
    Log_debug("[bs_vio_init] threads:%d shm file: %s\n", nr_iothreads, LOCATE_VIO_INFO(lu)->shm_file);
    return bs_thread_open(info, bs_vio_request, nr_iothreads);
}

static void bs_vio_exit(struct scsi_lu *lu)
{
    struct bs_thread_info *info = BS_THREAD_I(lu);

    bs_thread_close(info);
    Log_debug("[bs_vio_exit] successfully\n");
}

static struct backingstore_template vio_bst = {
    .bs_name              = "vio",
    .bs_datasize          = sizeof(struct bs_thread_info) + sizeof(struct vio_info),
    .bs_open              = bs_vio_open,
    .bs_close             = bs_vio_close,
    .bs_init              = bs_vio_init,
    .bs_exit              = bs_vio_exit,
    .bs_cmd_submit        = bs_thread_cmd_submit,
    .bs_oflags_supported  = O_SYNC | O_DIRECT | O_RDWR,
};

__attribute__((constructor)) static void register_bs_module(void)
{
    register_backingstore_template(&vio_bst);
}
