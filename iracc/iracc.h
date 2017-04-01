/*
 * gsm.h
 *
 *  Created on: 2017年2月5日
 *      Author: shawn
 */

#ifndef IRACC_H_
#define IRACC_H_

#include <stdlib.h>

#define REG_GATEWAY_STATUS 0x0000 // Gateway Status

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

typedef void (*DeviceCallback)(const char*, size_t);

int iracc_init(const char* devname, int32_t baudrate, DeviceCallback callback);
int iracc_shutdown();

int iracc_run();

int iracc_read_gateway_status();
int iracc_read_internal_unit_connection();
int iracc_read_internal_unit_status(uint8_t IUID);

void iracc_test();

#endif /* IRACC_H_ */
