/*
 * iracc_data.c
 *
 *  Created on: 2017年4月20日
 *      Author: shawn
 */

#include "iracc_cmd.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "log.h"
#include "iracc.h"
#include "utils.h"

//////////////////////////////////////////////////////////////////////
// Command parser
/*
 * parse received command line-by-line and store in iracc context
 * unit=1, mode=2, power=0, wind=5, i_temp=26.6, p_temp=28.1
 *
 *  - mode enum: 1,2,3,4,5|auto,cool,heat,dry,fan --> 03,02,01,07,00
 *  - wind enum: 1,2,3,4,5|ll,l,m,h,hh --> 10,20,30,40,50
 *  - temp: float --> 15.00 ~ 35.00
 *  - power: on,off|0,1 --> 61,60
 */
static inline int _iracc_cmd_assign_value(InternalUnitCommand *iuc, char* key, char* value){
	int ret = 0;
	if(strcmp(key,"unit") == 0){
		int i = atoi(value);
		if(i >=0){
			iuc->unitId = i;
		}else{
			ret = -1;
		}
	}else if(strcmp(key,"mode") == 0){
		WorkModeValue v = iracc_cmd_get_workmode_value(value);
		if(v != workModeValueUnknow){
			iuc->workMode = v;
		}else{
			ret = -1;
		}
	}else if(strcmp(key,"power") == 0){
		PowerModeValue v = iracc_cmd_get_powermode_value(value);
		if(v != powerModeValueUnknow){
			iuc->powerOn = v;
		}else{
			ret = -1;
		}
	}else if(strcmp(key,"wind") == 0){
		WindLevelValue v = iracc_cmd_get_windlevel_value(value);
		if(v != windLevelValueUnknow){
			iuc->windLevel = v;
		}else{
			ret = -1;
		}
	}else if(strcmp(key,"temp") == 0){
		double d = atof(value);
		ret = -1;
		if(d > 0){
			int i = (d * 100.f);
			if(i >= 1500 && i <= 3500){
				// valid
				iuc->presetTemperature = i /100.f;
				ret = 0;
			}
		}
	}else{
		// unknown value
		ret = -1;
	}
	return ret;
}

static inline int _iracc_cmd_parse_aline(char* aline, size_t alineLen){
	DBG("command: %.*s",alineLen,aline);
	char *kvpair;
	char* sep1=",",*sep2="=";
	char  *brk1;
	int count = 0;
	int errors = 0;
	int ret = -1;

	(void)count;

	// split the line by ','
	InternalUnitCommand *iuc = NULL;
	for(kvpair = strtok_r(aline,sep1,&brk1);kvpair;kvpair=strtok_r(NULL,sep1,&brk1)){
		kvpair = trim_str(kvpair);
		char *key,*value;
		DBG("param%d: %s",++count,kvpair);
		// search for the first occurance of '='
		key = kvpair;
		char *eq = strstr(kvpair,sep2);
		if(eq){
			*eq = 0;
			value = eq+1;
		}
		// the key/value is found
		if(key && strlen(key) > 0 && value && strlen(value) > 0){
			if(iuc == NULL){
				iuc = malloc(sizeof(InternalUnitCommand));
				memset(iuc,0,sizeof(InternalUnitCommand));
				iuc->workMode = -1;
			}
			key = trim_str(key);
			value = trim_str(value);
			DBG("key = %s, value %s",key,value);
			if(IRACC_OP_SUCCESS != _iracc_cmd_assign_value(iuc,key,value)){
				errors++;
			}
		}
	}
	if(iuc && errors == 0){
		if(iracc_push_command(iuc) == IRACC_OP_SUCCESS){
			ret = 0;
		}else{
			WARN("***WARN - iracc push command failed, abort current command");
		}
	}
	if(iuc){
		free(iuc);
	}
	return ret;
}

int iracc_cmd_parse(char* buf, size_t len){
	// read line by line
	char aline[128];
	int alineLen = 0;
	bool alineFound = false;
	int ret = 0;
	for(int i = 0;i<len;i++){
		char c = buf[i];
		if (c == '\r' || c == '\n') {
			alineFound = true;
		} else {
			alineFound = false;
			aline[alineLen++] = c;
			if (alineLen == (sizeof(aline) - 1)) {
				DBG("line read buffer full!");
				// we're full!
				alineFound = true;
			}
		}
		if(alineFound){
			aline[alineLen] = 0;
			// split the line
			ret = _iracc_cmd_parse_aline(aline,alineLen);
			// reset the line length
			alineLen = 0;
			alineFound = false;
		}
	}
	if(alineLen > 0){
		aline[alineLen] = 0;
		// split the line
		ret = _iracc_cmd_parse_aline(aline,alineLen);
	}
	return ret;
}


//////////////////////////////////////////////////////////////////////
// Value/Name converters

WorkModeValue iracc_cmd_get_workmode_value(char* valueName){
	WorkModeValue v = workModeValueUnknow;
	if(strcmp(valueName,"auto") == 0){
		v = workModeValueAuto;
	}else if(strcmp(valueName,"cool") == 0){
		v = workModeValueCool;
	}else if(strcmp(valueName,"heat") == 0){
		v = workModeValueHeat;
	}else if(strcmp(valueName,"dry") == 0){
		v = workModeValueDry;
	}else if(strcmp(valueName,"fan") == 0){
		v = workModeValueFan;
	}
	return v;
}

WindLevelValue iracc_cmd_get_windlevel_value(char* valueName){
	WindLevelValue v = windLevelValueUnknow;
	if(strcmp(valueName,"ll") == 0){
		v = windLevelValueLowLow;
	}else if(strcmp(valueName,"l") == 0){
		v = windLevelValueLow;
	}else if(strcmp(valueName,"m") == 0){
		v = windLevelValueMedium;
	}else if(strcmp(valueName,"h") == 0){
		v = windLevelValueHigh;
	}else if(strcmp(valueName,"hh") == 0){
		v = windLevelValueHighHigh;
	}
	return v;
}

PowerModeValue iracc_cmd_get_powermode_value(char* valueName){
	PowerModeValue v = powerModeValueUnknow;
	if(strcmp(valueName,"on") == 0){
		v = powerModeValueOn;
	}else if(strcmp(valueName,"off") == 0){
		v = powerModeValueOff;
	}
	return v;
}

const char* iracc_cmd_get_workmode_value_name(WorkModeValue value){
	const char* valueName = NULL;
	switch(value){
	case workModeValueAuto:
		valueName = "auto";
		break;
	case workModeValueCool:
		valueName = "cool";
		break;
	case workModeValueHeat:
		valueName = "heat";
		break;
	case workModeValueDry:
		valueName = "dry";
		break;
	case workModeValueFan:
		valueName = "fan";
		break;
	case workModeValueUnknow:
	default:
		valueName = "unknow";
		break;
	}
	return valueName;
}

const char* iracc_cmd_get_windlevel_value_name(WindLevelValue value){
	const char* valueName = NULL;
	switch(value){
	case windLevelValueLowLow:
		valueName = "ll";
		break;
	case windLevelValueLow:
		valueName = "l";
		break;
	case windLevelValueMedium:
		valueName = "m";
		break;
	case windLevelValueHigh:
		valueName = "h";
		break;
	case windLevelValueHighHigh:
		valueName = "hh";
		break;
	case windLevelValueUnknow:
	default:
		valueName = "unknow";
		break;
	}
	return valueName;
}

const char* iracc_cmd_get_powermode_value_name(PowerModeValue value){
	const char* valueName = NULL;
	switch(value){
	case powerModeValueOn:
		valueName = "on";
		break;
	case powerModeValueOff:
		valueName = "off";
		break;
	case powerModeValueUnknow:
	default:
		valueName = "unknow";
		break;
	}
	return valueName;
}

