/*
 * iokit.c
 *
 *  Created on: 2017年2月7日
 *      Author: shawn
 */

#include "iokit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <assert.h>
#include <time.h>

#include "utils.h"

//////////////////////////////////////////////////////////////////
// Simple poll wrapper

#define io_fds_len 8 // FIXED IO FDs
typedef struct io_fd_t {
	int fd;
	io_callback cb;
} io_fd;

static io_fd io_fds[io_fds_len];
static int maxfd = -1;

int io_init() {
	int i = 0;
	for (i = 0; i < io_fds_len; i++) {
		io_fds[i].fd = -1;
		io_fds[i].cb = 0;
	}
	return 0;
}

int io_shutdown(){
#if 1
	//just remove all fds from reactor
	int i = 0;
	for (i = 0; i < io_fds_len; i++) {
		int fd = io_fds[i].fd;
		if(fd >0){
			DBG("clean leaked fd %d",fd);
			close(fd);
			io_fds[i].fd = -1;
			io_fds[i].cb = 0;
		}
	}
	maxfd = -1;
#endif
	DBG("IOKit shutdown");
	return 0;
}

int io_add(int fd, io_callback callback) {
	int i = 0;
	for (i = 0; i < io_fds_len; i++) {
		if (io_fds[i].fd == fd) {
			// all ready there,
			return i;
		} else if (io_fds[i].fd < 0) {
			io_fds[i].fd = fd;
			io_fds[i].cb = callback;
			if (fd > maxfd) {
				maxfd = fd;
			}
			DBG("Add fd %d to poll list, maxfd is %d", fd, maxfd);
			return i;
		}
	}
	return -1;
}

int io_remove(int fd) {
	int i = 0, j = 0;
	for (i = 0; i < io_fds_len; i++) {
		if (io_fds[i].fd == fd) {
			io_fds[i].fd = -1;
			io_fds[i].cb = 0;
			if (maxfd == fd) {
				maxfd = 0;
				// get the maxfd
				for (j = 0; j < io_fds_len; j++) {
					if (io_fds[j].fd > maxfd) {
						maxfd = io_fds[j].fd;
					}
				}
			}
			DBG("Remove fd %d from poll list, maxfd is %d", fd, maxfd);
			return i;
		}
	}

	return -1;
}

int io_run() {
	fd_set rset, wset, eset;
	struct timeval timeo;
	int rc;
	int i;
	int fd;

	FD_ZERO(&rset);
	FD_ZERO(&wset);
	FD_ZERO(&eset);

	for (i = 0; i < io_fds_len; i++) {
		fd = io_fds[i].fd;
		if (fd >= 0) {
			FD_SET(fd, &rset);
			FD_SET(fd, &wset);
			FD_SET(fd, &eset);
		}
	}

	timeo.tv_sec = 0;
	timeo.tv_usec = 500000; // 500ms

	rc = select(maxfd + 1, &rset, &wset, &eset, &timeo);
	if (rc < 0) {
		fprintf(stderr, "*** select(): %s.\n", strerror(errno));
		return -1;
	} else if (rc > 0) {
		// got ready
		for (i = 0; i < io_fds_len; i++) {
			fd = io_fds[i].fd;
			if (fd >= 0 && FD_ISSET(fd, &rset)
					&& io_fds[i].cb > 0) {
				io_fds[i].cb(fd, io_state_read);
			}
			if (fd >= 0 && FD_ISSET(fd, &wset)
					&& io_fds[i].cb > 0) {
				io_fds[i].cb(fd, io_state_write);
			}
			if (fd >= 0 && FD_ISSET(fd, &eset)
					&& io_fds[i].cb > 0) {
				io_fds[i].cb(fd, io_state_error);
			}
		}
	} else {
		// idle
		for (i = 0; i < io_fds_len; i++) {
			if (io_fds[i].fd >= 0 && io_fds[i].cb > 0) {
				io_fds[i].cb(io_fds[i].fd, io_state_idle);
			}
		}
	}

	usleep(50000); // force sleep 50ms as write select is always returns true.
	return 0;
}

/////////////////////////////////////////////////////////////////////////
// IO Kit
static int _io_fn_run(struct IOReader *reader);
static int _io_fn_close(struct IOReader *reader);
static int _io_fn_flush(struct IOReader *reader);
static int _io_fn_read_line(struct IOReader *reader);
static int _io_fn_read_stream_timeout(struct IOReader *reader);

#define DEFAULT_READ_BUFFER_SIZE 8192

#if 0 - TODO
static int _io_default_runloop(int fd ,io_state state){
	return -1;
}
void io_open(struct IOReader *reader, int fd, io_callback cb){
	io_add(fd,cb?cb:_io_default_runloop);
}
#endif

void io_make_line_reader(struct IOReader *reader, int fd,  void* readercb){
	bzero(reader, sizeof(struct IOReader));
	reader->fd = fd;
	reader->buffer = malloc(DEFAULT_READ_BUFFER_SIZE);
	reader->bufferRetained = true;
	reader->maxBufferLen = DEFAULT_READ_BUFFER_SIZE;
	reader->fnRead = _io_fn_read_line;
	reader->fnRun = _io_fn_run;
	reader->fnFlush = _io_fn_flush;
	reader->fnClose = _io_fn_close;
	reader->callback = readercb;
}

void io_make_stream_reader(struct IOReader *reader, int fd, void* readercb, time_t readtimeout) {
	bzero(reader, sizeof(struct IOReader));
	reader->fd = fd;
	reader->buffer =  malloc(DEFAULT_READ_BUFFER_SIZE);;
	reader->bufferRetained = true;
	reader->maxBufferLen = DEFAULT_READ_BUFFER_SIZE;
	reader->fnRead = _io_fn_read_stream_timeout;
	reader->fnRun = _io_fn_run;
	reader->fnFlush = _io_fn_flush;
	reader->fnClose = _io_fn_close;
	reader->callback = readercb;
	reader->timeout = readtimeout;
}

static int _io_fn_read_line(struct IOReader *reader) {
	// read data into buffer and callback when CR or LF is met
	if (reader->fd < 0)
		return -1;
	bool flush = false;
	int rc = 0;
	char c = 0;
	while ((rc = read(reader->fd, &c, 1)) > 0) {
		if (c == '\r' || c == '\n') {
			flush = true;
		} else {
			flush = false;
			reader->buffer[reader->bufferLen] = c;
			reader->bufferLen++;
			if (reader->bufferLen == (reader->maxBufferLen - 1)) {
				DBG("read buffer full!");
				// we're full!
				reader->buffer[reader->bufferLen] = 0;
				reader->bufferLen--; // not including the \0
				flush = true;
			}
		}
		if (flush && reader->bufferLen > 0 && reader->callback) {
			reader->buffer[reader->bufferLen] = 0;
			reader->callback(reader->buffer, reader->bufferLen);
			reader->bufferLen = 0;
			flush = false;
		}
	}

	if (rc < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			rc = 0;
		} else {
			ERROR("*** io_readline read() error %d: %s", rc, strerror(errno));
		}
	}
	return rc;
}

static int _io_fn_flush(struct IOReader *reader) {
	if (reader->bufferLen > 0 && reader->callback > 0) {
		reader->buffer[reader->bufferLen] = 0;
		reader->callback(reader->buffer, reader->bufferLen);
		reader->bufferLen = 0;
	}
	return 0;
}

static int _io_fn_read_stream_timeout(struct IOReader *reader) {
	if (reader->fd < 0){
		//DBG("reader->fd is invalid");
		return -1;
	}

	int bytesRead =
			read(reader->fd, (reader->buffer + reader->bufferLen),
					(reader->maxBufferLen - reader->bufferLen - 1) /*buffer available*/);
	//DBG("_io_fn_read_stream_timeout: %d bytes read",bytesRead);
	if (bytesRead > 0) {
		reader->bufferLen += bytesRead;
		reader->lastRead = get_time_milli_seconds();

		// flush buffer if full or wait timeout
		if (reader->bufferLen == (reader->maxBufferLen - 1)) {
			_io_fn_flush(reader);
		}
	}

	if (bytesRead < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			bytesRead = 0;
		} else {
			ERROR("*** _io_fn_read_stream_timeout read() %d, error: %s", bytesRead,
					strerror(errno));
		}
	}
	return bytesRead;
}

static int _io_fn_run(struct IOReader *reader) {
	if (reader->fd < 0)
		return -1;
	if (reader->timeout > 0) {
		size_t t = get_time_milli_seconds();
		if (t - reader->lastRead > reader->timeout) {
			_io_fn_flush(reader);
		}
	}
	return 0;
}

static int _io_fn_close(struct IOReader *reader) {
	if(reader->fd < 0){
		return -1;
	}

	if(reader->fd >0){ // what if fd == 0 ?
		close(reader->fd);
	}
	// free internal buffer
	if(reader->buffer && reader->bufferRetained){
		free(reader->buffer);
		reader->buffer = 0;
		reader->bufferRetained = false;
	}
	bzero(reader, sizeof(struct IOReader));
	reader->fd = -1;
	//DBG("IOFile closed");
	return 0;
}
