#include <stdio.h>
#include <string.h>

int iracc_gate_main(int argc, char* argv[]);
int iracc_ctrl_main(int argc, char* argv[]);

static void _print_help(){
	printf("IRACC Program , ver 0.1(%s)\n",VERSION);
	printf("Copyright 2017, BG5HHP\n");
}

int main(int argc, char* argv[]){
	if(argc > 1 && strcmp(argv[1],"ctrl") == 0){
		return iracc_ctrl_main(argc-1,argv+1);
	}else if(argc > 1 && strcmp(argv[1],"gate") == 0){
		return iracc_gate_main(argc-1,argv+1);
	}
	_print_help();
	return 0;
}
