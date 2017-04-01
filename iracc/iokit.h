/*
 * iokit.h
 *
 *  Created on: 2017年2月7日
 *      Author: shawn
 */

#ifndef IOKIT_H_
#define IOKIT_H_

#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

//////////////////////////////////////////////////////////////////
// The poll wrapper
typedef enum{
	io_state_idle = 0,
	io_state_read,
	io_state_write,
	io_state_error
}io_state;

typedef void (*io_callback)(int,io_state);
int io_init();
int io_add(int fd, io_callback callback);
int io_remove(int fd);
int io_run();
int io_shutdown();

//////////////////////////////////////////////////////////////////
// The Buffered IOReader Class
struct IOReader;
struct IOWriter;

typedef void (*io_read_callback)(uint8_t*,size_t);
typedef void (*io_write_callback)(size_t);
typedef int (*io_method)(struct IOReader*);
typedef int (*io_write_method)(struct IOWriter*);

struct IOReader{
	int fd;
	uint8_t* buffer;
	size_t bufferLen;
	bool bufferRetained;
	io_method fnRead; 		// the read implementation
	io_method fnRun;
	io_method fnFlush;
	io_method fnClose;
	io_read_callback callback;	// the callback then something read

	// internal part
	size_t maxBufferLen;
	time_t timeout;				// read timeout
	time_t lastRead;
}Reader;

struct IOWriter{
	int fd;
	char* buffer;
	size_t bufferLen;
	io_write_method fnWrite; 		// the read implementation
	io_write_callback callback;		// the callback then something read

	// internal part
	size_t maxBufferLen;
}Writer;

#if 0 // - TODO - encapsulate the OPEN logic
#define IO_OPEN(x,fd,cb)  //io_open((struct IOReader*)x,fd,cb)
void io_open(struct IOReader *reader, int fd, void callback);
#endif

#define IO_READ(x) ((((struct IOReader *)(x))->fnRead)?((struct IOReader *)(x))->fnRead((struct IOReader *)(x)):-1)
#define IO_CLOSE(x) ((((struct IOReader *)(x))->fnClose)? ((struct IOReader *)x)->fnClose((struct IOReader *)x):-1)
#define IO_FLUSH(x) ((((struct IOReader *)(x))->fnFlush)? ((struct IOReader *)x)->fnFlush((struct IOReader *)x):-1)
#define IO_RUN(x) ((((struct IOReader *)(x))->fnRun)?((struct IOReader *)x)->fnRun((struct IOReader *)x):-1)

#define IO_MAKE_LINE_READER(x,fd,cb) io_make_line_reader((struct IOReader *)x,fd,cb)
void io_make_line_reader(struct IOReader *reader, int fd,  void* readercb);

#define IO_MAKE_STREAM_READER(x,fd,cb,to) io_make_stream_reader((struct IOReader *)x,fd,cb,to)
void io_make_stream_reader(struct IOReader *reader, int fd, void* readercb, time_t readtimeout);

#endif /* IOKIT_H_ */
