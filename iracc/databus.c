/*
 * databus.c
 *
 *  Created on: 2017年3月30日
 *      Author: shawn
 */

#include "databus.h"
#include "lib.h"
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>


#ifdef _SEM_SEMUN_UNDEFINED
union semun { 
	int val; 
	struct semid_ds *buf; 
	unsigned short *array; 
	struct seminfo *__buf; 
};
#endif 

typedef struct{
	int shmId;
	void * shmAddr;
	size_t shmSize;
	int semId;

	void* memOut;
	size_t memOutSize;
	void* memIn;
	size_t memInSize;

	bool master;
}DatabusCtx;

#define SHARED_MEM_SIZE 8 * 1024

static DatabusCtx databus;

static bool _databus_shm_should_release();
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
		WARN("*** error creating shm file(%s), %s.",sharedFilePath,strerror(errno));
		return -1;
	}
	key_t shmkey = ftok(sharedFilePath,2);
	if(shmkey < 0){
		// something wrong
		WARN("error: ftok() %s",strerror(errno));
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
		WARN("error: shmget() %s",strerror(errno));
		return -1;
	}
	void* shmaddr = shmat(shmid,0,0);
	if(shmaddr == 0){
		WARN("error: shmat() %s",strerror(errno));
		return -1;
	}
	databus.shmId = shmid;
	databus.shmAddr = shmaddr;
	databus.shmSize = shmsize;
	DBG("shm id(%d), addr(0x%lx), size(%d)",shmid,shmaddr,(int)shmsize);

	//|0 - 4095|4096 - 8192|
	//|read    |write      |
	databus.memOut = shmaddr;
	databus.memOutSize = shmsize / 2;
	databus.memIn = shmaddr + (shmsize / 2);
	databus.memInSize = shmsize / 2;
	DBG("read  addr(0x%lx), size(%d)",databus.memOut,(int)databus.memOutSize);
	DBG("write addr(0x%lx), size(%d)",databus.memIn,(int)databus.memInSize);

	// setup semaphore
	int semflag = 0666;
	if(masterFlag){
		semflag |= IPC_CREAT;
	}
	int semid = semget(shmkey,1,semflag);
	if(semid < 0){
		WARN("error: semget() %s",strerror(errno));
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
		databus.memIn = 0;
		databus.memOut = 0;
	}
	// release shared memory
	if(_databus_shm_should_release()/*databus.master && databus.shmId*/){
		DBG("release shared memory %d",databus.shmId);
		shmctl(databus.shmId,IPC_RMID,0);
		databus.shmId = 0;
	}
	// release semaphore
	if(databus.master && databus.semId){
		semctl(databus.semId,0,IPC_RMID);
		databus.semId = 0;
	}
	INFO("Databus shutdown");
	return 0;
}

int databus_put_in(uint8_t* data, size_t len){
	if(!databus.shmAddr){
		return 0;
	}
	_databus_lock();
	size_t size = MIN(len,databus.memInSize);
	memcpy(databus.memIn,data,size);
	_databus_unlock();
	return size;
}

int databus_get_in(uint8_t* data, size_t *len, uint8_t mode){
	if (!databus.shmAddr) {
		*len = 0;
		return 0;
	}
	_databus_lock();
	size_t size = MIN((*len), databus.memInSize);
	if (mode & DATABUS_ACCESS_MODE_STRING) {
		strncpy((char* )data, databus.memIn, size);
		*len = strlen((char*) data);
	} else {
		memcpy(data, databus.memIn, size);
		*len = size;
	}

	if((*len > 0) && (mode & DATABUS_ACCESS_MODE_POP)){
		uint8_t zero = 0;
		memcpy(databus.memIn,&zero,1);
		//DBG("clear databus.memIn");
	}
	_databus_unlock();
	return *len;
}

int databus_put_out(uint8_t* data, size_t len){
	if(!databus.shmAddr){
		return 0;
	}
	_databus_lock();
	size_t size = MIN(len,databus.memOutSize);
	memcpy(databus.memOut,data,size);
	_databus_unlock();
	return size;
}

int databus_get_out(uint8_t* data, size_t *len, uint8_t mode){
	if (!databus.shmAddr) {
		*len = 0;
		return 0;
	}
	_databus_lock();
	size_t size = MIN((*len), databus.memOutSize);
	if (mode & DATABUS_ACCESS_MODE_STRING) {
		strncpy((char* )data, databus.memOut, size);
		*len = strlen((char*) data);
	} else {
		memcpy(data, databus.memOut, size);
		*len = size;
	}

	if((*len > 0) && (mode & DATABUS_ACCESS_MODE_POP)){
		uint8_t zero = 0;
		memcpy(databus.memOut,&zero,1);
//		if(mode & DATABUS_ACCESS_MODE_STRING){
//
//		}else{
//
//		}
	}
	_databus_unlock();
	return *len;
}

static bool _databus_shm_should_release(){
	if(!databus.shmId) return false;
#if 1
	return databus.master;
#else
	struct shmid_ds buf;
	memset(&buf,0,sizeof(struct shmid_ds));
	if(0 == shmctl(databus.shmId,IPC_STAT,&buf)){
		return buf.shm_cpid == getpid();
	}
	return false;
#endif
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
	_databus_sem_op(-1); // decrease to 0 to lock
}
static void _databus_unlock(){
	_databus_sem_op(1);  // increase to 1 to unlock
}

#define DATABUS_TEST 0
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
