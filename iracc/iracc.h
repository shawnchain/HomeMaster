/*
 * gsm.h
 *
 *  Created on: 2017年2月5日
 *      Author: shawn
 */

#ifndef IRACC_H_
#define IRACC_H_

#include <stdlib.h>

#define REG_30001  0x0000		// Gateway Status
#define REG_GATEWAY_STATUS 0x0000

#define REG_30002  0x0001		// Connection Status
#define REG_30003  0x0002
#define REG_30004  0x0003
#define REG_30005  0x0004

#define REG_32001	0x07D0		// S01 Status

#define REG_32007	0x07D6		// S02 Status

#define REG_32013	0x07DC		// S03 Status

#define REG_42001	0x07D0
#define REG_42004	0x07D3
#define REG_42007	0x07D6

typedef void (*DeviceCallback)(const char*, size_t);

int iracc_init(const char* devname, int32_t baudrate, DeviceCallback callback);

int iracc_run();

void iracc_read_gateway_status();
#endif /* IRACC_H_ */
