/*
 * utils.h
 *
 *  Created on: 2016年8月29日
 *      Author: shawn
 */

#ifndef SRC_UTILS_H_
#define SRC_UTILS_H_

#include "log.h"

#include <sys/types.h>
#include <stddef.h>
#include <netdb.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#ifdef __APPLE__
#include <sys/filio.h>
#endif
#include <stdbool.h>
#include <stdio.h>

#include <arpa/inet.h>

#define countof(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Misc definitions */
#ifndef NULL
#define NULL  (void *)0
#endif
#ifndef EOF
#define	EOF   (-1)
#endif

#define SWAP16(s) ((s<<8) | (s>>8))
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define TO_BIG_ENDIAN_U16(s) SWAP16(s)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define TO_BIG_ENDIAN_U16(s) s
#else
#error Unsupported byte order
#endif

struct sockaddr_inx {
	union {
		struct sockaddr sa;
		struct sockaddr_in in;
		struct sockaddr_in6 in6;
	};
};

#define port_of_sockaddr(s) ((s)->sa.sa_family == AF_INET6 ? (s)->in6.sin6_port : (s)->in.sin_port)
#define addr_of_sockaddr(s) ((s)->sa.sa_family == AF_INET6 ? (void *)&(s)->in6.sin6_addr : (void *)&(s)->in.sin_addr)
#define sizeof_sockaddr(s)  ((s)->sa.sa_family == AF_INET6 ? sizeof((s)->in6) : sizeof((s)->in))

int resolve_host(const char *hostname_port_pair, struct sockaddr_inx *sa);

static inline int set_nonblock(int sockfd) {
	if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFD, 0) | O_NONBLOCK) == -1)
		return -1;
	return 0;
}

//void hexdump(void *d, size_t len);
void hexdump(void *d, size_t len, int direction);
void stringdump(void *d, size_t len);


int do_daemonize(void);

static inline size_t bytes_available(int fd){
	int bytes_avail = 0;
	return ioctl(fd, FIONREAD, &bytes_avail);
	return bytes_avail;
}

time_t get_time_milli_seconds();

#endif /* SRC_UTILS_H_ */
