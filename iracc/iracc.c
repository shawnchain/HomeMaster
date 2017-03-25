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
	device_state_close = 0,
	device_state_open,
	device_wait_for_response,
	//state_init_request,
	//state_ready,
	//state_closing, // mark for closing
}DeviceState;

#define MAX_IO_ERROR_COUNT 10

#define WAIT_RESPONSE_TIMEOUT 3
#define IRACC_DEFAULT_ADDRESS 0x01

typedef struct {
	struct IOReader reader;
	char devname[32];
	int32_t  baudrate;
	DeviceState state;
	DeviceCallback callback;
	uint32_t rxcount;
	uint8_t errcount;
	time_t last_reopen;

	ModbusRequest *currentRequest;
	time_t last_request_sent;
}IRACC;

IRACC iracc;


int iracc_init(const char* devname, int32_t baudrate, DeviceCallback callback){
	// init context
	bzero(&iracc,sizeof(IRACC));
	// copy the parameters
	strncpy(iracc.devname,devname,31);
	iracc.baudrate = baudrate;
	iracc.callback = callback;
	// open device
	iracc_open();
	//init modbus with IRACC default address
	modbus_init(IRACC_DEFAULT_ADDRESS);
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

/*
 * 00 01
 */
static void _handle_gateway_status(uint8_t *data, size_t len){
	if(len == 2){
		uint16_t status = MAKE_UINT16(data[0],data[1]);
		if(status == 1){
			INFO("IRACC device is ready");
		}else{
			INFO("IRACC device is NOT ready");
		}
	}
}


/*
 * handles the IRACC(modbus) response. Called when stream read timeout(300ms)
 */
static inline void _handle_iracc_response(uint8_t *data,size_t len){
	if(len == 0) return;

	ModbusResponse *resp = modbus_recv(data,len);
	if(!resp){
		return;
	}

	if(!iracc.currentRequest){
		WARN("Unexpected response from 0x%02x, code: 0x%02x, payloadLength: %d. current request is null",resp->addr, resp->code,resp->payload.dataLen);
		goto exit;
	}
	if(iracc.currentRequest->addr != resp->addr){
		WARN("Unexpected response from 0x%02x, code: 0x%02x, payloadLength: %d. current request is null",resp->addr, resp->code,resp->payload.dataLen);
		goto exit;
	}
	// dispatch to corresponding handlers
	switch(iracc.currentRequest->reg){
	case REG_30001:
		// handles the gateway status query
		_handle_gateway_status(resp->payload.data,resp->payload.dataLen);
		break;
	default:
		break;
	}
exit:
	if(iracc.currentRequest){
		free(iracc.currentRequest);
		iracc.currentRequest = NULL;
	}
	if(resp){
		free(resp);
	}
}


static void _on_stream_read(uint8_t* data, size_t len){
	//receive a line from IRACC
	switch(iracc.state){
	case device_state_open:
		DBG("RECV: %.*s",len,data);
		_handle_iracc_response(data,len);
		break;
	default:
		break;
	}
	return ;
}

// TODO - check if currentRequest exists and free it when time out
void _iracc_check_response_timeout(){
	if(iracc.currentRequest == NULL){
		return;
	}
	if(time(NULL) - iracc.last_request_sent > WAIT_RESPONSE_TIMEOUT){
		// wait response timeout
		INFO("wait IRACC response timeout!");
		free(iracc.currentRequest);
		iracc.currentRequest = NULL;
	}
}

int iracc_run(){
	time_t t = time(NULL);

	switch(iracc.state){
	case device_state_close:
		// re-connect
		if(t - iracc.last_reopen > 10/*config.tnc[0].current_reopen_wait_time*/){
			//config.tnc[0].current_reopen_wait_time += 30; // increase the reopen wait wait time
			DBG("reopening IRACC port...");
			iracc_open();
		}
		break;
	case device_state_open:
		//dispatch to the io reader runloop;
		IO_RUN(&iracc.reader);
		_iracc_check_response_timeout();
		break;
	default:
		break;
	}

	return 0;
}

static int iracc_open(){
	if(iracc.state == device_state_open) return 0;
	iracc.last_reopen = time(NULL);

	int fd = serial_port_open(iracc.devname, iracc.baudrate);
	if(fd < 0){
		return -1;
	}
	iracc.state = device_state_open;
	IO_MAKE_STREAM_READER(&iracc.reader,fd,_on_stream_read,300 /*ms to timeout*/);
	INFO("IRACC port \"%s\" opened, baudrate=%d, fd=%d",iracc.devname,iracc.baudrate,fd);

	// set unblock and select
	serial_port_set_nonblock(fd,1);
	io_add(fd,_on_io_callback);

	return 0;
}

static int iracc_close(){
	if(iracc.state == device_state_close) return 0;

	if(iracc.reader.fd > 0){ // TODO - move to reader->fnClose();
		io_remove(iracc.reader.fd);
		IO_CLOSE(&iracc.reader);
	}
	iracc.state = device_state_close;
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

static inline void _iracc_send(ModbusRequest *req){
	if(modbus_send(req,iracc.reader.fd,NULL)){
		iracc.currentRequest = req;
		iracc.last_request_sent = time(NULL);
	}else{
		free(req);
	}
}

/*
 * >> 01 04 00 00 00 01 31 CA
 * << 01 04 02 00 01 78 F0
 */
void iracc_read_gateway_status(){
	if(iracc.currentRequest){
		ERROR("current request is not null, operation aborted: [read_gateway_status]");
		return;
	}
	ModbusRequest *req = modbus_alloc_request(0x04/*read*/,REG_30001/*the status*/);
	_iracc_send(req);
}
