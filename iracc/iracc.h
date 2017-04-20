/*
 * gsm.h
 *
 *  Created on: 2017年2月5日
 *      Author: shawn
 */

#ifndef IRACC_H_
#define IRACC_H_

#include <stdlib.h>
#include <time.h>

typedef void (*DeviceCallback)(const char*, size_t);

#define IRACC_OP_SUCCESS 0
#define IRACC_OP_FAILURE -1

int iracc_init(const char* devname, int32_t baudrate, DeviceCallback callback);
int iracc_shutdown();

int iracc_run();

int iracc_read_gateway_status();
int iracc_read_internal_unit_connection();
int iracc_read_internal_unit_status(uint8_t IUID);

int iracc_get_status(char* buf, size_t len);

struct InternalUnitCommand;
int iracc_push_command(struct InternalUnitCommand *cmd);

void iracc_test();

#endif /* IRACC_H_ */
