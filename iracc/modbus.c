/*
 * modbus.c
 *
 *  Created on: 2017年3月23日
 *      Author: shawn
 */

#include "modbus.h"
#include "crc16.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "log.h"

typedef struct{
	uint8_t slaveAddress;
}ModbusCtx;

static ModbusCtx modbus;

void modbus_init(uint8_t address) {
	memset(&modbus,0,sizeof(ModbusCtx));
	modbus.slaveAddress = address;
}

void modbus_run() {

}

ModbusFrame* modbus_alloc_frame(size_t payloadSize) {
	ModbusFrame *f = malloc(sizeof(ModbusFrame));
	if (!f) {
		return NULL;
	}
	memset(f, 0, sizeof(ModbusFrame));
	f->crc = CRC_MODBUS_INIT_VAL;
	f->payload = malloc(payloadSize);
	if (!f->payload) {
		// out of memory!
		free(f);
		return NULL;
	}
	memset(f->payload, 0, payloadSize);
	return f;
}

void modbus_free_frame(ModbusFrame* frame) {
	if (frame) {
		if (frame->payload) {
			free(frame->payload);
		}
		free(frame);
	}
}

ModbusRequest* modbus_alloc_request(uint8_t code, uint16_t reg){
	ModbusRequest *req = malloc(sizeof(ModbusRequest));
	if(!req) return NULL;
	memset(req,0,sizeof(ModbusRequest));
	req->addr = modbus.slaveAddress;
	req->code = code;
	req->reg = reg;
	req->regCount = 0x0001;
	return req;
}

static inline void _modbus_putc(ModbusFrame *frame, uint8_t u8){
	frame->payload[frame->payloadLen++] = u8;
	frame->crc = crc16_modbus_update(u8,frame->crc);
}

void modbus_putc(ModbusFrame *frame, uint8_t u8){
	_modbus_putc(frame,u8);
}

/*
 * Modbus data frame is big-endian
 * which means the MSB is sent first
 */
void modbus_put16(ModbusFrame *frame, uint16_t u16){
	// Modbus data frame is big-endian
	_modbus_putc(frame,HI_BYTE(u16));
	_modbus_putc(frame,LO_BYTE(u16));
}

#ifndef MIN
#define MIN(x,y)  ((x<y)?x:y)
#endif
ModbusResponse* modbus_recv(uint8_t *bytes, size_t len){
	//parse the bytes into modbus frame;
	if(len < 4){
		// at least 4 bytes for a valid modbus response
		INFO("Invalid modbus response, len %d",len);
		return 0;
	}
	// check against the CRC
	uint16_t crc = crc16_modbus(bytes,len);
	if(crc != 0){
		WARN("CRC check failed on modbus data, %02x%02x",bytes[len-2],bytes[len-1]);
		return 0;
	}
	ModbusResponse *resp = malloc(sizeof(ModbusResponse));
	if(!resp){
		ERROR("allocate modbus response failed, out of memory");
		return 0;
	}
	memset(resp,0,sizeof(ModbusResponse));
	// JUST COPY THE RECEIVED ARRAY DIRECTLY INTO MODBUS RESPONSE!
	memcpy((uint8_t*)resp,bytes,MIN(len,sizeof(ModbusResponse)));
	return resp;
}

bool modbus_send(ModbusRequest *request, int fd, void* callback){
	uint8_t buf[512];
	uint16_t i = 0;
	buf[i++] = request->addr;
	buf[i++] = request->code;
	buf[i++] = HI_BYTE(request->reg);
	buf[i++] = LO_BYTE(request->reg);
	buf[i++] = HI_BYTE(request->regCount);
	buf[i++] = LO_BYTE(request->regCount);
	uint16_t crc = crc16_modbus(buf,i);
	buf[i++] = HI_BYTE(crc);
	buf[i++] = LO_BYTE(crc);

	// send via the serial port
	// TODO - abstract to IOKit
	ssize_t bytesWritten = 0;
	bytesWritten = write(fd,buf,i);
	if(bytesWritten != i){
		WARN("write may failure, %d of %d bytes written",bytesWritten,i);
	}

	return true;
}

