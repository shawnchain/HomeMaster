/*
 * global.h
 *
 *  Created on: 2017年2月5日
 *      Author: shawn
 */

#ifndef CONFIG_H_
#define CONFIG_H_

typedef struct {
	bool in_background;
	char service[128];
	char device[64];
	int32_t baudrate;

	char pid_file[64];
	char log_file[64];
	char shm_file[64];
}AppConfig;

extern AppConfig appConfig;

#endif /* CONFIG_H_ */
