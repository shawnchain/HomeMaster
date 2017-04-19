/*
 * databus.h
 *
 *  Created on: 2017年3月30日
 *      Author: shawn
 */

#ifndef IRACC_DATABUS_H_
#define IRACC_DATABUS_H_

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#define DATABUS_ACCESS_MODE_BINARY 0
#define DATABUS_ACCESS_MODE_STRING 1 << 0
#define DATABUS_ACCESS_MODE_POP 1 << 1

int databus_init(const char* sharedFilePath,bool createFlag);

int databus_shutdown();

int databus_put_in(uint8_t* data, size_t len);
int databus_get_in(uint8_t* data, size_t *len, uint8_t mode);

int databus_put_out(uint8_t* data, size_t len);
int databus_get_out(uint8_t* data, size_t *len,uint8_t mode);

void databus_test();
#endif /* IRACC_DATABUS_H_ */
