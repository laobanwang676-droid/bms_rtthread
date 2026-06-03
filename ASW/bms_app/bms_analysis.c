#include <stdio.h>
#include <stdlib.h>
#include <rtthread.h>

#include "bms_analysis.h"

#include "bms_monitor.h"

#include "bms_utils.h"
#include "bms_global_define.h"

#define DBG_TAG "analysis"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"

// thread config
#define ANALYSISI_TASK_STACK_SIZE	256
#define ANALYSISI_TASK_PRIORITY		21
#define ANALYSISI_TASK_TIMESLICE	25

#define ANALYSISI_TASK_PERIOD		1000

#define TEMP_CAP_RATE_LIMITH_HIGH   1050
#define TEMP_CAP_RATE_LIMITL_LOW    750

BMS_AnalysisDataTypedef BMS_AnalysisData =
{
	.CapacityRated = BMS_BATTERY_CAPACITY,
	.complete = false,
};

static void BMS_AnalysisTask(void *param);// 分析线程函数声明

void BMS_AnalysisInit(void)
{
    // 创建分析线程，指定线程函数、参数、栈大小、优先级和时间片
    rt_thread_t analysis_thread = rt_thread_create("bms_analysis", BMS_AnalysisTask,
                                                 NULL, ANALYSISI_TASK_STACK_SIZE, 
                                                 ANALYSISI_TASK_PRIORITY, 
                                                 ANALYSISI_TASK_TIMESLICE);
    if (analysis_thread != RT_NULL)
    {
        rt_thread_startup(analysis_thread); // 启动分析线程
        LOG_I("BMS Analysis thread created and started successfully.");
    }
    else
    {
        LOG_E("Failed to create BMS Analysis thread.");
    }
}

// 三元锂电池 SOC 开路电压法计算数据表
uint16_t SocOcvTab[101]=
{
	3282, // 0%~1%	
	3309, 3334, 3357, 3378, 3398, 3417, 3434, 3449, 3464, 3477,	// 0%~10%
	3489, 3500, 3510, 3520, 3528, 3536, 3543, 3549, 3555, 3561,	// 11%~20%
	3566, 3571, 3575, 3579, 3583, 3586, 3590, 3593, 3596, 3599,	// 21%~30%
	3602, 3605, 3608, 3611, 3615, 3618, 3621, 3624, 3628, 3632,	// 31%~40%
	3636, 3640, 3644, 3648, 3653, 3658, 3663, 3668, 3674, 3679,	// 41%~50%
	3685, 3691, 3698, 3704, 3711, 3718, 3725, 3733, 3741, 3748,	// 51%~60%
	3756, 3765, 3773, 3782, 3791, 3800, 3809, 3818, 3827, 3837,	// 61%~70%
	3847, 3857, 3867, 3877, 3887, 3897, 3908, 3919, 3929, 3940,	// 71%~80%
	3951, 3962, 3973, 3985, 3996, 4008, 4019, 4031, 4043, 4055,	// 81%~90%
	4067, 4080, 4092, 4105, 4118, 4131, 4145, 4158, 4172, 4185,	// 91~100%
};

static uint16_t BMS_AnalysisOcvToSoc(uint16_t voltage)
{
    uint16_t soc = 0;
    if(voltage <= SocOcvTab[0])
    {
        soc = 0;
    }
    else if(voltage >= SocOcvTab[100])
    {
        soc = 1000;// 100.0%以整数形式表示
    }
    else
    {
        uint16_t index = right_bound(SocOcvTab, 0, 100, voltage);
        //整数部分+小数部分，乘以10以整数形式表示SOC，保留一位小数
        soc = ((index - 1) * 10 + (voltage - SocOcvTab[index - 1]) * 10 / (SocOcvTab[index] - SocOcvTab[index - 1]));
    }
    return soc;
}

static void BMS_AnalysisCapAndSocInit(void)
{
    BMS_AnalysisData.SOC = BMS_AnalysisOcvToSoc(BMS_MonitorData.CellData[0].CellVoltage * 1000) / 1000.0f; // 转为范围0.0~1.0
    BMS_AnalysisData.CapacityReal = BMS_AnalysisData.CapacityRated;
    BMS_AnalysisData.CapacityRemain = BMS_AnalysisData.CapacityReal * BMS_AnalysisData.SOC;
}

//计算电压差、平均电压、实时功率等数据的函数
static void BMS_AnalysisCalculate(void)
{
    float volt_sum = 0.0f;
    BMS_AnalysisData.CellVoltMax = BMS_MonitorData.CellData[BMS_GlobalParam.Cell_Real_Number - 1].CellVoltage;//最大电压
    BMS_AnalysisData.CellVoltMin = BMS_MonitorData.CellData[0].CellVoltage;//最小电压
    for (uint8_t i = 0; i < BMS_GlobalParam.Cell_Real_Number; i++)
    {
        volt_sum += BMS_MonitorData.CellData[i].CellVoltage;
    }
    BMS_AnalysisData.AverageVoltage = volt_sum / BMS_GlobalParam.Cell_Real_Number;// 平均电压
    BMS_AnalysisData.MaxVoltageDifference = BMS_AnalysisData.CellVoltMax - BMS_AnalysisData.CellVoltMin;// 电压差
    BMS_AnalysisData.PowerReal = BMS_MonitorData.BatteryVoltage * BMS_MonitorData.BatteryCurrent;// 实时功率
}

//温度修正容量的函数
static void BMS_AnalysisTempCompensation(void)
{
	static int16_t LastTemp = 0;

	uint8_t  Ratio;     // 校准倍率
	uint16_t RateTemp;  // 校准后的容量百分比,比如1000表示100%
	int16_t MinTemp = BMS_MonitorData.CellTemp[0] * 10; // 小数转化成整数,方便计算


	if (BMS_MonitorData.CellTempEffectiveNumber == 0)
	{
		return;
	}

	// 判断温度变化是否超过1度
	// 超过1度则校准一次容量
	// 未超过则不执行下面校准
	if( MinTemp > LastTemp)
	{
		if (MinTemp - LastTemp >= 10)
		{
			LastTemp = MinTemp;
		}
		else
		{
			return;
		}
	}
	else
	{
		if (LastTemp - MinTemp >= 10)
		{
			LastTemp = MinTemp;
		}
		else
		{
			return;
		}
	}

	// 确定每一摄氏度的校准倍率
	// 该校准倍率的由来是根据不同温度下的放电曲线来的
	// 放电曲线：http://www.doczj.com/doc/1510977503.html
	// 上面链接的放电曲线跟这份代码的校准区间的参数有所不同	
	// 搜了几个三元锂电池的放电温度特性曲线,都是以25度常温为标准,25度时容量不受温度影响
	
	if (MinTemp >= 250)
	{
		// 温度大于25度时,每1度的倍率为0.001
		// 大于常温放电时间变长,就可以理解为容量增加
		// 增加的容量为：0.001 * (最小温度-常温)
		Ratio = 1;
	}
	else if (MinTemp >= 100 && MinTemp < 250)
	{
		// 温度小于25度时,每1度的倍率为0.002
		// 小于常温放电时间变短,就可以理解为容量减小
		// 减小的容量为：0.002 * (最小温度-常温)
		Ratio = 2;
	}
	else if (MinTemp >= 0 && MinTemp < 100)
	{   
		Ratio = 3;
	}
	else if (MinTemp >= -200 && MinTemp < -10)
	{   
		Ratio = 4;
	}
	else if (MinTemp >= -300 && MinTemp < -200)
	{   
		Ratio = 5;
	}
	else
	{
        // ratio:这个量表示是一个变化趋势，温度越低，温度区间范围越大，也就证明变化趋势越大，所以ratio也在增大
		Ratio = 6;
	}

	// 该公式理解:
	// 1000：表示为电池容量为100%
	// ratio：表示为特定温度区间内,每一摄氏度容量衰减/增加的倍率
	// (MinTemp - 250) / 10:高/低了多少度
	// RateTemp:计算出来的就是电池衰减/增加百分比
	RateTemp = 1000 + Ratio * (MinTemp - 250) / 10;

	// 做了个上下限
	// 不能超过105%
	// 不能低于75%
	if(RateTemp > TEMP_CAP_RATE_LIMITH_HIGH )
	{
		RateTemp = TEMP_CAP_RATE_LIMITH_HIGH;
	}
	else if(RateTemp < TEMP_CAP_RATE_LIMITL_LOW)
	{
		RateTemp = TEMP_CAP_RATE_LIMITL_LOW;
	}

	// 实时容量
	BMS_AnalysisData.CapacityReal = BMS_AnalysisData.CapacityRated * RateTemp / 1000;

	// 剩余容量
	BMS_AnalysisData.CapacityRemain = BMS_AnalysisData.CapacityReal * BMS_AnalysisData.SOC;
}

//待机开路电压测soc
static void BMS_AnalysisOcvSocCompensation(void)
{
	// 进入睡眠的条件:待机一段时间以上且没有电池在均衡
	if (BMS_GlobalParam.SysMode == BMS_MODE_SLEEP)
	{
		// 等待一段时间电压平稳,防止均衡才刚结束
		rt_thread_mdelay(BALANCE_VOLT_RISE_DELAY);

		// 开路电压校准
		BMS_AnalysisData.SOC = BMS_AnalysisOcvToSoc(BMS_MonitorData.CellData[0].CellVoltage  * 1000) / 1000.0;

		// 剩余容量 = 实际容量 * soc
		BMS_AnalysisData.CapacityRemain = BMS_AnalysisData.CapacityReal * BMS_AnalysisData.SOC;		
	}
}

//安时积分法计算soc
static void BMS_AnalysisAhIntegrateToSoc(void)
{
	float current = abs((int)(BMS_MonitorData.BatteryCurrent * 1000) / 1000.0f / 3600.0f); // 转换为安时
	
	if(BMS_GlobalParam.SysMode == BMS_MODE_STANDBY)//待机模式下,电流过小,不进行安时积分,防止SOC计算不准确
	{
		if(BMS_MonitorData.CellData[BMS_CELL_MAX - 1].CellVoltage >= INIT_OV_PROTECT)
		{
			BMS_AnalysisData.SOC = 1.0f; 
		}
		else if(BMS_MonitorData.CellData[0].CellVoltage <= INIT_UV_PROTECT)
		{
			BMS_AnalysisData.SOC = 0.0f;
		}
	}
	else if(BMS_GlobalParam.SysMode == BMS_MODE_CHARGE) // 充电模式下,电流为正,剩余容量增加,但不能超过实际容量
	{
		if(BMS_AnalysisData.CapacityRemain + current <= BMS_AnalysisData.CapacityReal)
		{
			BMS_AnalysisData.CapacityRemain += current; // 增加剩余容量
		}
		else
		{
			BMS_AnalysisData.CapacityRemain = BMS_AnalysisData.CapacityReal; // 充满了,剩余容量等于实际容量
		}
	}
	else if (BMS_GlobalParam.SysMode == BMS_MODE_DISCHARGE) // 放电模式下,电流为负,剩余容量减少,但不能小于0
	{
		if(BMS_AnalysisData.CapacityRemain - current >= 0)
		{
			BMS_AnalysisData.CapacityRemain -= current; // 减少剩余容量
		}
		else
		{
			BMS_AnalysisData.CapacityRemain = 0; // 放完了,剩余容量等于0
		}
	}

	BMS_AnalysisData.SOC = BMS_AnalysisData.CapacityRemain / BMS_AnalysisData.CapacityReal; // 更新SOC

	if(BMS_AnalysisData.SOC > 1.0f)
	{
		BMS_AnalysisData.SOC = 1.0f;
	}
}

static void BMS_AnalysisTask(void *param)
{
	(void)param;
    BMS_AnalysisCapAndSocInit(); // 初始化容量和SOC
    while(1)
    {
        BMS_AnalysisCalculate(); // 计算分析电压差、平均电压、实时功率等数据
        BMS_AnalysisTempCompensation(); // 温度修正容量
        BMS_AnalysisOcvSocCompensation();//休眠时测量开路电压校准SOC和剩余容量，为安时积分法提供准确的初始值
        BMS_AnalysisAhIntegrateToSoc(); // 安时积分法计算soc

		if(BMS_AnalysisData.complete == false)
		{
			BMS_AnalysisData.complete = true; // 第一次分析完成后，设置完成标志
		}
		
        rt_thread_delay(ANALYSISI_TASK_PERIOD); // 延时，控制任务周期
    }
}
