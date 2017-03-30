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
#include <sys/sem.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <time.h>

typedef struct{
	int shmId;
	void * shmAddr;
	size_t shmSize;
	bool master;

	int semId;
}DatabusCtx;

#define SHARED_MEM_SIZE 8 * 1024

static DatabusCtx databus;

static void _databus_lock();
static void _databus_unlock();

int databus_init(const char* sharedFilePath,bool masterFlag){
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
	size_t shmsize = SHARED_MEM_SIZE;
	int shmflag = 0666;
	if(masterFlag){
		shmflag |= IPC_CREAT;
	}
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

	// setup semaphore
	int semflag = 0666;
	if(masterFlag){
		semflag |= IPC_CREAT;
	}
	int semid = semget(shmkey,1,semflag);
	if(semid < 0){
		ERROR("error: semget() %s",strerror(errno));
		return -1;
	}
	// only master set the semaphore initial value
	union semun semval;
	if(masterFlag){
		semval.val = 0x01; //
		semctl(semid,0,SETVAL,semval);
		DBG("sem id(%d) created , init(%d)",semid,semval.val);
	}else{
		DBG("sem id(%d) attached, val(%d)",semid,semctl(semid,0,GETVAL));
	}
	databus.semId = semid;

	databus.master = masterFlag;
	return 0;
}

int databus_shutdown(){
	if(databus.shmAddr){
		// release shared memory
		shmdt(databus.shmAddr);
		databus.shmAddr = 0;
	}
	if(databus.master && databus.shmId){
		shmctl(databus.shmId,IPC_RMID,0);
		databus.shmId = 0;
	}
	if(databus.master && databus.semId){
		semctl(databus.semId,0,IPC_RMID);
		databus.semId = 0;
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
	_databus_lock();
	size_t size = MIN(len,databus.shmSize);
	memcpy(databus.shmAddr,data,size);
	_databus_unlock();
	return size;
}

int databus_get(uint8_t* data, size_t *len){
	if(!databus.shmAddr){
		*len = 0;
		return -1;
	}
	_databus_lock();
	size_t size = MIN((*len),databus.shmSize);
	memcpy(data,databus.shmAddr,size);
	*len = size;
	_databus_unlock();
	return size;
}

static inline void _databus_sem_op(int val){
	if(!databus.semId){
		return;
	}
	int sem_id = databus.semId;
	struct sembuf sem_buf;
	sem_buf.sem_num=0;
	sem_buf.sem_op=val;
	sem_buf.sem_flg=SEM_UNDO;
	if (semop(sem_id,&sem_buf,1)==-1) {
		ERROR("_databus_sem_op(%s) failed, %s",((val>0)?"lock":"unlock"),strerror(errno));
	}
}
static void _databus_lock(){
	//DBG("_databus_lock()");
	_databus_sem_op(-1); // decrease to 0 to lock
}
static void _databus_unlock(){
	//DBG("_databus_unlock()");
	_databus_sem_op(1);  // increase to 1 to unlock
}

#define DATABUS_TEST 1
#if DATABUS_TEST

static time_t t = 0;
static bool master = false;
static uint8_t i = 1;
void databus_test(){
	if(time(NULL) - t < 3){
		return;
	}
	t = time(NULL);

	size_t s = 1;
	if(master){
		// just increase and quit
		i++;
		if(i>128)i=1;
		databus_put(&i,s);
		DBG("master set i = %d",i);
		return;
	}

	// slave or first time run
	if(!databus_get(&i,&s)){
		return;
	}
	if(i == 0){
		i = 1;
		databus_put(&i,s);
		master = true;
		DBG("master set i = %d",i);
		return;
	}else{
		// slave part
		DBG("client get i = %d",i);
		databus.master = false;
	}
}
#endif
