/*
 * iracc_control.c
 *
 *  Created on: 2017年3月30日
 *      Author: shawn
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <getopt.h>

#include "config.h"
#include "log.h"
#include "utils.h"
#include "iracc.h"
#include "databus.h"

static void print_help(int argc, char *argv[]);

static struct option long_opts[] = {
	{ "set", required_argument, 0, 'S', },
	{ "data", required_argument, 0, 'd', },
	{ "log", required_argument, 0, 'l', },
	{ "verbose", no_argument, 0, 'V', },
	{ "help", no_argument, 0, 'h', },
	{ 0, 0, 0, 0, },
};
static AppConfig ctrlConfig = {
		.in_background = false,
		.service = "",
		.device="",
		.baudrate = 0,
		//.pid_file = "/tmp/iracc-ctrl.pid",
		.log_file = "/tmp/iracc-ctrl.log",
		.shm_file = "/tmp/iracc-gate.shm",
};

typedef enum{
	DataOp_Read = 0,
	DataOp_Write = 1
}DataOp;

// iracc ctrl --set unit=0,mode=2,power=1,wind=4,p_temp=12.3
int iracc_ctrl_main(int argc, char* argv[]){

	// load data from databus
	uint8_t buf[512];
	size_t bufSize = sizeof(buf);
	DataOp op = DataOp_Read;
	bool verbose = false;

	int opt;
	while ((opt = getopt_long(argc, argv, "S:d:l:Vh",
				long_opts, NULL)) != -1) {
		switch (opt){
		case 'S': // --set unit=0,mode=2,power=1,wind=4,p_temp=12.3
			strncpy((char*)buf,optarg,bufSize -1);
			op = DataOp_Write;
			break;
		case 'd': // --data /tmp/iracc-gate.shm
			strncpy(ctrlConfig.shm_file,optarg,sizeof(ctrlConfig.shm_file) - 1);
			break;
		case 'l': // --log /tmp/iracc-ctrl.log
			strncpy(ctrlConfig.log_file,optarg,sizeof(ctrlConfig.log_file) - 1);
			break;
		case 'V':
			verbose = true;
			break;
		case 'h': // --help
			print_help(argc,argv);
			exit(0);
			break;
		case '?':
			DBG("Unknown command args");
			exit(1);
		}
	}

	int rc;
	if((rc = log_init(ctrlConfig.log_file,verbose?DEBUG_LEVEL:ERROR_LEVEL)) < 0){
		printf("*** warning: log system initialize failed");
	}
	if((rc = databus_init(ctrlConfig.shm_file,false)) < 0){
		WARN("*** error initializing databus");
		exit(1);
	}

	if(op == DataOp_Write){
		// send command to the databus
		INFO("Writing databus...");
		if(databus_put(buf,strlen((char*)buf)) > 0){
			// write success
			//printf("OK\n");
		}else{
			printf("databus is not ready\n");
			goto exit;
		}
	}

	INFO("Reading databus...");
	// read data from databus
	if(databus_get(buf,&bufSize) > 0){
		if(strlen((const char*)buf) > 0){
			printf("%.*s\n",128,buf);
		}else{
			printf("no data\n");
		}
	}else{
		printf("databus is not ready\n");
	}

#define IRACC_CTRL_TEST 0
#if IRACC_CTRL_TEST
	time_t t = 0;
	while(true){
		sleep(1);

		if(time(NULL) - t < 3){
			continue;
		}
		t = time(NULL);

		// slave or first time run
		if(!databus_get(&i,&s)){
			break;
		}
		// slave part
		DBG("client get i = %d",i);
	}
#endif

	exit:
	databus_shutdown();
	log_shutdown();
	return 1;
}

#ifndef VERSION
#define VERSION "DEV"
#endif
static void print_help(int argc, char *argv[]){
	printf("IRACC Control , ver 0.1(%s)\n",VERSION);
	printf("Usage:\n");
	printf("  iracc %s [options]\n", argv[0]);
	printf("Options:\n");
	printf("  -S, --set                           setup command parameters\n");
	printf("  -d, --data                          data file path\n");
	printf("  -l, --log                           log file path\n");
	printf("  -h, --help                          print this help\n");
}
