/*
 * iSCSI Safe Logging and Tracing Library
 *
 * Copyright (C) 2004 Dmitry Yusupov, Alex Aizman
 * maintained by open-iscsi@googlegroups.com
 *
 * circular buffer code based on log.c from dm-multipath project
 *
 * heavily based on code from log.c:
 *   Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>,
 *   licensed under the terms of the GNU GPL v2.0,
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * See the file COPYING included with this distribution for more details.
 */

#ifndef LOG_H
#define LOG_H

#include <sys/sem.h>
#include <syslog.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>





#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

union semun {
	int val;
	struct semid_ds *buf;
	unsigned short int *array;
	struct seminfo *__buf;
};

#define LOG_SPACE_SIZE 16384
#define MAX_MSG_SIZE 256

extern int log_daemon;
extern int log_level;

struct logmsg {
	short int prio;
	void *next;
	char *str;
};

struct logarea {
	int empty;
	int active;
	void *head;
	void *tail;
	void *start;
	void *end;
	char *buff;
	int semid;
	union semun semarg;
};

extern int log_print_level;
extern char *log_name;

/*
#define Log_debug(fmt, args...) \
do { \
    if (LOG_DEBUG <= log_print_level) \
    { \
        struct timeval tv; \
        gettimeofday (&tv, NULL); \
        struct tm *RetTime = localtime( &tv.tv_sec); \
        char buffer[40]; \
        vsprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d.%06ld %s: " fmt,RetTime->tm_year + 1900,RetTime->tm_mon + 1,RetTime->tm_mday \
            ,RetTime->tm_hour,RetTime->tm_min,RetTime->tm_sec,tv.tv_usec,,log_name); \
        vfprintf(stderr, "%s" fmt, (const char *)buffer[0], ##args); \
        fflush(stderr); \
    } \
} \
while (0)
    */


#define Log_debug(fmt, args...) \
do { \
    if (LOG_DEBUG <= log_print_level) \
    { \
        struct timeval tv; \
        gettimeofday (&tv, NULL); \
        struct tm *RetTime = localtime( &tv.tv_sec); \
         fprintf(stderr, "%04d-%02d-%02dT%02d:%02d:%02d.%06ld %s %s: [%s %s %d] " fmt,RetTime->tm_year + 1900,RetTime->tm_mon + 1,RetTime->tm_mday \
            ,RetTime->tm_hour,RetTime->tm_min,RetTime->tm_sec,tv.tv_usec,"debug",log_name,__FILE__, __FUNCTION__, __LINE__,##args); \
         fflush(stderr); \
    } \
} \
while (0)


#define Log_info(fmt, args...) \
do { \
    if (LOG_INFO <= log_print_level) \
    { \
        struct timeval tv; \
        gettimeofday (&tv, NULL); \
        struct tm *RetTime = localtime( &tv.tv_sec); \
         fprintf(stderr, "%04d-%02d-%02dT%02d:%02d:%02d.%06ld %s %s: [%s %s %d] " fmt,RetTime->tm_year + 1900,RetTime->tm_mon + 1,RetTime->tm_mday \
            ,RetTime->tm_hour,RetTime->tm_min,RetTime->tm_sec,tv.tv_usec,"info",log_name,__FILE__, __FUNCTION__, __LINE__,##args); \
         fflush(stderr); \
    } \
} \
while (0)



#define Log_warning(fmt, args...) \
do { \
    if (LOG_WARNING <= log_print_level) \
    { \
        struct timeval tv; \
        gettimeofday (&tv, NULL); \
        struct tm *RetTime = localtime( &tv.tv_sec); \
         fprintf(stderr, "%04d-%02d-%02dT%02d:%02d:%02d.%06ld %s %s: [%s %s %d] " fmt,RetTime->tm_year + 1900,RetTime->tm_mon + 1,RetTime->tm_mday \
            ,RetTime->tm_hour,RetTime->tm_min,RetTime->tm_sec,tv.tv_usec,"warning",log_name,__FILE__, __FUNCTION__, __LINE__,##args); \
         fflush(stderr); \
    } \
} \
while (0)

#define Log_error(fmt, args...) \
do { \
    if (LOG_ERR <= log_print_level) \
    { \
        struct timeval tv; \
        gettimeofday (&tv, NULL); \
        struct tm *RetTime = localtime( &tv.tv_sec); \
         fprintf(stderr, "%04d-%02d-%02dT%02d:%02d:%02d.%06ld %s %s: [%s %s %d] " fmt,RetTime->tm_year + 1900,RetTime->tm_mon + 1,RetTime->tm_mday \
            ,RetTime->tm_hour,RetTime->tm_min,RetTime->tm_sec,tv.tv_usec,"error",log_name,__FILE__,__FUNCTION__, __LINE__, ##args); \
         fflush(stderr); \
    } \
} \
while (0)






extern int log_init (char * progname, int size, int daemon, int debug);
extern void log_close (void);
extern void dump_logmsg (void *);
extern void log_warning(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern void log_error(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern void log_debug(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

#ifdef NO_LOGGING
#define eprintf(fmt, args...)						\
do {									\
	fprintf(stderr, "%s: " fmt, program_name, ##args);		\
} while (0)

#define dprintf(fmt, args...)						\
do {									\
	if (debug)							\
		fprintf(stderr, "%s %d: " fmt,				\
			__FUNCTION__, __LINE__, ##args);		\
} while (0)
#else
#define eprintf(fmt, args...)						\
do {	\
	log_error("[%s %s %d] " fmt,__FILE__, __FUNCTION__, __LINE__, ##args);	\
} while (0)

#define dprintf(fmt, args...)						\
do {									\
	if (unlikely(is_debug))						\
		log_debug("%s(%d) " fmt, __FUNCTION__, __LINE__, ##args); \
} while (0)
#endif

#endif	/* LOG_H */
