/*
 * modbus.h
 *
 *  Created on: 2017年3月23日
 *      Author: shawn
 */

#ifndef MODBUS_H_
#define MODBUS_H_

#include <unistd.h>
#include <stdint.h>
#include "stdbool.h"
#include "object.h"

#define HI_BYTE(x) (uint8_t)(x >> 8 & 0xff)
#define LO_BYTE(x) (uint8_t)(x & 0xff)
#define MAKE_UINT16(hi,lo) (uint16_t)((hi & 0xff) << 8 | (lo & 0xff))

#define MODBUS_CODE_READ 0x04
#define MODBUS_CODE_PRESET 0x06
#define MODBUS_CODE_PRESET_MULTI 0x10

typedef struct{
	Object object;
	uint8_t addr;
	uint8_t code;
	uint16_t reg;

	uint8_t regValue[32];	// for query request, this field containing the req count to query
	uint8_t regValueCount;  // for query request, this field is fixed to 2
	uint16_t crc;
}ModbusRequest;

typedef struct{
	uint8_t addr; 			// from addr
	uint8_t code; 			// func code
	union{					// union for different response types
		struct{				// for query response
			uint8_t dataLen;
			uint8_t data[256];
		}payload;
		struct{				// for command response
			uint8_t regAddr[2];
			uint8_t regValue[2];
		}ack;
	};
	uint16_t crc;
}ModbusResponse;

typedef struct {
	uint8_t slaveAddress;
}ModbusCtx;

typedef void (*ModbusSendCallback)(ModbusRequest*);
typedef void (*ModbusReceiveCallback)(ModbusRequest*, ModbusResponse*);

/*
 * Initialize the modbus
 */
void modbus_init(uint8_t devAddr,int fd,ModbusSendCallback onSend, ModbusReceiveCallback onReceive);

/*
 * Main runloop
 */
void modbus_run();

/*
 * Close
 */
int modbus_close();

int modbus_enqueue_request(ModbusRequest *req);

ModbusRequest* modbus_alloc_request(uint8_t code, uint16_t reg);
//ModbusResponse* modbus_recv(uint8_t *bytes, size_t len);
//bool modbus_send(ModbusRequest *request, int fd, void* callback);

#endif /* MODBUS_H_ */
