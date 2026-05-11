/*
对bq769数据采集
各节电压、电流、温度等数据的采集和处理
*/

#include <stdio.h>
#include <rtthread.h>
#include "bq769.h"
#include "bms_monitor.h"

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
BMS_MonitorDataTypedef BMS_MonitorData;

// 采样使能标志：为 true 时允许对应采样执行
static bool FlagVoltage = true;     // 电压采样开关,单体和总电压共用一个标志
static bool FlagCellTemp = true;    // 电芯温度采样开关
static bool FlagBatCurrent = true;  // 电流采样开关（软件/硬件触发取决于实现）

// 时间计数器：以毫秒为单位累计，用于判断是否到达采样周期
static uint16_t CountVoltage = 0;     // 电压计时器（ms）单节和总都用同一个计时器，因为它们的采样周期相同
static uint16_t CountCellTemp = 0;    // 电芯温度计时器（ms）
static uint16_t CountBatCurrent = 0; // 电流计时器（ms）
static uint16_t CountSysModeSleep = 0; // 系统模式切换睡眠计时器（ms）

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

        rt_thread_delay(MONITOR_TASK_PERIOD); // 延时，控制任务周期
        rt_kprintf("%d.%03dV, %d.%03dA\n", (int)BMS_MonitorData.BatteryVoltage, (int)(BMS_MonitorData.BatteryVoltage * 1000) % 1000,
               (int)BMS_MonitorData.BatteryCurrent, (int)(BMS_MonitorData.BatteryCurrent * 1000) % 1000);
        //打印单节电压
        for(uint8_t i = 0; i < BQ769X0_CELL_MAX; i++)
        {
            rt_kprintf("Cell%d: %d.%03dV\n", i + 1, (int)BMS_MonitorData.CellVoltage[i], (int)(BMS_MonitorData.CellVoltage[i] * 1000) % 1000);
        }
    }
}

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
        BQ769X0_UpdateCellVolt(); // 更新单体电芯电压
        BQ769X0_UpdateBatVolt(); // 更新电池组电压

        BMS_MonitorData.BatteryVoltage = BQ769X0_SampleData.BatteryVoltage; // 保存电池组总电压到监控数据

        for(uint8_t i = 0; i < BQ769X0_CELL_MAX; i++)
        {
            BMS_MonitorData.CellVoltage[i] = BQ769X0_SampleData.CellVoltage[i];// 保存未排序的电芯电压
            BMS_MonitorData.CellData[i].CellVoltage = BQ769X0_SampleData.CellVoltage[i];// 保存电芯电压到CellData结构
            BMS_MonitorData.CellData[i].CellNumber = i + 1;// 电芯编号
        }
    }

    CountCellTemp += MONITOR_TASK_PERIOD; // 每次任务周期增加电芯温度计时器
    if(CountCellTemp >= UPDATE_CELL_TEMP_CYCLE && FlagCellTemp)
    {
        CountCellTemp = 0; // 重置电芯温度计时器
        BQ769X0_UpdateTsTemp(); // 更新单体电芯温度

        for(uint8_t i = 0; i < BQ769X0_TMEP_MAX; i++)
        {
            BMS_MonitorData.CellTemp[i] = BQ769X0_SampleData.TsxTemperature[i]; // 保存单体电芯温度到监控数据
        }
    }

    CountBatCurrent += MONITOR_TASK_PERIOD; // 每次任务周期增加电流计时器
    if(CountBatCurrent >= UPDATE_BAT_CURRENT_CYCLE && FlagBatCurrent)
    {
        CountBatCurrent = 0; // 重置电流计时器
        BQ769X0_UpdateCurrent(); // 更新电池组电流

        BMS_MonitorData.BatteryCurrent = BQ769X0_SampleData.BatteryCurrent; // 保存电池组总电流到监控数据
    }
}

static void BMS_MonitorSysMode(void)
{
    static BMS_SysModeTypedef SysMode = BMS_MODE_NULL;
    
    if(BMS_MonitorData.BatteryCurrent > SYS_MODE_DECISION_CURRENT)
    {
        SysMode = BMS_MODE_DISCHARGE; // 放电模式
    }
    else if(BMS_MonitorData.BatteryCurrent < -SYS_MODE_DECISION_CURRENT)
    {
        SysMode = BMS_MODE_CHARGE; // 充电模式
    }
    else
    {
        SysMode = BMS_MODE_STANDBY; // 待机模式
    }

    if(SysMode == BMS_MODE_STANDBY)
    {
        CountSysModeSleep += MONITOR_TASK_PERIOD; // 待机模式下增加睡眠计时器
        if(CountSysModeSleep >= 60000) // 60秒无充放电活动进入睡眠模式
        {
            SysMode = BMS_MODE_SLEEP; // 睡眠模式
            CountSysModeSleep = 0; // 重置睡眠计时器
        }
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
