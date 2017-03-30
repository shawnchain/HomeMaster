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
#include "utils.h"

static int _iracc_open();
static int _iracc_close();
static int _iracc_reading(int fd);
static void _iracc_send(ModbusRequest *req);

typedef enum{
	device_state_close = 0,
	device_state_open,
	//device_wait_for_response,
	device_state_init_start,
	device_state_init_complete,
	device_state_ready,
	//state_closing, // mark for closing
}DeviceState;

#define MAX_IO_ERROR_COUNT 10
#define WAIT_RESPONSE_TIMEOUT 3
#define IRACC_DEFAULT_ADDRESS 0x01


#define IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START 0x07D0
#define IRACC_INTERNAL_UNIT_STATUS_REG_COUNT 6

#define UNIT_ID(reg) ((reg - IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START) / 6)

typedef struct {
	// parent IO reader
	struct IOReader reader;
	char devname[32];
	int32_t  baudrate;
	DeviceState state;
	DeviceCallback callback;
	uint32_t rxcount;
	uint8_t errcount;
	time_t last_reopen;

	// IRACC status
	uint8_t connectedUnitMask[8]; // supports up to 64 internal units
	uint8_t connectedUnitCount;

	ModbusRequest *currentRequest;
	ModbusRequest *queuedRequest;
	time_t last_request_sent;
}IRACC;

IRACC iracc;

typedef struct{
	uint8_t unitId;
	uint8_t windLevel;
	uint8_t powerOn;
	uint8_t filterCleanupFlag;
	uint8_t workingMode;
	float presetTemperature;
	uint16_t errorCode;
	float interiorTemerature;
	uint8_t temperatureSensorOK;
}InternalUnitStatus;

static void _iracc_task_check_status();
static bool _iracc_task_process_queued_request();
static void _iracc_task_process_response_timeout();
static void _handle_iracc_response(uint8_t *data,size_t len);
static void _handle_gateway_status(uint8_t *data, size_t len);
static void _handle_internal_unit_connection(uint8_t *data, size_t len);
static InternalUnitStatus* _handle_internal_unit_status(uint8_t unitId, uint8_t *data, size_t len);

int iracc_init(const char* devname, int32_t baudrate, DeviceCallback callback){
	// init context
	bzero(&iracc,sizeof(IRACC));
	// copy the parameters
	strncpy(iracc.devname,devname,31);
	iracc.baudrate = baudrate;
	iracc.callback = callback;
	// open device
	_iracc_open();
	//init modbus with IRACC default address
	modbus_init(IRACC_DEFAULT_ADDRESS);

	// init shared memory for controller info
	return 0;
}

int iracc_shutdown(){
	_iracc_close();
	if(iracc.currentRequest){
		free(iracc.currentRequest);
		iracc.currentRequest = NULL;
	}
	if(iracc.queuedRequest){
		free(iracc.queuedRequest);
		iracc.queuedRequest = NULL;
	}
	return 0;
}

/*
 * Main run-loop of IRACC module
 */
int iracc_run(){
	time_t t = time(NULL);

	switch(iracc.state){
	case device_state_close:
		// just re-open the device
		if(t - iracc.last_reopen > 10/*config.tnc[0].current_reopen_wait_time*/){
			//config.tnc[0].current_reopen_wait_time += 30; // increase the reopen wait wait time
			DBG("reopening IRACC port...");
			_iracc_open();
		}
		break;
	case device_state_open:
		_iracc_task_check_status();
		break;
	case device_state_init_start:
	case device_state_init_complete:
	case device_state_ready:
		break;
	default:
		break;
	}

	// internal tasks
	_iracc_task_process_queued_request();
	_iracc_task_process_response_timeout();
	//dispatch to the io reader runloop;
	IO_RUN(&iracc.reader);

	return 0;
}

/*
 * IO runloop callback
 */
static void _on_io_callback(int fd, io_state state){
	switch(state){
		case io_state_read:
			_iracc_reading(fd);
			break;
		/*
		case io_state_write:
			_iracc_writing(fd);
			break;
		*/
		case io_state_error:
			INFO("Polled error, closing port...");
			_iracc_close();
			break;
		case io_state_idle:
			//DBG("iracc idle");
			break;
		default:
			break;
	}
}

/*
 * callback of IOKit Stream
 */
static void _on_stream_read(uint8_t* data, size_t len){
	//receive a line from IRACC
	switch(iracc.state){
	case device_state_open:
	case device_state_init_start:
	case device_state_init_complete:
	case device_state_ready:
		//DBG("RECV: %.*s",len,data);
		hexdump(data,len,1/*<<*/);
		_handle_iracc_response(data,len);
		break;
	default:
		break;
	}
	return ;
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
	case REG_GATEWAY_STATUS:
		// handles the gateway status query
		_handle_gateway_status(resp->payload.data,resp->payload.dataLen);
		break;
	case REG_CONNECT_STATUS:
		_handle_internal_unit_connection(resp->payload.data,resp->payload.dataLen);
		break;
	default:
	{
		if(iracc.currentRequest->reg >= IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START){
			// handle the internal unit status
			InternalUnitStatus *s = _handle_internal_unit_status(UNIT_ID(iracc.currentRequest->reg), resp->payload.data,resp->payload.dataLen);
			if(s){
				// the status
				DBG("unit %d - power: %d, wind: %d, i_temp: %.1f, p_temp: %.1f",s->unitId, s->powerOn, s->windLevel,s->interiorTemerature,s->presetTemperature);
				free(s);
			}
		}else{
			INFO("Unknown response 0x%02x 0x%02x 0x%02x",resp->addr,resp->code,resp->payload.dataLen);
		}
	}
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

/////////////////////////////////////////////////////////////////////
#pragma mark - Interanl implementations

void _iracc_task_check_status(){
	// check iracc status
	switch(iracc.state){
	case device_state_open:
		// check gateway status
		DBG("state: OPEN->INIT_START");
		iracc_read_gateway_status();
		iracc.state = device_state_init_start;
		break;
	case device_state_init_complete:
		// check connected devices
		DBG("state: INIT_COMPLETE->(iracc_read_internal_unit_connection)->READY");
		iracc_read_internal_unit_connection();
		iracc.state = device_state_ready;
		break;
	default:
		break;
	}
}
/*
 * tasks routine
 * check if currentRequest exists and free it when time out
 */
void _iracc_task_process_response_timeout(){
	if(iracc.currentRequest == NULL || iracc.state == device_state_close){
		return;
	}
	if(time(NULL) - iracc.last_request_sent > WAIT_RESPONSE_TIMEOUT){
		// wait response timeout
		INFO("wait IRACC response timeout!");
		free(iracc.currentRequest);
		iracc.currentRequest = NULL;
		if(iracc.state == device_state_init_start){
			DBG("state: INIT_START->OPEN");
			iracc.state = device_state_open;
		}
	}
}

/*
 * tasks routine
 * process queued requests
 */
static bool _iracc_task_process_queued_request(){
	if(iracc.state == device_state_close){
		return false;
	}
	ModbusRequest *req = iracc.queuedRequest;
	if(req){
		DBG("dequeue request 0x%02x 0x%02x 0x%04x",req->addr, req->code,req->reg);
		iracc.queuedRequest = NULL;
		_iracc_send(req);
	}
	return true;
}

static int _iracc_open(){
	if(iracc.state == device_state_open)return 0;
	iracc.last_reopen = time(NULL);

	int fd = serial_port_open(iracc.devname, iracc.baudrate);
	if(fd < 0){
		return -1;
	}
	iracc.state = device_state_open;
	// set unblock and select
	serial_port_set_nonblock(fd,1);

	io_add(fd,_on_io_callback);
	//IO_OPEN(&iracc.reader,fd,_on_io_callback);

	IO_MAKE_STREAM_READER(&iracc.reader,fd,_on_stream_read,300 /*ms to timeout*/);
	INFO("IRACC port \"%s\" opened, baudrate=%d, fd=%d",iracc.devname,iracc.baudrate,fd);

	return 0;
}

static int _iracc_close(){
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

static int _iracc_reading(int fd){
	if(fd != iracc.reader.fd) return -1;
	int rc = IO_READ(&iracc.reader);
	if(rc < 0 ){
		// RC = 0 means nothing to read.
		if(++iracc.errcount >= MAX_IO_ERROR_COUNT){
			INFO("too much receiving error encountered %d, closing port...",iracc.errcount);
			_iracc_close();
		}
		return -1;
	}else if(rc == 0){
		//DBG("IRACC is idle");
	}
	iracc.errcount = 0;
	return 0;
}

static inline void _iracc_send(ModbusRequest *req){
	// get from queue
	if(modbus_send(req,iracc.reader.fd,NULL)){
		iracc.currentRequest = req;
		iracc.last_request_sent = time(NULL);
	}else{
		free(req);
	}
}

/////////////////////////////////////////////////////////////////////
#pragma mark - IRACC interface operations

/**
 * Get the gateway status
 *
 * >> 01 04 00 00 00 01 31 CA
 * << 01 04 02 00 01 78 F0
 */
int iracc_read_gateway_status(){
	if(iracc.queuedRequest){
		ERROR("queued request is full, operation aborted:[read_gateway_status]");
		return -1;
	}
	ModbusRequest *req = modbus_alloc_request(0x04/*read*/,REG_GATEWAY_STATUS/*the status*/);
	iracc.queuedRequest = req;
	DBG("enqueue request 0x%02x 0x%02x 0x%04x",req->addr, req->code,req->reg);
	return 0;
}
static void _handle_gateway_status(uint8_t *data, size_t len){
	if(len == 2){
		uint16_t status = MAKE_UINT16(data[0],data[1]);
		if(status == 1){
			if(iracc.state == device_state_init_start){
				iracc.state = device_state_init_complete;
			}
			INFO("IRACC device is ready");
		}else{
			INFO("IRACC device is NOT ready");
		}
	}
}

/**
 * Get the count of connected internal units
 */
int iracc_read_internal_unit_connection(){
	if(iracc.queuedRequest){
		ERROR("queued request is full, operation aborted:[read_gateway_status]");
		return -1;
	}
	ModbusRequest *req = modbus_alloc_request(0x04/*read*/,REG_CONNECT_STATUS/*the internal unit connection status*/);
	req->regCount = 0x04; // query for 4 regs, in 8 bytes
	iracc.queuedRequest = req;
	DBG("enqueue request 0x%02x 0x%02x 0x%04x",req->addr, req->code,req->reg);
	return 0;
}
static void _handle_internal_unit_connection(uint8_t *data, size_t len){
	//check internal unit connections
	if(len == 8){
		// valid internal unit status
		uint8_t connectedIU  = 0;
		hexdump(data,len,-1);
		for(int i = 0;i<8;i++){
			uint8_t b = data[i];
			iracc.connectedUnitMask[i] = b;
			for(int j = 0;j<8;j++){
				if((b >> j) & 1){
					connectedIU++;
				}
			}
		}
		iracc.connectedUnitCount = connectedIU;
		INFO("IRACC connected to %d internal units",connectedIU);
	}
}

/*
 * query internal unit status
 * @pram IUID the internal unit id that in range [0 ... 63]
 */
int iracc_read_internal_unit_status(uint8_t IUID){
	if(iracc.queuedRequest){
		ERROR("queued request is full, operation aborted:[iracc_read_internal_unit_status]");
		return -1;
	}
	if(IUID >= 64 /*max 64 internal units supported*/){
		return -1;
	}
	uint16_t regAddr = IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START + (IUID * IRACC_INTERNAL_UNIT_STATUS_REG_COUNT);
	ModbusRequest *req = modbus_alloc_request(0x04/*read*/,regAddr/*the internal unit status registry*/);
	req->regCount = IRACC_INTERNAL_UNIT_STATUS_REG_COUNT/*0x06*/; // query for 6 regs, in 12 bytes
	iracc.queuedRequest = req;
	DBG("enqueue request 0x%02x 0x%02x 0x%04x",req->addr, req->code,req->reg);
	return 0;
}
static InternalUnitStatus* _handle_internal_unit_status(uint8_t unitId, uint8_t *data, size_t len){
	if(len != 12){
		// incorrect data length
		return NULL;
	}

	InternalUnitStatus *status = malloc(sizeof(InternalUnitStatus));
	memset(status,0,sizeof(InternalUnitStatus));
	status->unitId = unitId;

	uint8_t i = 0;
	status->windLevel = (data[i++] >>4 ) & 0x0f; //50 风量hh 风量=0x50&0xf0 10=ll,20=l,30=m,40=h,50=hh.
	status->powerOn = data[i++] & 1; //00 开关off 开关=0x00&0x01 1=开 0=关
	status->filterCleanupFlag = data[i++];
	status->workingMode = (data[i++] & 0x0f);
	status->presetTemperature = ((float)(data[i] * 100 + data[i+1])) / 10.0f;
	i+=2;

	status->errorCode = (data[i] << 8 | data[i+1]);
	i+=2;

	//status->errorCode = ntohs(status->errorCode);
	status->interiorTemerature = ((float)(data[i] * 100 + data[i+1])) / 10.0f;
	i+=2;

	status->temperatureSensorOK = ((data[i] == 0x08) && data[i+1] == 0x00);
	i+=2;
	return status;

	/*
	50 风量hh 风量=0x50&0xf0 10=ll,20=l,30=m,40=h,50=hh.
	00 开关off 开关=0x00&0x01 1=开 0=关
	42 过虑网清洗标志.
	02 制冷模式 模式=0x02&0x0f 0=送风,1=制热,2=制冷,7=除湿
	01 18 设置温度 28 设置温度= (0x01*100+0x18)/10;
	00 00 故障代码
	01 0A 室内温度 26.6 室内温度= (0x01*100+0x0a)/10;
	08 00 温度传感器状态 0x0800 正常 0x0001 异常
	*/
}


void iracc_test(){
	//uint16_t i = 0x0102;
	//DBG("HI(i)= 0x%02x",i >> 8 & 0xff);
	//DBG("HI(i)= 0x%02x",i & 0xff);
	//iracc_read_gateway_status();
	//iracc_read_internal_unit_count();
	//iracc_read_internal_unit_status(0);
	// iracc set iu00 on
	// iracc > set iu00 off
	// iracc list
	// iracc > list

}
