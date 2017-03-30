/*
 * utils.c
 *
 *  Created on: 2016年8月29日
 *      Author: shawn
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/time.h>

#include <ctype.h>
#include <strings.h>
#include <sys/types.h>
#include <netdb.h>

#include "utils.h"

int resolve_host(const char *hostname_port_pair /*rotate.aprs2.net:14580*/,
		struct sockaddr_inx *sa) {
	struct addrinfo hints, *result;
	char host[51] = "", s_port[10] = "";
	int port = 0, rc;

	if (hostname_port_pair == NULL) {
		return -EINVAL;
	}

	if (sscanf(hostname_port_pair, "%50[^:]:%d", host, &port) == 0) {
		return -EINVAL;
	}
	if (port == 0)
		port = 14580;
	sprintf(s_port, "%d", port);
	if (port <= 0 || port > 65535)
		return -EINVAL;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET; /* Allow IPv4 only */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; /* For wildcard IP address */
	hints.ai_protocol = 0; /* IPPROTO_TCP */
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	if ((rc = getaddrinfo(host, s_port, &hints, &result)))
		return -EAGAIN;

	/* Get the first resolution. */
	memcpy(sa, result->ai_addr, result->ai_addrlen);

	freeaddrinfo(result);

	char s_server_addr[50];
	inet_ntop(sa->sa.sa_family, addr_of_sockaddr(sa), s_server_addr,
			sizeof(s_server_addr));
	DBG("Resolved to %s:%u", s_server_addr, ntohs(port_of_sockaddr(sa)));

	return 0;
}

int do_daemonize(void) {
	pid_t pid;

	if ((pid = fork()) < 0) {
		fprintf(stderr, "*** fork() error: %s.\n", strerror(errno));
		return -1;
	} else if (pid > 0) {
		/* In parent process */
		exit(0);
	} else {
		/* In child process */
		int fd;
		setsid();
		chdir("/tmp");
		if ((fd = open("/dev/null", O_RDWR)) >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);
		}
	}
	return 0;
}

time_t get_time_milli_seconds() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	time_t time_in_mill = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000;
	return time_in_mill;
}

//void hexdump(void *d, size_t len) {
//	hexdump(d,len,-1);
//}

void hexdump(void *d, size_t len, int direction){
	if(len == 0) return;
	unsigned char *s;
	uint16_t i = 0;
	if(direction == 0){
		printf(">>  ");
	}else if(direction == 1){
		printf("<<  ");
	}else{
		printf("--  ");
	}
	for (s = d; len; len--, s++){
		printf("%02x ", *s);
		i++;
		if(i > 0){
			if(i % 24 == 0)
				printf("\n");
			else if(i % 4 == 0)
				printf("  ");
		}
	}
	printf("\n");
}

void stringdump(void *d, size_t len) {
	unsigned char *s;
	printf(
			"=======================================================================\n");
	for (s = d; len; len--, s++)
		printf("%c", *s);
	printf(
			"\n^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
}
