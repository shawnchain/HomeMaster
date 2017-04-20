/*
 * iracc_cmd.h
 *
 *  Created on: 2017年4月20日
 *      Author: shawn
 */

#ifndef IRACC_IRACC_CMD_H_
#define IRACC_IRACC_CMD_H_
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct InternalUnitStatus{
	uint8_t unitId;
	uint8_t windLevel;
	uint8_t powerOn;
	uint8_t filterCleanupFlag;
	uint8_t workMode;
	float presetTemperature;
	uint16_t errorCode;
	float interiorTemerature;
	uint8_t temperatureSensorOK;
}InternalUnitStatus;

typedef struct InternalUnitCommand{
	//Object object;
	uint8_t unitId;
	uint8_t windLevel;  // 0 means not set
	uint8_t powerOn;	// 0 means not set
	int8_t workMode; 	// -1 means not set
	float presetTemperature;
	bool responseReceived;
}InternalUnitCommand;

typedef enum{
	workModeValueAuto = 0x03,
	workModeValueCool = 0x02,
	workModeValueHeat = 0x01,
	workModeValueDry = 0x07,
	workModeValueFan = 0x00,
	workModeValueUnknow = 0xff
}WorkModeValue;

typedef enum{
	windLevelValueLowLow = 0x10,
	windLevelValueLow = 0x20,
	windLevelValueMedium = 0x30,
	windLevelValueHigh = 0x40,
	windLevelValueHighHigh = 0x50,
	windLevelValueUnknow = 0xff
}WindLevelValue;

typedef enum{
	powerModeValueOn = 0x61,
	powerModeValueOff = 0x60,
	powerModeValueUnknow = 0xff,
}PowerModeValue;

int iracc_cmd_parse(char* buf, size_t len);

WorkModeValue iracc_cmd_get_workmode_value(char* valueName);
WindLevelValue iracc_cmd_get_windlevel_value(char* valueName);
PowerModeValue iracc_cmd_get_powermode_value(char* valueName);

const char* iracc_cmd_get_workmode_value_name(WorkModeValue value);
const char* iracc_cmd_get_windlevel_value_name(WindLevelValue value);
const char* iracc_cmd_get_powermode_value_name(PowerModeValue value);

#endif /* IRACC_IRACC_CMD_H_ */
