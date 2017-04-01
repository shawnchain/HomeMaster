#
# Copyright (c) 2016 Shawn Chain
# Author: Shawn Chain (shawn.chain@gmail.com)
#

ifeq ($(PREFIX),)
PREFIX := $(shell [ -d /opt/local ] && echo /opt/local || echo /usr )
endif

GIT_VERSION := $(shell git describe --abbrev=6 --always --tags)

CC ?= gcc
CFLAGS += -Wall -ggdb -DDEBUG -DVERSION=\"$(GIT_VERSION)\"
HEADERS = lib.h defs.h object.h utils.h serial_port.h log.h iokit.h 

LD ?= gcc
#LDFLAGS += -Wall -g

#PCH_H = lib.h
#PCH = lib.h.gch

OBJS = main.o serial_port.o utils.o log.o iracc.o iokit.o crc16.o modbus.o databus.o iracc_ctrl.o iracc_gate.o object.o
OBJS += fifo.o

LIBS =

all: iracc

iracc: $(OBJS) 
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

#%.o: %.c $(HEADERS) $(PCH)
#	$(CC) $(CFLAGS) -c -o $@ $<

$(PCH): $(PCH_H)
	$(CC) $(CFLAGS) -x c-header $> $^

install: iracc-gate 
	cp -f iracc $(PREFIX)/sbin/

clean:
	@rm -f iracc *.o *.gch

