/*
 * modbus.c
 *
 *  Created on: 2017年3月23日
 *      Author: shawn
 */

#include "modbus.h"
#include "lib.h"
#include "crc16.h"
#include "utils.h"
#include "fifo.h"
#include "iokit.h"
#include <time.h>
#include <errno.h>

#define DEFAULT_WAIT_RESPONSE_TIMEOUT 1

static ModbusResponse* modbus_recv(uint8_t *bytes, size_t len);
static bool modbus_send(ModbusRequest *request, int fd, void* callback);

static int _io_init(int fd);
static void _io_callback(int fd, io_state state);
static inline void _io_error();
static void _reader_callback(uint8_t* data, size_t len);

typedef enum {
	modbusStateIdle = 0, modbusStateWaitForResponse
} ModbusState;

typedef struct {
	ModbusCtx ctx;

	Object* queue[32];
	FIFOPtr fifo;
	ModbusState state;

	ModbusRequest *lastRequest;
	time_t lastRequestSendTime;
	time_t waitResponseTimeout; 			// by default 3 second ?

	ModbusReceiveCallback receiveCallback;
	ModbusSendCallback sendCallback;

	struct IOReader reader;
} ModbusCtxInternal;
static ModbusCtxInternal modbus;

void modbus_init(uint8_t address, int fd, ModbusSendCallback onSend,
		ModbusReceiveCallback onReceive) {
	memset(&modbus, 0, sizeof(ModbusCtxInternal));
	modbus.ctx.slaveAddress = address;
	_io_init(fd);
	fifo_init(&(modbus.fifo), modbus.queue,
			(sizeof(modbus.queue) / sizeof(Object*)));
	modbus.sendCallback = onSend;
	modbus.receiveCallback = onReceive;
	modbus.waitResponseTimeout = DEFAULT_WAIT_RESPONSE_TIMEOUT;
}

void modbus_run() {
	if(modbus.reader.fd < 0) return;

	switch (modbus.state) {
	case modbusStateIdle:
	{
		if (fifo_isempty(&modbus.fifo))
			break;
		ModbusRequest *req = (ModbusRequest*) fifo_pop(&modbus.fifo);
		if(!req) break;
		DBG("dequeued request 0x%02x 0x%02x 0x%04x",req->addr, req->code,req->reg);
		modbus_send(req, modbus.reader.fd, NULL);
		modbus.lastRequest = req;
		modbus.lastRequestSendTime = time(NULL);
		modbus.state = modbusStateWaitForResponse;
		break;
	}
	case modbusStateWaitForResponse:
	{
		// timeout check
		if (time(NULL) - modbus.lastRequestSendTime
				> modbus.waitResponseTimeout) {
			// send timeout
			_io_error();
			modbus.state = modbusStateIdle;
			free(modbus.lastRequest);
			modbus.lastRequest = NULL;
		}
		break;
	}
	} // end of switch;

	IO_RUN(&(modbus.reader));
}

/*
 * allocate memory for the request and fill-up default values.
 */
ModbusRequest* modbus_alloc_request(uint8_t code, uint16_t reg) {
	ModbusRequest *req = malloc(sizeof(ModbusRequest));
	if (!req)
		return NULL;
	memset(req, 0, sizeof(ModbusRequest));
	req->addr = modbus.ctx.slaveAddress;
	req->code = code;
	req->reg = reg;
	req->regValue[0] = 0;
	req->regValue[1] = 1;
	req->regValueCount = 2;
	return req;
}

/*
 * Receive them modbus packet
 * @returns ModbusResponse or null if invalid packet received.
 */
static ModbusResponse* modbus_recv(uint8_t *bytes, size_t len) {
	//parse the bytes into modbus frame;
	if (len < 4) {
		// at least 4 bytes for a valid modbus response
		INFO("Invalid modbus response, len %d", len);
		return 0;
	}
	// check against the CRC
	uint16_t crc = crc16_modbus(bytes, len);
	if (crc != 0) {
		WARN("CRC check failed on modbus data, %02x%02x", bytes[len - 2],
				bytes[len - 1]);
		return 0;
	}
	ModbusResponse *resp = malloc(sizeof(ModbusResponse));
	if (!resp) {
		ERROR("allocate modbus response failed, out of memory");
		return 0;
	}
	memset(resp, 0, sizeof(ModbusResponse));

	// JUST COPY THE RECEIVED ARRAY DIRECTLY INTO MODBUS RESPONSE!
	// SEE structure ModbusResponse
	memcpy((uint8_t* )resp, bytes, MIN(len,sizeof(ModbusResponse)));

	// convert the CRC word to host order
	resp->crc = ntohs(resp->crc);
	return resp;
}

int modbus_enqueue_request(ModbusRequest *req){
	if(fifo_isfull(&modbus.fifo)) return -1;
	fifo_push(&modbus.fifo,(Object*)req);
	return 0;
}

/*
 * Send the request over modbus
 */
static bool modbus_send(ModbusRequest *request, int fd, void* callback) {
	uint8_t buf[512];
	uint16_t i = 0;
	buf[i++] = request->addr;
	buf[i++] = request->code;
	buf[i++] = HI_BYTE(request->reg);
	buf[i++] = LO_BYTE(request->reg);
	if(request->regValueCount > 0){
		uint8_t bytesToCopy = MIN(request->regValueCount,128);
		memcpy((buf + i),request->regValue,bytesToCopy);
		i+= bytesToCopy;
	}
	uint16_t crc = crc16_modbus(buf, i);
	crc = htons(crc); //convert to network order a.k.a. BIG_ENDIAN order
	//crc = TO_BIG_ENDIAN_U16(crc);
	buf[i++] = HI_BYTE(crc);
	buf[i++] = LO_BYTE(crc);

	DBG("sending modbus request");
	hexdump(buf, i, 0/*>>*/);
	// send via the serial port. TODO - abstract/move to IOKit
	ssize_t bytesWritten = 0;
	bytesWritten = write(fd, buf, i);
	if (bytesWritten != i) {
		WARN("write may failure, %d of %d bytes written", bytesWritten, i);
		// TODO - handles the async-socket write operations
	}

	return true;
}

static int _io_init(int fd) {
	io_add(fd, _io_callback);
	IO_MAKE_STREAM_READER(&(modbus.reader), fd, _reader_callback,300 /*ms to timeout*/);
	return 0;
}
static inline void _io_error() {
	if (modbus.receiveCallback) {
		modbus.receiveCallback(modbus.lastRequest, NULL);
	}
}
static void _io_callback(int fd, io_state state) {
	switch (state) {
	case io_state_read:
		if (fd == modbus.reader.fd) {
			//int rc = IO_READ(&(modbus.reader));
			if (IO_READ(&(modbus.reader)) < 0) {
				INFO("IOKit read error, %s",strerror(errno));
				_io_error();
			}
		}
		break;
		/*
		 case io_state_write:
		 _iracc_writing(fd);
		 break;
		 */
	case io_state_error:
		INFO("IOKit generic error");
		if (fd == modbus.reader.fd) {
			_io_error();
		}
		break;
	case io_state_idle:
		//DBG("iracc idle");
		break;
	default:
		break;
	}
}

static void _reader_callback(uint8_t* data, size_t len) {
	if (len == 0) {
		WARN("_reader_callback() is called with data len = 0");
		return;
	}
	hexdump(data, len, 1/*<<*/);

	if (modbus.state == modbusStateWaitForResponse) {
		ModbusRequest *req = modbus.lastRequest;
		if (!req) {
			WARN("***illegal state: Got modbus response but the last request is NULL");
			return;
		}
		ModbusResponse *resp = modbus_recv(data, len);
		if (!resp) {
			return;
		}
		//DBG("received modbus response 0x%02, 0x%02x",resp->addr,resp->code);
		if(req->addr == resp->addr && req->code == resp->code){
			if (modbus.receiveCallback) {
				modbus.receiveCallback(req, resp);
			}
			modbus.state = modbusStateIdle; // flip the modbus state
		}else{
			WARN("***illegal state: modbus response (0x%02x%02x) does not match request address(0x%02x%02x). len: %d",
					resp->addr, resp->code, req->addr, req->code,resp->payload.dataLen);
		}
		free(resp);
		free(modbus.lastRequest);
		modbus.lastRequest = NULL;
	} else {
		// do nothing for other mode currently.
		WARN("Received something while idle: %.*s",MIN(16,len),data);
	}
}

int modbus_close() {
	if (modbus.reader.fd > 0) { // TODO - move to reader->fnClose();
		io_remove(modbus.reader.fd);
		IO_CLOSE(&modbus.reader);
	}
	modbus.state = modbusStateIdle;
	return 0;
}

#if 0
ModbusFrame* modbus_alloc_frame(size_t payloadSize) {
	ModbusFrame *f = malloc(sizeof(ModbusFrame));
	if (!f) {
		return NULL;
	}
	memset(f, 0, sizeof(ModbusFrame));
	f->crc = CRC_MODBUS_INIT_VAL;
	f->payload = malloc(payloadSize);
	if (!f->payload) {
		// out of memory!
		free(f);
		return NULL;
	}
	memset(f->payload, 0, payloadSize);
	return f;
}

void modbus_free_frame(ModbusFrame* frame) {
	if (frame) {
		if (frame->payload) {
			free(frame->payload);
		}
		free(frame);
	}
}

static inline void _modbus_putc(ModbusFrame *frame, uint8_t u8) {
	frame->payload[frame->payloadLen++] = u8;
	frame->crc = crc16_modbus_update(u8,frame->crc);
}

void modbus_putc(ModbusFrame *frame, uint8_t u8) {
	_modbus_putc(frame,u8);
}

/*
 * Modbus data frame is big-endian
 * which means the MSB is sent first
 */
void modbus_put16(ModbusFrame *frame, uint16_t u16) {
	// Modbus data frame is big-endian
	_modbus_putc(frame,HI_BYTE(u16));
	_modbus_putc(frame,LO_BYTE(u16));
}
#endif
