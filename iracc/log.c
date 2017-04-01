/*
 * log.c
 *
 *  Created on: 2016年9月20日
 *      Author: shawn
 */
#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>


typedef struct{
	int logLevel;
	size_t logSizeThreshold;
	size_t logSize;
	char logfileName[128];
	FILE *logfile;
}LogCtx;

static LogCtx log = {
		.logLevel = DEFAULT_LOG_LEVEL,
		.logSizeThreshold = (128 * 1024),
		.logSize = 0,
		.logfileName = "",
		.logfile = NULL,
};


#define to_append false
#define to_overwrite true

int log_fd(){
	if(log.logfile){
		return fileno(log.logfile);
	}else{
		return -1;
	}
}

static int log_open(bool overwrite){
	if(overwrite)
		log.logfile = fopen(log.logfileName,"w");
	else
		log.logfile = fopen(log.logfileName,"a");
	if(!log.logfile){
		printf("open log file failed, %s\n",log.logfileName);
		return -1;
	}

	// get log file size if appending
	if(overwrite){
		log.logSize = 0;
		//printf("open log file [%s] success\n",log.logfileName);
	}else{
		// appending mode, get existing file size
		struct stat st;
		memset(&st,0,sizeof(struct stat));
		stat(log.logfileName,&st);
		log.logSize = st.st_size;
		//log_log("INFO ",__FILE__,"log file size, %d",logSize);
		//printf("open log file %s success, file size: %d\n",log.logfileName,(int)log.logSize);
	}
	return 0;
}

static int log_rotate(){
	int rc = 0;
	// close the log file
	if(!log.logfile) return -1;
	fclose(log.logfile);
	log.logfile = NULL;

	// TODO -
	// remove the $logfileName.1
	// rename to $logfileName.1
	char logfileName2[150];
	snprintf(logfileName2, sizeof(logfileName2) - 1, "%s.1",log.logfileName);
	// remove the existing old file
	remove(logfileName2);
	rename(log.logfileName,logfileName2);
	// reopen with overwrite
	//log_log("INFO ",__FILE__,"log file rotated");
	printf("log file rotated\n");
	rc = log_open(to_overwrite);
	return rc;
}

int log_init(const char* logfile,int loglevel){
	int rc;
	strncpy(log.logfileName,logfile,sizeof(log.logfileName) - 1);
	if((rc = log_open(to_append)) < 0){
		return rc;
	}
	log.logLevel = loglevel;
	return 0;
}

int log_shutdown(){
	//DBG("Logger shutdown");
	if(log.logfile){
		fclose(log.logfile);
		log.logfile = NULL;
	}
	return 0;
}

void log_log(const char* tag, int level, const char* module, const char* msg, ...) {
	char string[1024];
	va_list args;
	va_start(args,msg);

	size_t bytesPrinted = 0;

	char stime[32];
	time_t current_time;
	struct tm * time_info;

	if(level > log.logLevel){
		return;
	}

	time(&current_time);
	time_info = localtime(&current_time);
	strftime(stime, sizeof(stime) -1, "%Y-%m-%d %H:%M:%S", time_info);

	// head
	bytesPrinted = sprintf(string,"%s [%s] (%s) - ",stime,tag,module);
	// body
	bytesPrinted += vsnprintf((string + bytesPrinted),sizeof(string) - bytesPrinted - 1,msg,args);

	if(log.logfile){
		fprintf(log.logfile,"%s\n", string);
		fflush(log.logfile);
	}
	// if debug level is on, pring to console as well
	if(strncmp("ERROR",tag,5) == 0){
		fprintf(stderr,"%s\n", string);
	}else{
		printf("%s\n",string);
	}
	bytesPrinted++; // including the leading '\n'

	if(bytesPrinted > 0){
		log.logSize += bytesPrinted;
		//printf("logsize: %d\n",(int)logSize);
		if(log.logSize >= log.logSizeThreshold){
			// we should rotate the log right now!
			log_rotate();
		}
	}
}

void log_hexdump(void *d, size_t len) {
	unsigned char *s;
	size_t bytesPrinted = 0;
	if(log.logfile){
		for (s = d; len; len--, s++){
			bytesPrinted += fprintf(log.logfile,"%02x ", (unsigned int) *s);
		}
		bytesPrinted += fprintf(log.logfile,"\n");
		fflush(log.logfile);
	}
	for (s = d; len; len--, s++){
		printf("%02x ", (unsigned int) *s);
	}
	printf("\n");
}
