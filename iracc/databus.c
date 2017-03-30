/*
 * databus.c
 *
 *  Created on: 2017年3月30日
 *      Author: shawn
 */

#include "databus.h"
#include "log.h"
#include <string.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

typedef struct{
	int shmId;
	void * shmAddr;
	size_t shmSize;
}DatabusCtx;

#define SHARED_MEM_SIZE 8 * 1024

static DatabusCtx databus;

int databus_init(const char* sharedFilePath,bool createFlag){
	memset(&databus,0,sizeof(DatabusCtx));

	// shared memory key file
	FILE *fp;
	if ((fp = fopen(sharedFilePath, "w"))) {
		fprintf(fp, "%d\n", (int)getpid());
		fclose(fp);
	}else{
		ERROR("*** error creating shm file(%s), %s.",sharedFilePath,strerror(errno));
		return -1;
	}
	key_t shmkey = ftok(sharedFilePath,1);
	if(shmkey < 0){
		// something wrong
		ERROR("error: ftok() %s",strerror(errno));
		return -1;
	}

	// allocate the shared memory
	int shmflag = 0666;
	size_t shmsize = SHARED_MEM_SIZE;
	if(createFlag) shmflag |= IPC_CREAT;
	int shmid = shmget(shmkey,shmsize,shmflag);
	if(shmid < 0){
		ERROR("error: shmget() %s",strerror(errno));
		return -1;
	}
	void* shmaddr = shmat(shmid,0,0);
	if(shmaddr == 0){
		ERROR("error: shmat() %s",strerror(errno));
		return -1;
	}
	databus.shmId = shmid;
	databus.shmAddr = shmaddr;
	databus.shmSize = shmsize;
	DBG("shm id(%d), addr(0x%lx), size(%d)",shmid,shmaddr,(int)shmsize);
	return 0;
}

int databus_shutdown(){
	if(databus.shmAddr){
		// release shared memory
		shmdt(databus.shmAddr);
		databus.shmAddr = 0;
	}
	if(databus.shmId){
		shmctl(databus.shmId,IPC_RMID,0);
		databus.shmId = 0;
	}
	INFO("databus shutdown");
	return 0;
}
#ifndef MIN
#define MIN(x,y) ((x<y)?x:y)
#endif
int databus_put(uint8_t* data, size_t len){
	if(!databus.shmAddr){
		return -1;
	}
	size_t size = MIN(len,databus.shmSize);
	memcpy(databus.shmAddr,data,size);
	return size;
}

int databus_get(uint8_t* data, size_t *len){
	if(!databus.shmAddr){
		*len = 0;
		return -1;
	}
	size_t size = MIN((*len),databus.shmSize);
	memcpy(data,databus.shmAddr,size);
	*len = size;
	return size;
}

static time_t t = 0;
static bool master = false;
void databus_test(){

	if(time(NULL) - t < 3){
		return;
	}
	t = time(NULL);
	uint8_t i = 1;
	size_t s = 1;
	if(!databus_get(&i,&s)){
		return;
	}
	if(i == 0){
		i = 1;
		databus_put(&i,s);
		master = true;
		DBG("master set i = 1");
	}

	if(!master){
		DBG("client get i = %d",i);
	}
}
