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

#include "config.h"
#include "log.h"
#include "utils.h"
#include "iracc.h"
#include "databus.h"


int main(int argc, char* argv[]){
	int rc;
	if((rc = log_init("/tmp/iracc-ctl.log",ERROR_LEVEL)) < 0){
		printf("*** warning: log system initialize failed");
	}
	if((rc = databus_init("/tmp/iracc-gate.shm",false)) < 0){
		WARN("*** error initializing databus");
		//exit(1);
	}

	// load data from databus
	uint8_t i = 0;
	size_t s = 1;

	if(databus_get(&i,&s) > 0){
		// slave part
		printf("client get i = %d\n",i);
	}else{
		printf("no data\n");
	}
#if 0
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

//	exit:
	databus_shutdown();
	log_shutdown();
	return 1;
}
