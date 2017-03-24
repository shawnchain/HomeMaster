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

#ifndef CPU_BIG_ENDIAN
#define CPU_BIG_ENDIAN 0

#if CPU_BIG_ENDIAN

#define HI_BYTE(x) x >> 8 & 0xff
#define LO_BYTE(x) x & 0xff
#define MAKE_WORD(hi,lo) (hi << 8 & 0xff00) | (lo & 0xff)
#else

#define HI_BYTE(x) x & 0xff
#define LO_BYTE(x) x >> 8 & 0xff
#define MAKE_WORD(hi,lo) (lo << 8 & 0xff00) | (hi & 0xff)
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

void modbus_init();
void modbus_run();

ModbusFrame* modbus_alloc_frame(size_t payloadSize);
void modbus_free_frame(ModbusFrame* frame);

void modbus_put16(ModbusFrame *frame, uint16_t u16);
void modbus_putc(ModbusFrame *frame, uint8_t u8);

void modbus_parse(ModbusFrame *frame, uint8_t *bytes, size_t len);

#endif /* MODBUS_H_ */
