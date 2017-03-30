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
HEADERS = gsm.h utils.h serial_port.h log.h iokit.h

all: iracc-gate 

iracc-gate: main.o serial_port.o utils.o log.o iracc.o iokit.o crc16.o modbus.o databus.o
	$(CC) $(LDFLAGS) -o $@ $^ 

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

install: iracc-gate 
	cp -f iracc-gate $(PREFIX)/sbin/

clean:
	@rm -f iracc-gate *.o

