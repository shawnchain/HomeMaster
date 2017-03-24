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

#include "config.h"
#include "log.h"
#include "utils.h"
#include "iokit.h"
#include "iracc.h"
#include "crc16.h"

static void print_help(int argc, char *argv[]);

static struct option long_opts[] = {
	{ "service", required_argument, 0, 'S', },
	{ "baudrate", required_argument, 0, 'B', },
	{ "device", required_argument, 0, 'D', },
	{ "log", required_argument, 0, 'l', },
	{ "daemon", no_argument, 0, 'd', },
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
		.pid_file = "/tmp/sms-daemon.pid",
		.log_file = "/tmp/sms-daemon.log"
};

int main(int argc, char* argv[]){
	int opt;

	crc16_test();
	while ((opt = getopt_long(argc, argv, "S:D:B:l:dh",
				long_opts, NULL)) != -1) {
		switch (opt){
		case 'S': // service
			strncpy(appConfig.service,optarg,sizeof(appConfig.service) -1);
			break;
		case 'D': // gsm serial device path
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
	}

	int rc;

	if((rc = log_init(appConfig.log_file)) < 0){
		printf("*** warning: log system initialize failed");
	}

	if((rc = io_init()) < 0){
		ERROR("*** error: initialize the poll module, aborted.");
		exit(1);
	}

	if((rc = iracc_init(appConfig.device,appConfig.baudrate,NULL)) < 0){
		ERROR("*** error: initialize the GSM module, aborted.");
		exit(1);
	}

	while(true){
		iracc_run();
		io_run();
	}
	return 0;
}

#ifndef VERSION
#define VERSION "DEV"
#endif

static void print_help(int argc, char *argv[]){
	printf("SMS Forward Daemon , ver 0.1(%s)\n",VERSION);
	printf("Usage:\n");
	printf("  %s [options]\n", argv[0]);
	printf("Options:\n");
	printf("  -S, --service                       service API URL\n");
	printf("  -D, --device                        specify path for GSM device\n"); /*-D /dev/ttyUSB0?AT+KISS=1;*/
	printf("  -B, --baudrate                      specify baudrate for GSM device \n"); /*-B 115200*/
	printf("  -l, --log                           log file name\n");
	printf("  -d, --daemon                        run as daemon process\n");
	printf("  -h, --help                          print this help\n");
}
