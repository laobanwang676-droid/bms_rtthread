#ifndef BMS_ANALYSIS_H
#define BMS_ANALYSIS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
	// 这三个值目前不用管
	uint8_t SOH;	// 电池包SOH值			实际容量/额定容量
	uint8_t SOP;	// 电池包SOP值	
	uint8_t SOE;	// 电池包SOE值


	// 目前这两个值还未做,等后面再实现了
	uint32_t LoopCount;			// 电池包循环次数(完整的一个充放过程+1)
	float CapacityLoop;			// 电池包循环容量(Ah)
	

	// 下面的值已经实现了
	float SOC;					// 电池包SOC值(剩余电量百分比)

	float AverageVoltage;		// 单体平均电压值(V)
	float MaxVoltageDifference;	// 单体电芯最大电压差(V)
	float PowerReal;			// 电池包实时功率(W)
	float CellVoltMax;			// 单体电芯最大电压(V)
	float CellVoltMin;			// 单体电芯最小电压(V)
	
	float CapacityRated;		// 电池包额定容量(Ah)
	float CapacityReal;			// 电池包实际容量(Ah)  		计算方法得进行一次完整的充放电计算
	float CapacityRemain;		// 电池包剩余容量(Ah)
	bool complete;			    // 数据分析完成标志
}BMS_AnalysisDataTypedef;


extern BMS_AnalysisDataTypedef BMS_AnalysisData;



void BMS_AnalysisInit(void);


#endif /* BMS_ANALYSIS_H */

