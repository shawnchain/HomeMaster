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

#ifndef CPU_BIG_ENDIAN
#define CPU_BIG_ENDIAN 0

#if CPU_BIG_ENDIAN

#define HI_BYTE(x) x >> 8 & 0xff
#define LO_BYTE(x) x & 0xff
#define MAKE_WORD(hi,lo) (hi << 8 & 0xff00) | (lo & 0xff)
#else

#define HI_BYTE(x) x & 0xff
#define LO_BYTE(x) x >> 8 & 0xff
#define MAKE_UINT16(hi,lo) (uint16_t)((lo << 8 & 0xff00) | (hi & 0xff))
#endif
#endif

typedef struct{
	uint8_t devAddr;
	uint8_t funCode;
	uint16_t regAddr;
	uint16_t regCount;
	uint8_t *payload;
	uint16_t payloadLen;
	uint16_t crc;
}ModbusFrame;

typedef struct{
	uint8_t addr;
	uint8_t code;
	uint16_t reg;
	uint16_t regCount;
	uint16_t crc;
}ModbusRequest;

typedef struct{
	uint8_t addr; 			// from addr
	uint8_t code; 			// func code
	union{					// union for different response types
		struct{
			uint8_t dataLen;
			uint8_t data[256];
		}payload;
		struct{
			uint8_t reg_addr[2];
			uint8_t reg_value[2];
		}ack;
	};
	uint16_t crc;
}ModbusResponse;


void modbus_init(uint8_t devAddr);

void modbus_run();

ModbusFrame* modbus_alloc_frame(size_t payloadSize);
void modbus_free_frame(ModbusFrame* frame);

ModbusRequest* modbus_alloc_request(uint8_t code, uint16_t reg);

void modbus_put16(ModbusFrame *frame, uint16_t u16);
void modbus_putc(ModbusFrame *frame, uint8_t u8);

ModbusResponse* modbus_recv(uint8_t *bytes, size_t len);

bool modbus_send(ModbusRequest *request, int fd, void* callback);
#endif /* MODBUS_H_ */
