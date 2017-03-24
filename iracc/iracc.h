/*
 * gsm.h
 *
 *  Created on: 2017年2月5日
 *      Author: shawn
 */

#ifndef IRACC_H_
#define IRACC_H_

#include <stdlib.h>

typedef void (*DeviceCallback)(const char*, size_t);

int iracc_init(const char* devname, int32_t baudrate, DeviceCallback callback);

int iracc_run();

#endif /* IRACC_H_ */
