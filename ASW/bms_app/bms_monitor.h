#ifndef BMS_MONITOR_H
#define BMS_MONITOR_H

#include <stdbool.h>
#include "bms_global_define.h"

typedef struct
{
	float CellVoltage; 		// 电芯电压(V)
	uint32_t CellNumber;	// 电芯的编号
}BMS_CellDataTypedef;

typedef struct
{
	float CellTemp[BMS_TEMP_MAX];					// 采样温度,温度数据会从小到大排序(℃)
	float BatteryVoltage;							// 电池总电压(V)
	float BatteryCurrent;							// 电池组电流(A)
	BMS_CellDataTypedef CellData[BMS_CELL_MAX]; 	// 电芯数据,电压数据会从小到大排序
	float CellVoltage[BMS_CELL_MAX]; 				// 电芯电压,未排序的
	uint32_t CellTempEffectiveNumber;				// 有效值的温度数量
	bool complete;									// 数据获取完成标志
}BMS_MonitorDataTypedef;

extern BMS_MonitorDataTypedef BMS_MonitorData;
extern BMS_GlobalParamTypedef BMS_GlobalParam;

void BMS_MonitorInit(void);
void BMS_MonitorStateVoltage(BMS_StateTypedef NewState);
void BMS_MonitorStateBatCurrent(BMS_StateTypedef NewState);
void BMS_MonitorStateCellTemp(BMS_StateTypedef NewState);


#endif /* BMS_MONITOR_H */
