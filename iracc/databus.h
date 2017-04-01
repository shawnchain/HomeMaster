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

int databus_init(const char* sharedFilePath,bool createFlag);

int databus_shutdown();

int databus_put(uint8_t* data, size_t len);

int databus_get(uint8_t* data, size_t *len);

void databus_test();
#endif /* IRACC_DATABUS_H_ */
