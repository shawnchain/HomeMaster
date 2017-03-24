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



void modbus_init() {

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

void modbus_parse(ModbusFrame *frame, uint8_t *bytes, size_t len){
	//TODO parse the bytes into modbus frame;
}


