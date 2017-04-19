/*
 * iracc_gate.c
 *
 *  Created on: 2017年3月31日
 *      Author: shawn
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>

#include "config.h"
#include "log.h"
#include "utils.h"
#include "iokit.h"
#include "iracc.h"
#include "databus.h"

static void print_help(int argc, char *argv[]);
static void _register_signal_handlers();

static struct option long_opts[] = {
	{ "service", required_argument, 0, 'S', },
	{ "baudrate", required_argument, 0, 'B', },
	{ "device", required_argument, 0, 'D', },
	{ "log", required_argument, 0, 'l', },
	{ "daemon", no_argument, 0, 'd', },
	{ "verbose", no_argument, 0, 'V', },
	{ "help", no_argument, 0, 'h', },
	{ 0, 0, 0, 0, },
};

AppConfig appConfig = {
		.in_background = false,
		.service = "http://localhost/api?q=",
#ifdef __linux__
		.device="/dev/ttyUSB0",
#else
		.device="/dev/tty.SLAB_USBtoUART",
#endif
		.baudrate = 9600,
		.pid_file = "/tmp/iracc-gate.pid",
		.log_file = "/tmp/iracc-gate.log",
		.shm_file = "/tmp/iracc-gate.shm",
};

int iracc_gate_main(int argc, char* argv[]){
	int opt;
	bool verboseLog = false;
	while ((opt = getopt_long(argc, argv, "S:D:B:l:dVh",
				long_opts, NULL)) != -1) {
		switch (opt){
		case 'S': // service
			strncpy(appConfig.service,optarg,sizeof(appConfig.service) -1);
			break;
		case 'D': // iracc/485 serial device path
			strncpy(appConfig.device,optarg,sizeof(appConfig.device) -1);
			break;
		case 'B':{
			int32_t val = atol(optarg);
			if(val >=0) appConfig.baudrate = val;
		}
			break;
		case 'l': // logfile
			strncpy(appConfig.log_file,optarg,sizeof(appConfig.log_file) - 1);
			break;
		case 'd':
			appConfig.in_background = true;
			break;
		case 'V':
			verboseLog = true;
			break;
		case 'h':
			print_help(argc,argv);
			exit(0);
			break;
		case '?':
			exit(1);
		}
	}

	if (appConfig.in_background){
		do_daemonize();
	}

	FILE *fp;
	if ((fp = fopen(appConfig.pid_file, "w"))) {
		fprintf(fp, "%d\n", (int)getpid());
		fclose(fp);
	}else{
		ERROR("*** error creating pid file(%s), %s. Startup Aborted.",appConfig.pid_file,strerror(errno));
		exit(1);
	}


	int rc;

	if((rc = log_init(appConfig.log_file,verboseLog?DEBUG_LEVEL:DEFAULT_LOG_LEVEL)) < 0){
		printf("*** warning: log system initialize failed");
	}
	if((rc = io_init()) < 0){
		ERROR("*** error initializing iokit module, Startup Aborted.");
		exit(1);
	}
	if((rc = databus_init(appConfig.shm_file,1/*master*/)) < 0){
		ERROR("*** error initializing databus module, Startup Aborted.");
		exit(1);
	}
	if((rc = iracc_init(appConfig.device,appConfig.baudrate,NULL)) < 0){
		ERROR("*** error initializing IRACC module, Startup Aborted.");
		exit(1);
	}

#if 0
	iracc_test();
#endif
	_register_signal_handlers();

	while(true){
		iracc_run();
		io_run();
		//databus_test();
	}
	return 0;
}

#ifndef VERSION
#define VERSION "DEV"
#endif

static void print_help(int argc, char *argv[]){
	printf("IRACC Gate Daemon , ver 0.1(%s)\n",VERSION);
	printf("Usage:\n");
	printf("  %s [options]\n", argv[0]);
	printf("Options:\n");
	printf("  -S, --service                       service API URL\n");
	printf("  -D, --device                        specify path for IRACC device\n"); /*-D /dev/ttyUSB0?AT+KISS=1;*/
	printf("  -B, --baudrate                      specify baudrate for IRACC device \n"); /*-B 115200*/
	printf("  -l, --log                           log file name\n");
	printf("  -d, --daemon                        run as daemon process\n");
	printf("  -V, --verbose                       verbose log \n");
	printf("  -h, --help                          print this help\n");
}

static void signal_handler(int sig){
	//TODO release resources
	DBG("signal caught: %d",sig);
	iracc_shutdown();
	io_shutdown();
	databus_shutdown();
	INFO("Gateway Shutdown OK");
	log_shutdown();
	exit(0);
}
static void _register_signal_handlers(){
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGSTOP, signal_handler);
	signal(SIGTSTP, signal_handler);
	signal(SIGQUIT, signal_handler);
}
