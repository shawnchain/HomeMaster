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
#include <stdbool.h>

#include <strings.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

#include "utils.h"
#include "serial_port.h"
#include "log.h"
#include "config.h"
#include "iracc.h"
#include "modbus.h"
#include "utils.h"
#include "databus.h"

#include "iracc_cmd.h"

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
	read_state_init = 0,
	read_state_start,
	read_state_complete,
}ReadTaskState;

typedef enum{
	write_state_init = 0,
	write_state_start,
	write_state_complete,
}WriteTaskState;

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

	// reading IU parameters
	uint16_t readIntervalSeconds;
	uint16_t readWaitTimeoutSeconds;
	time_t lastReadTaskTime;		 // start time of the read-iu-status task
	time_t lastReadRequestTime; // start time of the read-iu-status request in one read task
	ReadTaskState readingState;
	uint8_t readingUnitIndex; // the current polling unit index [0..connectedUnitCount]

	// writing IU commands
	InternalUnitCommand writeCommands[64];
	uint8_t	writeCommandCount;
	uint8_t writingCommandIndex;

	uint16_t writeIntervalSeconds;
	uint16_t writeWaitTimeoutSeconds;
	time_t lastWriteTaskTime;		 		// start time of the write-iu-status task
	time_t lastWriteRequestTime; 			// start time of the write-iu-status request in one read task
	WriteTaskState writingState;

	// modbus communication
	//ModbusRequest *currentRequest;
	//ModbusRequest *queuedRequest;
	//ModbusResponse *currentResponse;
	//time_t last_request_sent;
}IRACC;

////////////////////////////////////////////////////////////////////
// IRACC MODBUS REGISTERS
#define REG_GW_STATUS 0x0000 // Gateway Status
#define REG_IU_CONNECTION 0x0001 // Connection Status
#define REG_30003  0x0002
#define REG_30004  0x0003
#define REG_30005  0x0004

#define REG_IU_STATUS_START 0X07D0 // Internal Unit Status
#define REG_IU01_STATUS  0x07D0
#define REG_32001	0x07D0		// IU01 Status
#define REG_32007	0x07D6		// IU02 Status
#define REG_32013	0x07DC		// IU03 Status

#define REG_42001	0x07D0
#define REG_42004	0x07D3
#define REG_42007	0x07D6


////////////////////////////////////////////////////////////////////
// Internal Macros

#define MAX_IO_ERROR_COUNT 10
#define IRACC_DEFAULT_ADDRESS 0x01

#define IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START 0x07D0
#define IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_END 0x0950
#define IRACC_INTERNAL_UNIT_STATUS_REG_COUNT 6
#define UNIT_ID(reg) ((reg - IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START) / 6)


#define IRACC_INTERNAL_UNIT_PRESET_REG_ADDR_START 0x07D0
#define IRACC_INTERNAL_UNIT_PRESET_REG_ADDR_END 0x088D
#define IRACC_INTERNAL_UNIT_PRESET_REG_COUNT 3
#define WRITE_UNIT_ID(reg) ((reg - IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START) / 3)

#define IRACC_READ_IU_IDLE() (iracc.readingState == read_state_init && iracc.readingUnitIndex == 0)
#define IRACC_WRITE_IU_IDLE() (iracc.writingState == write_state_init && iracc.writingCommandIndex == 0)
#define IRACC_CURRENT_COMMAND() ((iracc.writeCommandCount > 0)?(&iracc.writeCommands[iracc.writingCommandIndex]):NULL)

#define DEFAULT_POLLING_INTERVAL 8
#define DEFAULT_POLLING_WAITTIMEOUT 1

/////////////////////////////////////////////////////////////////////
// Internal Methods

static int _iracc_open();
static int _iracc_close();
static void _iracc_update_shared_data();
static int _iracc_print_status(char* buf, size_t bufLen);

static void _modbus_received(ModbusRequest *req, ModbusResponse *resp);
static void _iracc_task_initialize();
static void _iracc_task_read_iu_status();
static void _iracc_task_write_iu_commands();

static int _handle_gateway_status_response(ModbusResponse *resp);
static int _handle_internal_unit_connection_response(ModbusResponse *resp);
static InternalUnitStatus* _handle_internal_unit_status_response(uint8_t unitId, ModbusResponse *resp);

static int _iracc_write_internal_unit_command(InternalUnitCommand *cmd);
static void _handle_internal_unit_command_response(InternalUnitCommand *cmd, ModbusResponse *resp);

IRACC iracc;

int iracc_init(const char* devname, int32_t baudrate, DeviceCallback callback){
	// init context
	bzero(&iracc,sizeof(IRACC));

	// setup defaults
	iracc.readIntervalSeconds = DEFAULT_POLLING_INTERVAL;
	iracc.readWaitTimeoutSeconds = DEFAULT_POLLING_WAITTIMEOUT;
	iracc.writeIntervalSeconds = DEFAULT_POLLING_INTERVAL * 2;
	iracc.writeWaitTimeoutSeconds = DEFAULT_POLLING_WAITTIMEOUT;

	// copy the parameters
	strncpy(iracc.devname,devname,31);
	iracc.baudrate = baudrate;
	iracc.callback = callback;

	// open device
	_iracc_open();

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
	// call the modbus runloop first
	modbus_run();

	time_t t = time(NULL);
	switch(iracc.state){
	case device_state_close:
		// just re-open the device
		if(t - iracc.last_reopen > 10/*config.iracc[0].current_reopen_wait_time*/){
			//config.iracc[0].current_reopen_wait_time += 30; // increase the reopen wait wait time
			DBG("reopening IRACC port...");
			_iracc_open();
		}
		break;
	case device_state_open:
		_iracc_task_initialize();
		break;
	case device_state_ready:
		// polling the input command
		_iracc_task_write_iu_commands();

		// polling the IU status
		_iracc_task_read_iu_status();
		break;
	default:
		break;
	}

	return 0;
}

int iracc_get_status(char* buf, size_t len){
	return _iracc_print_status(buf,len);
}

int iracc_push_command(InternalUnitCommand *cmd){
	int ret = -1;
	if(iracc.writeCommandCount < (sizeof(iracc.writeCommands) / sizeof(InternalUnitCommand))){
		//iracc.writeCommands[iracc.writeCommandCount++] = iuc;
		memcpy(&iracc.writeCommands[iracc.writeCommandCount],cmd,sizeof(InternalUnitCommand));
		iracc.writeCommandCount++;
		ret = 0;
	}
	return ret;
}

static void _modbus_receive_error(){
	// something wrong, increase the error count;
	if(++iracc.errcount >= MAX_IO_ERROR_COUNT){
		INFO("too much receiving error encountered %d, closing port...",iracc.errcount);
		_iracc_close();
	}
}
/*
 * callback from modbus on response received
 */
static void _modbus_received(ModbusRequest *req, ModbusResponse *resp){
	if(iracc.state == device_state_open){
		// under initialize state, we will issue REG_GATEWAY_STATUS and REG_IU_CONNECTION queries
		int result = -1;
		if(req->reg == REG_GW_STATUS){
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

		if(req->code == MODBUS_CODE_READ && req->reg >= IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START && req->reg <= IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_END){
			// handle the internal unit status
			uint8_t unitid = UNIT_ID(req->reg);
			InternalUnitStatus *s = _handle_internal_unit_status_response(unitid, resp);
			if(s){
				// NOTE - store the received unit status
				memcpy(iracc.connectedUnitStatus + unitid,s,sizeof(InternalUnitStatus));
				DBG("unit=%x, mode=%x, power=%x, wind=%x, i_temp=%.1f, p_temp=%.1f",s->unitId, s->workMode, s->powerOn, s->windLevel,s->interiorTemerature,s->presetTemperature);
				free(s);
			}
		}else if((req->code == MODBUS_CODE_PRESET || req->code == MODBUS_CODE_PRESET_MULTI ) && req->reg >= IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START && req->reg <= IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_END){
			// preset single register for one unit
			DBG("command response 0x%02x 0x%02x 0x%02x",resp->addr,resp->code,resp->payload.dataLen);
			_handle_internal_unit_command_response(IRACC_CURRENT_COMMAND(),resp);
		}else{
			INFO("Unknown response 0x%02x 0x%02x 0x%02x",resp->addr,resp->code,resp->payload.dataLen);
		}
	}else{
		INFO("***illegal iracc.state: %d, got modbus data 0x%02x 0x%02x 0x%02x",iracc.state,resp->addr,resp->code,resp->payload.dataLen);
	}
}

/////////////////////////////////////////////////////////////////////
// Interanl implementations

/**
 * Initialize the module states by checking adapter status and the IU connections
 */
static void _iracc_task_initialize(){
	// perform initialize after device is open
	switch(iracc.initState){
	case init_check_status:
		// send gateway status check request
		DBG("checking gateway status");
		if(iracc_read_gateway_status() == IRACC_OP_SUCCESS){
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
		if(iracc_read_internal_unit_connection() == IRACC_OP_SUCCESS){
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

static void _iracc_task_write_iu_commands(){
	if(iracc.connectedUnitCount == 0 || iracc.state != device_state_ready){
		// not ready
		return;
	}
	if(!IRACC_READ_IU_IDLE()){
		// busy reading, do nothing.
		return;
	}
	if(IRACC_WRITE_IU_IDLE()/*iracc.writingState == write_state_init && iracc.writingCommandIndex == 0*/){
		/*
		if(time(NULL) - iracc.lastWriteTaskTime < iracc.writeIntervalSeconds){
			return;
		}
		*/
		// check shared memory for received commands
		char buf[512];
		size_t len = sizeof(buf);
		databus_get_in((uint8_t*)buf,&len,DATABUS_ACCESS_MODE_STRING | DATABUS_ACCESS_MODE_POP);
		if(len == 0) return;

		// parse the received command string "f1=v1,f2=v2" and fill up the iracc.commands
		if(iracc_cmd_parse(buf,len) != IRACC_OP_SUCCESS){
			INFO("parse write command failed, %.*s",len,buf);
			return;
		}
		INFO("start write commands to internal units");
	}

	switch(iracc.writingState){
	case write_state_init:
		if(iracc.writingCommandIndex > 0 &&  iracc.writingCommandIndex >= iracc.writeCommandCount){
			// all commands are proceed, we're done.
			iracc.writingState = write_state_complete;
			break;
		}
		InternalUnitCommand *cmd = IRACC_CURRENT_COMMAND();
		uint8_t _currentUnitId = (cmd->unitId);
		DBG("writing command[%d] for unit[%d]",iracc.writingCommandIndex, _currentUnitId);
		iracc.lastWriteRequestTime = time(NULL);
		if(_iracc_write_internal_unit_command(cmd) == IRACC_OP_SUCCESS){
			iracc.writingState = write_state_start;
		}else{
			WARN("Error write command[%d] for unit[%d]",iracc.writingCommandIndex, _currentUnitId);
			iracc.writingCommandIndex++; // move to the next command
		}
		break;

	case write_state_start:{
		if(iracc.writingCommandIndex > 0 &&  iracc.writingCommandIndex >= iracc.writeCommandCount){
			iracc.writingState = write_state_complete;
			break;
		}
		InternalUnitCommand *cmd = IRACC_CURRENT_COMMAND(); 		// assert cmd is not NULL
		// check if response received or timeout
		if(cmd->responseReceived){
			// the status slot[pollingUnitIndex] is filled, which means we did receive the unit status.
			DBG("write command[%d] on unit[%d] success",iracc.writingCommandIndex,cmd->unitId);
			iracc.writingCommandIndex++; // move to the next
			iracc.writingState = write_state_init;
		}else if(time(NULL) - iracc.lastWriteRequestTime > iracc.writeWaitTimeoutSeconds){
			// write timeout, move to the next one
			WARN("write command[%d] on unit[%d] timeout",iracc.writingCommandIndex,cmd->unitId);
			iracc.writingCommandIndex++;
			iracc.writingState = write_state_init;
		}
		break;
	}

	case write_state_complete:
		// save status into the shared memory
		DBG("write task completed. Total %d unit command written",iracc.writingCommandIndex);
		iracc.writingCommandIndex = 0;
		iracc.writeCommandCount = 0;
		iracc.writingState = write_state_init;
		iracc.lastWriteTaskTime = time(NULL); // reset task timestamp
		break;
	default:
		break;
	}


	// returns the list of parsed requests.
	// while(has_more_commands()){
	//  iracc_send_command(cmd++);
	// }
}

/**
 *	polling the IU status by sending the read_registry command one by one
 */
static void _iracc_task_read_iu_status(){

	if(iracc.connectedUnitCount == 0 || iracc.state != device_state_ready){
		return;
	}
	if(!IRACC_WRITE_IU_IDLE()){
		// busy writing, do nothing
		return;
	}
	// pause a while between the polling task.
	if(IRACC_READ_IU_IDLE()/*iracc.pollingState == poll_state_init && iracc.pollingUnitIndex == 0*/){
		if(time(NULL) - iracc.lastReadTaskTime < iracc.readIntervalSeconds){
			return;
		}
		INFO("start read internal unit status");
	}

	uint8_t _currentUnitId = iracc.connectedUnitIDs[iracc.readingUnitIndex];
	switch(iracc.readingState){
	case read_state_init:
		if(iracc.readingUnitIndex >= iracc.connectedUnitCount){
			iracc.readingState = read_state_complete;
			break;
		}

		DBG("reading unit[%d]",_currentUnitId);
		iracc.lastReadRequestTime = time(NULL);
		if(iracc_read_internal_unit_status(_currentUnitId) == IRACC_OP_SUCCESS){
			iracc.readingState = read_state_start;
		}else{
			WARN("Error query status for unit id %d",iracc.readingUnitIndex);
			iracc.readingUnitIndex++; // move to the next
		}
		break;
	case read_state_start:{
		if(iracc.readingUnitIndex >= iracc.connectedUnitCount){
			iracc.readingState = read_state_complete;
			break;
		}

		InternalUnitStatus *status = &(iracc.connectedUnitStatus[iracc.readingUnitIndex]);
		// check if data received or timeout
		if(status->unitId == _currentUnitId /*data received if unitId equals current unit idotherwise will be -1 */){
			// the status slot[pollingUnitIndex] is filled, which means we did receive the unit status.
			DBG("read unit[%d] status success",status->unitId);
			iracc.readingUnitIndex++; // move to the next - FIXME - what if read timeout?
			iracc.readingState = read_state_init;
		}else if(time(NULL) - iracc.lastReadRequestTime > iracc.readWaitTimeoutSeconds){
			// polling timeout, move to the next one
			WARN("read unit[%d] status timeout",status->unitId);
			iracc.readingUnitIndex++;
			iracc.readingState = read_state_init;
		}
	}
		break;
	case read_state_complete:
		// save status into the shared memory
		DBG("read task completed, total %d unit status read",iracc.readingUnitIndex);
		//update the data in shared memory
		_iracc_update_shared_data();
		iracc.readingUnitIndex = 0;
		iracc.readingState = read_state_init;
		iracc.lastReadTaskTime = time(NULL);
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

	iracc.readingState = read_state_init;
	iracc.state = device_state_close;
	iracc.initState = init_check_status;
	iracc.errcount = 0;
	iracc.rxcount = 0;
	INFO("IRACC port closed.");
	return 0;
}

static void _iracc_update_shared_data(){
	uint8_t buf[1024];
	int len = _iracc_print_status((char*)buf,sizeof(buf));
	databus_put_out(buf,len);
}

#define STATUS_TEMPLATE_STRING "unit=%d, mode=%s, power=%s, wind=%s, i_temp=%.1f, p_temp=%.1f\n"
static inline int _iracc_print_status(char* buf, size_t bufLen){
	//FIXME - check the buf len to avoid buffer overflow.
	int len = 0;
	for(int i = 0;i<iracc.connectedUnitCount;i++){
		uint8_t id = iracc.connectedUnitIDs[i];
		InternalUnitStatus *s = &iracc.connectedUnitStatus[id];
		len += sprintf((char*)(buf + len),STATUS_TEMPLATE_STRING,
				s->unitId,
				iracc_cmd_get_workmode_value_name(s->workMode),
				iracc_cmd_get_powermode_value_name(s->powerOn),
				iracc_cmd_get_windlevel_value_name(s->windLevel),
				s->interiorTemerature,
				s->presetTemperature);
	}
	return len;
}

/////////////////////////////////////////////////////////////////////
// IRACC modbus operations

/**
 * Get the gateway status
 *
 * >> 01 04 00 00 00 01 31 CA
 * << 01 04 02 00 01 78 F0
 */
int iracc_read_gateway_status(){
	ModbusRequest *req = modbus_alloc_request(MODBUS_CODE_READ/*0x04*/,REG_GW_STATUS/*the status*/);
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
/*
 * handles the gw status response
 * @ModbusResponse the response or NULL if modbus wait timeout.
 */
static int _handle_gateway_status_response(ModbusResponse *resp){
	if(!resp){
		INFO("read gateway status timeout");
		goto failure;
	}

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

failure:
	iracc.initState = init_failure;
	return -1;
}


/**
 * Get the count of connected internal units
 */
int iracc_read_internal_unit_connection(){
	ModbusRequest *req = modbus_alloc_request(MODBUS_CODE_READ/*0x04*/,REG_IU_CONNECTION/*the internal unit connection status*/);
	if(!req) return -1;
	req->regValue[0] = 0x00;
	req->regValue[1] = 0x04; // query for 4 regs, in 8 bytes
	req->regValueCount = 2;
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
	for(int i = 0;i<8;i=i+2){
		// bit mask: 0000 0000 0000 0011
		//           ^^^^ ^^^^ ^^^^ ^^^^
		// device id FEDC BA98 7654 3210
		uint16_t w = data[i] << 8 | data[i+1];
		for(int j = 0;j<16;j++){
			if((w >> j) & 1){
				iracc.connectedUnitIDs[connectedIU++] = ((i / 2) * 16 + j);
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
 *
 * @returns 0 for sucess or -1 for error
 */
int iracc_read_internal_unit_status(uint8_t IUID){
	if(IUID >= 64 /*max 64 internal units supported*/){
		return -1;
	}
	uint16_t regAddr = IRACC_INTERNAL_UNIT_STATUS_REG_ADDR_START + (IUID * IRACC_INTERNAL_UNIT_STATUS_REG_COUNT);
	ModbusRequest *req = modbus_alloc_request(MODBUS_CODE_READ/*0x04*/,regAddr/*the internal unit status registry*/);
	if(!req) return -1;
	req->regValue[0] = 0x00;
	req->regValue[1] = IRACC_INTERNAL_UNIT_STATUS_REG_COUNT/*0x06*/; // query for 6 regs, in 12 bytes
	req->regValueCount = 2;
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

	hexdump(data,len,-1);

	InternalUnitStatus *status = malloc(sizeof(InternalUnitStatus));
	memset(status,0,sizeof(InternalUnitStatus));
	status->unitId = unitId;

	uint8_t i = 0;
	status->windLevel = data[i++]; //(data[i++] >>4 ) & 0x0f; //50 风量hh 风量=0x50&0xf0 10=ll,20=l,30=m,40=h,50=hh.
	status->powerOn = (data[i++] & 1) + 0x60; //00 开关off 开关=0x00&0x01 1=开 0=关, convert to 0x60,0x61
	status->filterCleanupFlag = data[i++];
	status->workMode = (data[i++] & 0x0f);
	status->presetTemperature = ((float)(data[i] * 0x100 + data[i+1])) / 10.0f;
	i+=2;

	status->errorCode = (data[i] << 8 | data[i+1]);
	i+=2;

	//status->errorCode = ntohs(status->errorCode);
	status->interiorTemerature = ((float)(data[i] * 0x100 + data[i+1])) / 10.0f;
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


/*
 * write command to iracc gateway
 */
static int _iracc_write_internal_unit_command(InternalUnitCommand *cmd){
	if(!cmd)
		return -1;

	ModbusRequest *req = NULL;
	uint16_t regAddr = IRACC_INTERNAL_UNIT_PRESET_REG_ADDR_START + (cmd->unitId * IRACC_INTERNAL_UNIT_PRESET_REG_COUNT);
	req = modbus_alloc_request(/*presetValueCount > 1?MODBUS_CODE_PRESET_MULTI:*/MODBUS_CODE_PRESET/*0x10 or 0x06*/,regAddr/*the internal unit status registry*/);
	if(!req) return -1;

	int presetValueCount = 0;
	if(cmd->windLevel > 0){
		req->regValue[0] = cmd->windLevel; // wind level should be 10,20,30,40,50
		req->regValue[1] = 0xFF;
		req->regValueCount = 2;
		presetValueCount++;
	}

	if(cmd->powerOn > 0){		 // power mode value is 0x60 and 0x61
		req->regValue[1] = cmd->powerOn; // power on or off
		if(presetValueCount == 0){
			req->regValue[0] = 0xFF; // no wind
		}
		req->regValueCount = 2;
		presetValueCount++;
	}

	if(cmd->workMode >= 0){
		if(presetValueCount == 0){
			req->regValue[0] = 0x00;
			req->regValue[1] = cmd->workMode; // 00通风|01制热|02制冷|03自动|07除湿|
			req->regValueCount = 2;
		}else{
			//regValue[2] = 0x00;
			//regValue[3] = cmd->workingMode;
			WARN("multi-command is not supprted yet!");
		}
		presetValueCount++;
	}

	if(cmd->presetTemperature > 0){
		uint16_t temp = (cmd->presetTemperature * 10);
		if(presetValueCount == 0){
			req->regValue[0] = temp >> 8; 		// high part
			req->regValue[1] = temp & 0xff; 	// low part
			req->regValueCount = 2;
		}else{
			//regValue[4] = temp >> 8;
			//regValue[5] = temp & 0xff;
			WARN("multi-command is not supprted yet!");
		}
		presetValueCount++;
	}

	if(modbus_enqueue_request(req) != IRACC_OP_SUCCESS){
		ERROR("queued request is full, operation aborted:[_iracc_write_internal_unit_command]");
		return -1;
	}
	DBG("enqueue request 0x%02x 0x%02x 0x%04x",req->addr, req->code,req->reg);
	return 0;
}

static void _handle_internal_unit_command_response(InternalUnitCommand *cmd, ModbusResponse *resp){
	if(!cmd) return;
	uint16_t reg = resp->ack.regAddr[0] << 8 | resp->ack.regAddr[1];
	uint8_t unitId = WRITE_UNIT_ID(reg);
	if(cmd->unitId == unitId){
		cmd->responseReceived = true;
	}else{
		INFO("unit id mismatch in command response. expected %d, got %d in response",cmd->unitId,unitId);
	}
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
