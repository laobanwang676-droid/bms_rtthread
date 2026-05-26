/*
对bq769数据采集
各节电压、电流、温度等数据的采集和处理
*/

#include <stdio.h>
#include <rtthread.h>
#include "bq769.h"
#include "bms_monitor.h"
#include "bms_utils.h"
#include "bms_control.h"

#define DBG_TAG "monitor"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"

// 线程配置：栈大小、优先级、时间片（用于 RT-Thread 创建线程）
#define MONITOR_TASK_STACK_SIZE	512
#define MONITOR_TASK_PRIORITY	19
#define MONITOR_TASK_TIMESLICE	25

// 监控任务周期（单位：毫秒），任务每隔此时间进入一次循环
#define MONITOR_TASK_PERIOD		250

// 采样时间配置（单位：毫秒）
// 单体电芯电压采样周期
#define UPDATE_CELL_VOLTAGE_CYCLE	250
// 电池组电压采样周期
#define UPDAYE_BAT_VOLTAGE_CYCLE	250
// 单体电芯温度采样周期
#define UPDATE_CELL_TEMP_CYCLE		2000
// 电流采样周期（若使用软件周期触发，单位：毫秒）
#define UPDATE_BAT_CURRENT_CYCLE	1000

//系统模式决策电流值
#define SYS_MODE_DECISION_CURRENT	0.2f  // 0.2A作为系统模式切换的电流阈值

// 全局监控数据结构，保存采样到的电池相关数据（电压、电流、温度等）
BMS_MonitorDataTypedef BMS_MonitorData =
{
    .complete = false, // 初始化时数据未完成
};

// 采样使能标志：为 true 时允许对应采样执行
static bool FlagVoltage = true;     // 电压采样开关,单体和总电压共用一个标志
static bool FlagCellTemp = true;    // 电芯温度采样开关
static bool FlagBatCurrent = true;  // 电流采样开关（软件/硬件触发取决于实现）

// 时间计数器：以毫秒为单位累计，用于判断是否到达采样周期
static uint16_t CountVoltage = 0;     // 电压计时器（ms）单节和总都用同一个计时器，因为它们的采样周期相同
static uint16_t CountCellTemp = 0;    // 电芯温度计时器（ms）
static uint16_t CountBatCurrent = 0; // 电流计时器（ms）
static uint16_t CountSysModeSleep = 0; // 系统模式切换睡眠计时器（ms）

//初始化全局参数
BMS_GlobalParamTypedef BMS_GlobalParam = 
{
	.SysMode 			= BMS_MODE_STANDBY,
	.Cell_Real_Number 	= BMS_CELL_MAX,
	.Temp_Real_Number 	= BMS_TEMP_MAX,
	.Charge 			= BMS_STATE_DISABLE,
	.Discharge 			= BMS_STATE_DISABLE,
	.Balance		 	= BMS_STATE_DISABLE,
};

//声明函数
static void Bms_MonitorTask(void *parameter);
static void BMS_MonitorBattery(void);
static void BMS_MonitorSysMode(void);

void BMS_MonitorInit(void)
{
    // 创建监控线程，指定线程函数、参数、栈大小、优先级和时间片
    rt_thread_t monitor_thread = rt_thread_create("bms_monitor", Bms_MonitorTask,
                                                 NULL, MONITOR_TASK_STACK_SIZE, 
                                                 MONITOR_TASK_PRIORITY, 
                                                 MONITOR_TASK_TIMESLICE);
    if (monitor_thread != RT_NULL)
    {
        rt_thread_startup(monitor_thread); // 启动监控线程
        LOG_I("BMS Monitor thread created and started successfully.");
    }
    else
    {
        LOG_E("Failed to create BMS Monitor thread.");
    }
}

static void Bms_MonitorTask(void *parameter)
{
    while(1)
    {
        BMS_MonitorBattery(); // 采集电池数据
        BMS_MonitorSysMode(); // 根据采集的数据判断系统模式

        if(BMS_MonitorData.complete == false)
        {
            BMS_MonitorData.complete = true; // 第一次采集完成后，设置完成标志
        }
        
        rt_thread_delay(MONITOR_TASK_PERIOD); // 延时，控制任务周期
    }
}

/****************************HAL抽象层**********************/

// 冒泡排序的比较程序,对电压数据进行比较
static int compaer_cell(void *e1, void *e2)
{
	float temp1, temp2;
	
	temp1 = (*(BMS_CellDataTypedef *)e1).CellVoltage;
	temp2 = (*(BMS_CellDataTypedef *)e2).CellVoltage;

	if (temp1 > temp2)
	{
		return 1;
	}

    return 0;
}

// 采集电压数据的函数，更新单体电芯电压和电池组总电压，并保存到监控数据结构中
static void BMS_HalMonitorBattery(void)
{
    BMS_HalUpdateCellVoltage(); // 通过HAL接口采集电压（含I2C互斥锁保护）
    
    for(uint8_t i = 0; i < BMS_GlobalParam.Cell_Real_Number; i++)
    {
        BMS_MonitorData.CellVoltage[i] = BQ769X0_SampleData.CellVoltage[i];// 保存未排序的电芯电压
        BMS_MonitorData.CellData[i].CellVoltage = BQ769X0_SampleData.CellVoltage[i];// 保存电芯电压到CellData结构
        BMS_MonitorData.CellData[i].CellNumber = i;// 电芯编号
    }
    BubbleSort(BMS_MonitorData.CellData, BMS_GlobalParam.Cell_Real_Number, sizeof(BMS_CellDataTypedef), compaer_cell); // 对电芯数据进行冒泡排序，按电压从小到大排序
    
    BMS_MonitorData.BatteryVoltage = BQ769X0_SampleData.BatteryVoltage; // 保存电池组总电压到监控数据
}

// 采集电芯温度的函数，更新单体电芯温度，并保存到监控数据结构中
static void Bms_HalMonitorCellTemperature(void)
{	
	uint8_t index1 = 0, index2 = 0;//读指针和写指针 避免无效温度数据对排序的影响
	
	BMS_HalUpdateCellTemperature(); // 通过HAL接口采集温度（含I2C互斥锁保护）
	
	for (; index1 < BMS_GlobalParam.Temp_Real_Number; index1++)
	{
		if (BQ769X0_SampleData.TsxTemperature[index1] >= BMS_TEMP_MEASURE_MIN &&  BQ769X0_SampleData.TsxTemperature[index1] <= BMS_TEMP_MEASURE_MAX)
		{
			BMS_MonitorData.CellTemp[index2++] = BQ769X0_SampleData.TsxTemperature[index1];
		}
	}
	
	BMS_MonitorData.CellTempEffectiveNumber = index2;

	// 进行顺序排序
	BubbleFloat(BMS_MonitorData.CellTemp, index2);
}

// 采集电流数据的函数，更新电池组总电流，并保存到监控数据结构中
static void Bms_HalMonitorBatteryCurrent(void)
{
	BMS_HalUpdateCellCurrent(); // 通过HAL接口采集电流（含I2C互斥锁保护）
	
	BMS_MonitorData.BatteryCurrent = BQ769X0_SampleData.BatteryCurrent;	
}
/***********************************************************/

/*
250ms执行一次
读取单节、总电压、电流、温度等数据的函数    
根据采样周期和使能标志决定何时执行采样，并将采样结果保存到全局监控数据结构中
*/
static void BMS_MonitorBattery(void)
{
    CountVoltage += MONITOR_TASK_PERIOD; // 每次任务周期增加电压计时器
    if(CountVoltage >= UPDATE_CELL_VOLTAGE_CYCLE && FlagVoltage)
    {
        CountVoltage = 0; // 重置电压计时器
        BMS_HalMonitorBattery(); 
    }

    CountCellTemp += MONITOR_TASK_PERIOD; // 每次任务周期增加电芯温度计时器
    if(CountCellTemp >= UPDATE_CELL_TEMP_CYCLE && FlagCellTemp)
    {
        CountCellTemp = 0;
        Bms_HalMonitorCellTemperature();
    }

    CountBatCurrent += MONITOR_TASK_PERIOD; // 每次任务周期增加电流计时器
    if(CountBatCurrent >= UPDATE_BAT_CURRENT_CYCLE && FlagBatCurrent)
    {
        CountBatCurrent = 0;
        Bms_HalMonitorBatteryCurrent();
    }
}

static void BMS_MonitorSysMode(void)
{
    if(BMS_MonitorData.BatteryCurrent > SYS_MODE_DECISION_CURRENT)
    {
        BMS_GlobalParam.SysMode = BMS_MODE_DISCHARGE; // 放电模式
    }
    else if(BMS_MonitorData.BatteryCurrent < -SYS_MODE_DECISION_CURRENT)
    {
        BMS_GlobalParam.SysMode = BMS_MODE_CHARGE; // 充电模式
    }
    else
    {
        if(BMS_GlobalParam.SysMode != BMS_MODE_SLEEP)
        {
            BMS_GlobalParam.SysMode = BMS_MODE_STANDBY; // 待机模式
        }
    }

    if(BMS_GlobalParam.SysMode == BMS_MODE_STANDBY)
    {
        // CountSysModeSleep += MONITOR_TASK_PERIOD; // 待机模式下增加睡眠计时器
        // if(CountSysModeSleep >= 60000) // 60秒无充放电活动进入睡眠模式
        // {
        //     BMS_GlobalParam.SysMode = BMS_MODE_SLEEP; // 睡眠模式
        //     CountSysModeSleep = 0; // 重置睡眠计时器
        // }
    }
}

void BMS_MonitorStateVoltage(BMS_StateTypedef NewState)
{
    if(NewState == BMS_STATE_ENABLE)
    {
        FlagVoltage = true; // 使能单体电芯电压采样
    }
    else
    {
        FlagVoltage = false; // 禁止单体电芯电压采样
    }
}

void BMS_MonitorStateBatCurrent(BMS_StateTypedef NewState)
{
    if(NewState == BMS_STATE_ENABLE)
    {
        FlagBatCurrent = true; // 使能电流采样
    }
    else
    {
        FlagBatCurrent = false; // 禁止电流采样
    }
}

void BMS_MonitorStateCellTemp(BMS_StateTypedef NewState)
{
    if(NewState == BMS_STATE_ENABLE)
    {
        FlagCellTemp = true; // 使能单体电芯温度采样
    }
    else
    {
        FlagCellTemp = false; // 禁止单体电芯温度采样
    }
}
