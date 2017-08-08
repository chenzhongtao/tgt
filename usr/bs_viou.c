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

#include "viou.h"

struct viou_info {
    struct viou_connection *conn;
    size_t size;
};

#define LOCATE_VIO_INFO(lu)	((struct viou_info *) \
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

static void bs_viou_request(struct scsi_cmd *cmd)
{
    int ret = 0;
    uint32_t length = 0;
    int result = SAM_STAT_GOOD;
    uint8_t  key = 0;
    uint16_t asc = 0;
    struct viou_info *viou = LOCATE_VIO_INFO(cmd->dev);

    switch (cmd->scb[0]) {
    case WRITE_6:
    case WRITE_10:
    case WRITE_12:
    case WRITE_16:
        length = scsi_get_out_length(cmd);
        //Log_debug("write command length:%u, offset:%lu", length, cmd->offset);
        ret = write_at_unix(viou->conn, scsi_get_out_buffer(cmd), length, cmd->offset);
        if (ret) {
            eprintf("fail to write at %lx for %u\n", cmd->offset, length);
            set_medium_error(&result, &key, &asc);
        }
        break;
    case READ_6:
	case READ_10:
	case READ_12:
	case READ_16:
	    length = scsi_get_in_length(cmd);
	    //Log_debug("read command length:%u, offset:%lu", length, cmd->offset);
	    ret = read_at_unix(viou->conn, scsi_get_in_buffer(cmd), length, cmd->offset);
	    if (ret) {
            eprintf("fail to read at %lx for %u\n", cmd->offset, length);
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

    dprintf("io done %p %x %d %u\n", cmd, cmd->scb[0], ret, length);

    scsi_set_result(cmd, result);
    if (result != SAM_STAT_GOOD) {
		eprintf("io error %p %x %d %d %" PRIu64 ", %m\n",
			cmd, cmd->scb[0], ret, length, cmd->offset);
		sense_data_build(cmd, key, asc);
	}
}

static int viou_open(struct viou_info *viou, char *socket_path)
{
	//eprintf("viou_open socket path: %s\n", socket_path);

    viou->conn = new_viou_connection(socket_path);
    if (NULL == viou->conn) {
        eprintf("Cannot estibalish connection");
        return -1;
    }

    start_response_processing_unix(viou->conn);
    return 0;
}

static int bs_viou_open(struct scsi_lu *lu, char *path, int *fd, uint64_t *size)
{
    struct viou_info *viou = LOCATE_VIO_INFO(lu);
    int ret = viou_open(viou, path);
    if (ret) {
        return ret;
    }

    *size = viou->size;
    return 0;
}

static void bs_viou_close(struct scsi_lu *lu)
{
    if (LOCATE_VIO_INFO(lu)->conn) {
        shutdown_viou_connection(LOCATE_VIO_INFO(lu)->conn);
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

static tgtadm_err bs_viou_init(struct scsi_lu *lu, char *bsopts)
{
    struct bs_thread_info *info = BS_THREAD_I(lu);
    char *ssize = NULL;
    size_t size = 0;

    while (bsopts && strlen(bsopts)) {
        if (is_opt("size", bsopts)) {
            ssize = slurp_value(&bsopts);
            size = atoll(ssize);
        }
    }

    LOCATE_VIO_INFO(lu)->size = size;

    return bs_thread_open(info, bs_viou_request, nr_iothreads);
}

static void bs_viou_exit(struct scsi_lu *lu)
{
    struct bs_thread_info *info = BS_THREAD_I(lu);

    bs_thread_close(info);
}

static struct backingstore_template viou_bst = {
    .bs_name              = "viou",
    .bs_datasize          = sizeof(struct bs_thread_info) + sizeof(struct viou_info),
    .bs_open              = bs_viou_open,
    .bs_close             = bs_viou_close,
    .bs_init              = bs_viou_init,
    .bs_exit              = bs_viou_exit,
    .bs_cmd_submit        = bs_thread_cmd_submit,
    .bs_oflags_supported  = O_SYNC | O_DIRECT | O_RDWR,
};

__attribute__((constructor)) static void register_bs_module(void)
{
    register_backingstore_template(&viou_bst);
}

