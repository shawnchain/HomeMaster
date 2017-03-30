/*
 * log.h
 *
 *  Created on: 2016年9月20日
 *      Author: shawn
 */

#ifndef SRC_LOG_H_
#define SRC_LOG_H_

#include <sys/types.h>

#define DEBUG_LEVEL 3
#define INFO_LEVEL 2
#define WARN_LEVEL 1
#define ERROR_LEVEL 0

#ifdef DEBUG
#define DEFAULT_LOG_LEVEL DEBUG_LEVEL
#else
#define DEFAULT_LOG_LEVEL INFO_LEVEL
#endif

//////////////////////////////////////////////////////////////////
// Simple logger
#ifdef DEBUG
#define DBG(msg, ...)  log_log("DEBUG",DEBUG_LEVEL,__FILE__,msg, ##__VA_ARGS__)
#else
#define DBG(msg, ...)
#endif
#define INFO(msg, ...) log_log("INFO ",INFO_LEVEL,__FILE__,msg, ##__VA_ARGS__)
#define WARN(msg, ...) log_log("WARN ",WARN_LEVEL,__FILE__,msg, ##__VA_ARGS__)
#define ERROR(msg, ...) log_log("ERROR",ERROR_LEVEL,__FILE__,msg, ##__VA_ARGS__)

int log_fd();
int log_init(const char* logfile, int loglevel);
int log_shutdown();
void log_log(const char* tag, int level, const char* module, const char* message, ...);
void log_hexdump(void *d, size_t len);
#endif /* SRC_LOG_H_ */
