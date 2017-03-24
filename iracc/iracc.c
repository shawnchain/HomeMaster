/*
 * iracc.c
 *
 *  Created on: 2017年2月5日
 *      Author: shawn
 */


#include "iokit.h"

#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#include <stdint.h>

#include <strings.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"
#include "serial_port.h"
#include "log.h"
#include "config.h"
#include "iracc.h"
#include "modbus.h"

static int iracc_open();
static int iracc_close();
static int iracc_reading(int fd);

typedef enum{
	state_close = 0,
	state_open,
	//state_init_request,
	//state_ready,
	//state_closing, // mark for closing
}DeviceState;

#define MAX_IO_ERROR_COUNT 10

typedef struct {
	struct IOReader reader;
	char devname[32];
	int32_t  baudrate;
	DeviceState state;
	DeviceCallback callback;
	uint32_t rxcount;
	uint8_t errcount;
	time_t last_reopen;
}IRACC;

IRACC iracc;

int iracc_init(const char* devname, int32_t baudrate, DeviceCallback callback){
	bzero(&iracc,sizeof(IRACC));

	// copy the parameters
	strncpy(iracc.devname,devname,31);
	iracc.baudrate = baudrate;
	iracc.callback = callback;

	iracc_open();
	return 0;
}

static void _on_io_callback(int fd, io_state state){
	switch(state){
		case io_state_read:
			iracc_reading(fd);
			break;
		case io_state_error:
			INFO("Polled error, closing port...");
			iracc_close();
			break;
		case io_state_idle:
			//DBG("tnc idle");
			break;
		default:
			break;
	}
}

static inline void _parse_iracc_frame(const char* message,size_t len){
	if(len == 0) return;
	//TODO - parse the modbus frame first

}


static void _on_stream_read(uint8_t* data, size_t len){
	//receive a line from IRACC
	switch(iracc.state){
	case state_open:
		DBG("RECV: %.*s",len,data);
		_parse_iracc_frame((const char*)data,len);
		break;
	default:
		break;
	}
	return ;
}

int iracc_run(){
	time_t t = time(NULL);

	switch(iracc.state){
	case state_close:
		// re-connect
		if(t - iracc.last_reopen > 10/*config.tnc[0].current_reopen_wait_time*/){
			//config.tnc[0].current_reopen_wait_time += 30; // increase the reopen wait wait time
			DBG("reopening IRACC port...");
			iracc_open();
		}
		break;
	case state_open:
		// io reader runloop;
		IO_RUN(&iracc.reader);
		break;
	default:
		break;
	}

	return 0;
}

static int iracc_open(){
	if(iracc.state == state_open) return 0;
	iracc.last_reopen = time(NULL);

	int fd = serial_port_open(iracc.devname, iracc.baudrate);
	if(fd < 0){
		return -1;
	}
	iracc.state = state_open;
	IO_MAKE_STREAM_READER(&iracc.reader,fd,_on_stream_read,300 /*ms*/);
	INFO("IRACC port \"%s\" opened, baudrate=%d, fd=%d",iracc.devname,iracc.baudrate,fd);

	// set unblock and select
	serial_port_set_nonblock(fd,1);
	io_add(fd,_on_io_callback);

	return 0;
}

static int iracc_close(){
	if(iracc.state == state_close) return 0;

	if(iracc.reader.fd > 0){ // TODO - move to reader->fnClose();
		io_remove(iracc.reader.fd);
		IO_CLOSE(&iracc.reader);
	}
	iracc.state = state_close;
	iracc.errcount = 0;
	iracc.rxcount = 0;
	INFO("IRACC port closed.");
	return 0;
}

static int iracc_reading(int fd){
	if(fd != iracc.reader.fd) return -1;
	int rc = IO_READ(&iracc.reader);
	if(rc < 0 ){
		// RC = 0 means nothing to read.
		if(++iracc.errcount >= MAX_IO_ERROR_COUNT){
			INFO("too much receiving error encountered %d, closing port...",iracc.errcount);
			iracc_close();
		}
		return -1;
	}else if(rc == 0){
		//DBG("IRACC is idle");
	}
	iracc.errcount = 0;
	return 0;
}

