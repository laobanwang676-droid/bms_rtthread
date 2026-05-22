#ifndef BMS_BALANCE_H__
#define BMS_BALANCE_H__

#include <rtthread.h>
#include "bms_global_define.h"

/* 外部I2C互斥锁声明 */
extern rt_mutex_t i2c_mutex;

typedef struct
{
	float SocStopChg;			// 停止充电SOC值
	float SocStartChg;			// 启动充电SOC值
	float SocStopDsg;			// 停止放电SOC值
	float SocStartDsg;			// 启动放电SOC值
	
	float BalanceStartVoltage;	// 均衡起始电压(V)
	float BalanceDiffeVoltage;	// 均衡差异电压(V)
	uint32_t BalanceCycleTime;	// 均衡周期时间(s)
	BMS_CellIndexTypedef BalanceRecord;	// 均衡记录,正在均衡的会被位与上
	bool BalanceReleaseFlag;			// 表示均衡释放,false:表示已不满足均衡条件,true:满足均衡条件
}BMS_BalanceDataTypedef;

extern BMS_BalanceDataTypedef BMS_BalanceData;
void BMS_BalanceInit(void);
#endif /* BMS_BALANCE_H__ */
