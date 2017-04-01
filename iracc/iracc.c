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
#include "databus.h"

static int _iracc_open();
static int _iracc_close();

static void _modbus_received(ModbusRequest *req, ModbusResponse *resp);

typedef enum{
	device_state_close = 0,
	device_state_open,
	//device_wait_for_response,
	//device_state_init_start,
	//device_state_init_complete,
	device_state_ready,
	//state_closing, // mark for closing
}DeviceState;


typedef enum{
	init_check_status = 0,
	init_check_status_wait,
	init_check_connection,
	init_check_connection_wait,
	init_success,
	init_failure,
}InitState;

/*
 * Query State
 */
typedef enum{
	poll_state_init = 0,
	poll_state_start,
	poll_state_complete,
}PollState;

#define MAX_IO_ERROR_COUNT 10
#define IRACC_DEFAULT_ADDRESS 0x01

#define IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START 0x07D0
#define IRACC_INTERNAL_UNIT_STATUS_REG_COUNT 6

#define UNIT_ID(reg) ((reg - IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START) / 6)

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

typedef struct {
	// parent IO reader
	char devname[32];
	int32_t  baudrate;
	DeviceState state;
	DeviceCallback callback;
	uint32_t rxcount;
	uint8_t errcount;
	time_t last_reopen;

	InitState initState;

	// IRACC status
	uint8_t connectedUnitCount;
	uint8_t connectedUnitIDs[64];
	InternalUnitStatus connectedUnitStatus[64];

	// polling
	uint16_t pollingIntervalSeconds;
	uint16_t pollingWaitTimeoutSeconds;
	time_t last_polling_task_time;		 // start time of the polling task
	time_t last_polling_query_time; // start time of the polling request in one polling task
	PollState pollingState;
	uint8_t pollingUnitIndex; // the current polling unit index [0..connectedUnitCount]

	// modbus communication
	//ModbusRequest *currentRequest;
	//ModbusRequest *queuedRequest;
	//ModbusResponse *currentResponse;
	//time_t last_request_sent;
}IRACC;

IRACC iracc;


static void _iracc_task_initialize();
static void _iracc_task_polling_iu_status();
static int _handle_gateway_status_response(ModbusResponse *resp);
static int _handle_internal_unit_connection_response(ModbusResponse *resp);
static InternalUnitStatus* _handle_internal_unit_status_response(uint8_t unitId, ModbusResponse *resp);

int iracc_init(const char* devname, int32_t baudrate, DeviceCallback callback){
	// init context
	bzero(&iracc,sizeof(IRACC));

	// setup defaults
	iracc.pollingIntervalSeconds = 5;
	iracc.pollingWaitTimeoutSeconds = 1;

	// copy the parameters
	strncpy(iracc.devname,devname,31);
	iracc.baudrate = baudrate;
	iracc.callback = callback;
	// open device
	_iracc_open();

	// init shared memory for controller info
	return 0;
}

int iracc_shutdown(){
	INFO("IRACC shutdown");
	return _iracc_close();
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
		_iracc_task_initialize();
		break;
	case device_state_ready:
		// polling the IU status
		_iracc_task_polling_iu_status();
		break;
	default:
		break;
	}

	// call the modbus runloop
	modbus_run();

	return 0;
}

static void _modbus_receive_error(){
	// something wrong, increase the error count;
	if(++iracc.errcount >= MAX_IO_ERROR_COUNT){
		INFO("too much receiving error encountered %d, closing port...",iracc.errcount);
		_iracc_close();
	}
}
static void _modbus_received(ModbusRequest *req, ModbusResponse *resp){
	if(iracc.state == device_state_open){
		// initialize state
		int result = -1;
		if(req->reg == REG_GATEWAY_STATUS){
			// handles the gateway status query
			result = _handle_gateway_status_response(resp);
		}else if(req->reg == REG_IU_CONNECTION){
			result = _handle_internal_unit_connection_response(resp);
		}else if(resp != NULL){
			INFO("Unknown response 0x%02x 0x%02x 0x%02x",resp->addr,resp->code,resp->payload.dataLen);
		}
		// something wrong
		if(result == -1){
			_modbus_receive_error();
		}

	}else if(iracc.state == device_state_ready){
		// ready state
		if(resp == NULL){
			DBG("modbus receive timeout");
			_modbus_receive_error();
			return;
		}

		if(req->reg >= IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START){
			// handle the internal unit status
			uint8_t unitid = UNIT_ID(req->reg);
			InternalUnitStatus *s = _handle_internal_unit_status_response(unitid, resp);
			if(s){
				// NOTE - store the received unit status
				memcpy(iracc.connectedUnitStatus + unitid,s,sizeof(InternalUnitStatus));
				DBG("unit %d - power: %d, wind: %d, i_temp: %.1f, p_temp: %.1f",s->unitId, s->powerOn, s->windLevel,s->interiorTemerature,s->presetTemperature);
				free(s);
			}
		}else{
			INFO("Unknown response 0x%02x 0x%02x 0x%02x",resp->addr,resp->code,resp->payload.dataLen);
		}
	}else{
		INFO("***illegal iracc.state: %d, got modbus data 0x%02x 0x%02x 0x%02x",iracc.state,resp->addr,resp->code,resp->payload.dataLen);
	}
}

/////////////////////////////////////////////////////////////////////
#pragma mark - Interanl implementations

/**
 * Initialize the module states by checking adapter status and the IU connections
 */
static void _iracc_task_initialize(){
	// perform initialize after device is open
	switch(iracc.initState){
	case init_check_status:
		// send gateway status check request
		DBG("checking gateway status");
		if(iracc_read_gateway_status() == 0){
			iracc.initState = init_check_status_wait;
		}else{
			iracc.initState = init_failure;
		}
		break;
	case init_check_status_wait:
		// see _handle_gateway_status()
		break;
	case init_check_connection:
		// send request for check gateway status
		DBG("checking IU connections");
		if(iracc_read_internal_unit_connection() == 0){
			iracc.initState = init_check_connection_wait;
		}else{
			iracc.initState = init_failure;
		}
		break;
	case init_check_connection_wait:
		// see _handle_internal_unit_connection()
		break;
	case init_success:
		iracc.state = device_state_ready;
		INFO("IRACC device is ready!");
		break;
	case init_failure:
		INFO("IRACC device initialize failed!");
		iracc.initState = init_check_status;
		_iracc_close(); // close port if initialize failed;
		break;
	default:
		break;
	}
}

/**
 *	polling the IU status by sending the read_registry command one by one
 */
static void _iracc_task_polling_iu_status(){
	if(iracc.connectedUnitCount == 0 || iracc.state != device_state_ready){
		return;
	}

	// pause a while between the polling task.
	if(iracc.pollingState == poll_state_init && iracc.pollingUnitIndex == 0){
		if(time(NULL) - iracc.last_polling_task_time < iracc.pollingIntervalSeconds){
			return;
		}
		DBG("polling task started");
	}

	uint8_t unitId = iracc.connectedUnitIDs[iracc.pollingUnitIndex];

	switch(iracc.pollingState){
	case poll_state_init:
		if(iracc.pollingUnitIndex >= iracc.connectedUnitCount){
			iracc.pollingState = poll_state_complete;
			break;
		}

		DBG("polling unit[%d]",unitId);
		iracc.last_polling_query_time = time(NULL);
		if(iracc_read_internal_unit_status(unitId) == 0){
			iracc.pollingState = poll_state_start;
		}else{
			WARN("Error query status for unit id %d",iracc.pollingUnitIndex);
			iracc.pollingUnitIndex++; // move to the next
		}
		break;
	case poll_state_start:{
		if(iracc.pollingUnitIndex >= iracc.connectedUnitCount){
			iracc.pollingState = poll_state_complete;
			break;
		}

		InternalUnitStatus *status = &(iracc.connectedUnitStatus[iracc.pollingUnitIndex]);
		if(status->unitId == unitId){
			// the status slot[pollingUnitIndex] is filled, which means we did receive the unit status.
			DBG("polled unit[%d] status",status->unitId);
			iracc.pollingUnitIndex++; // move to the next - FIXME - what if read timeout?
		}else if(time(NULL) - iracc.last_polling_query_time > iracc.pollingWaitTimeoutSeconds){
			// polling timeout, move to the next one
			WARN("polling unit[%d] status timeout",status->unitId);
			iracc.pollingUnitIndex++;
			iracc.pollingState = poll_state_init;
		}
	}
		break;
	case poll_state_complete:
		// save status into the shared memory
		DBG("polling task completed. Total %d unit status received",unitId);
		//TODO - dump to the shared memory
		iracc.pollingUnitIndex = 0;
		iracc.pollingState = poll_state_init;
		iracc.last_polling_task_time = time(NULL);
		break;
	default:
		break;
	}
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
	//init modbus on port open
	modbus_init(IRACC_DEFAULT_ADDRESS,fd,NULL,_modbus_received);
	INFO("IRACC port \"%s\" opened, baudrate=%d, fd=%d",iracc.devname,iracc.baudrate,fd);
	return 0;
}

static int _iracc_close(){
	if(iracc.state == device_state_close) return 0;

	modbus_close();

	iracc.pollingState = poll_state_init;
	iracc.state = device_state_close;
	iracc.initState = init_check_status;
	iracc.errcount = 0;
	iracc.rxcount = 0;
	INFO("IRACC port closed.");
	return 0;
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
	ModbusRequest *req = modbus_alloc_request(0x04/*read*/,REG_GATEWAY_STATUS/*the status*/);
	if(!req){
		return -1;
	}
	if(modbus_enqueue_request(req) == -1){
		ERROR("queued request is full, operation aborted:[read_gateway_status]");
		free(req);
		return -1;
	}
	DBG("enqueue request 0x%02x 0x%02x 0x%04x",req->addr, req->code,req->reg);
	return 0;
}

static int _handle_gateway_status_response(ModbusResponse *resp){
	if(resp){
		uint8_t *data = resp->payload.data;
		size_t len = resp->payload.dataLen;
		if(len == 2){
			uint16_t status = MAKE_UINT16(data[0],data[1]);
			if(status == 1){
				iracc.initState = init_check_connection; // go next check
				INFO("gateway respond OK");
				return 0;
			}else{
				INFO("gateway respond negative");
			}
		}
	}else{
		INFO("read gateway status timeout");
	}
	iracc.initState = init_failure;
	return -1;
}

/**
 * Get the count of connected internal units
 */
int iracc_read_internal_unit_connection(){
	ModbusRequest *req = modbus_alloc_request(0x04/*read*/,REG_IU_CONNECTION/*the internal unit connection status*/);
	if(!req) return -1;
	req->regCount = 0x04; // query for 4 regs, in 8 bytes
	if(modbus_enqueue_request(req) == -1){
		ERROR("queued request is full, operation aborted:[read_gateway_status]");
		free(req);
		return -1;
	}
	DBG("enqueue request 0x%02x 0x%02x 0x%04x",req->addr, req->code,req->reg);
	return 0;
}
static int _handle_internal_unit_connection_response(ModbusResponse *resp){
	if(!resp){
		INFO("read internal unit connection timeout");
		goto failure;
	}

	uint8_t *data= resp->payload.data;
	size_t len  = resp->payload.dataLen;
	//check internal unit connections
	if(len != 8){
		goto failure;
	}

	// reset the connected unit data first.
	for(int i = 0;i<64;i++){
		iracc.connectedUnitIDs[i] = -1;
		iracc.connectedUnitStatus[i].unitId = -1;
	}

	uint8_t connectedIU  = 0;
	hexdump(data,len,-1);
	for(int i = 0;i<8;i++){
		uint8_t b = data[i];
		for(int j = 0;j<8;j++){
			if((b >> j) & 1){
				iracc.connectedUnitIDs[connectedIU++] = (i * 8 + j);
			}
		}
	}
	iracc.connectedUnitCount = connectedIU;
	if(connectedIU > 0){
		// debug out the connected IUs
		char buf[512];
		int idx = sprintf(buf,"%d internal units connected, [",connectedIU);
		for(int k = 0;k<connectedIU;k++){
			idx += sprintf(buf + idx,"%d,",iracc.connectedUnitIDs[k]);
		}
		sprintf(buf+idx,"]");
		INFO("%s",buf);
	}else{
		INFO("0 internal units connected");
	}
	// check success
	iracc.initState = init_success;
	return 0;

failure:
	iracc.initState = init_failure;
	return -1;
}

/*
 * query internal unit status
 * @pram IUID the internal unit id that in range [0 ... 63]
 */
int iracc_read_internal_unit_status(uint8_t IUID){
	if(IUID >= 64 /*max 64 internal units supported*/){
		return -1;
	}
	uint16_t regAddr = IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START + (IUID * IRACC_INTERNAL_UNIT_STATUS_REG_COUNT);
	ModbusRequest *req = modbus_alloc_request(0x04/*read*/,regAddr/*the internal unit status registry*/);
	if(!req) return -1;
	req->regCount = IRACC_INTERNAL_UNIT_STATUS_REG_COUNT/*0x06*/; // query for 6 regs, in 12 bytes
	if(modbus_enqueue_request(req) == -1){
		ERROR("queued request is full, operation aborted:[iracc_read_internal_unit_status]");
		return -1;
	}
	DBG("enqueue request 0x%02x 0x%02x 0x%04x",req->addr, req->code,req->reg);
	return 0;
}
static InternalUnitStatus* _handle_internal_unit_status_response(uint8_t unitId, ModbusResponse *resp){
	if(resp == NULL){
		INFO("read internal unit status timeout");
		return NULL;
	}

	uint8_t *data = resp->payload.data;
	size_t len = resp->payload.dataLen;
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
