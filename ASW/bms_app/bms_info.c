#include <stdio.h>
#include <string.h>
#include <rtthread.h>
#include "stm32f10x.h"
#include "bms_global_define.h"
#include "bms_info.h"
#include "bms_monitor.h"
#include "bms_analysis.h"
#include "bms_balance.h"
#include "Com_Cfg.h"
#include "Rte.h"

#define DBG_TAG "info"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"

#define LED1_PORT     GPIOB
#define LED1_PIN      GPIO_Pin_6
#define LED2_PORT     GPIOB
#define LED2_PIN 	  GPIO_Pin_7
#define LED3_PORT     GPIOB
#define LED3_PIN      GPIO_Pin_8
#define LED4_PORT     GPIOB
#define LED4_PIN      GPIO_Pin_9

#define INFO_TASK_STACK_SIZE	1024// 线程栈大小
#define INFO_TASK_PRIORITY		20// 线程优先级
#define INFO_TASK_TIMESLICE	    25// 线程时间片（单位：ms）
#define INFO_TASK_PERIOD		3000// 信息显示任务周期（单位：ms）

#define RTE_COM_RX_PERIOD_MS 10u

static void rte_com_task(void *parameter); // RTE通信任务函数声明
static bool FlagInfoPrintf = true; // 信息打印使能标志，为 true 时允许信息打印执行

/* 将浮点数转为整数+小数格式的字符串，避免使用 %f 从而不引入浮点 printf 库 */
static void format_float_3(char *buf, float val)
{
    int scaled = (int)(val * 1000.0f + (val >= 0 ? 0.5f : -0.5f));
    int int_part = scaled / 1000;
    int frac_part = scaled % 1000;
    if (frac_part < 0) frac_part = -frac_part;
    sprintf(buf, "%d.%03d", int_part, frac_part);
}

static void format_float_1(char *buf, float val)
{
    int scaled = (int)(val * 10.0f + (val >= 0 ? 0.5f : -0.5f));
    int int_part = scaled / 10;
    int frac_part = scaled % 10;
    if (frac_part < 0) frac_part = -frac_part;
    sprintf(buf, "%d.%d", int_part, frac_part);
}

static void BMS_InfoLedInit(void);
static void BMS_InfoTask(void *parameter); // 信息显示线程函数声明

void BMS_InfoInit(void)
{
	BMS_InfoLedInit(); // 初始化LED
    // 创建信息显示线程，指定线程函数、参数、栈大小、优先级和时间片
    rt_thread_t info_thread = rt_thread_create("bms_info", BMS_InfoTask,
                                                 NULL, INFO_TASK_STACK_SIZE, 
                                                 INFO_TASK_PRIORITY, 
                                                 INFO_TASK_TIMESLICE);
    if (info_thread != RT_NULL)
    {
        rt_thread_startup(info_thread); // 启动信息显示线程
        LOG_I("BMS Info thread created and started successfully.");
    }
    else
    {
        LOG_E("Failed to create BMS Info thread.");
    }

	/*创建can通信任务*/
    rt_thread_t rte_com_thread = rt_thread_create("rte_com", rte_com_task, NULL, 512, 22, 20);
    if (rte_com_thread != RT_NULL)
    {
        rt_thread_startup(rte_com_thread);
    }
}

static void rte_com_task(void *parameter)
{
    rt_tick_t last_tx_tick = rt_tick_get();
    const rt_tick_t tx_period_ticks = rt_tick_from_millisecond(COM_BMS_STATUS_PERIOD_MS);
    const rt_tick_t rx_period_ticks = rt_tick_from_millisecond(RTE_COM_RX_PERIOD_MS);

    (void)parameter;

    while (1)
    {
        Rte_MainFunction_ComRx(); //进行接收处理

        if ((rt_tick_get() - last_tx_tick) >= tx_period_ticks)
        {
            (void)Rte_MainFunction_ComTx();
            last_tx_tick = rt_tick_get();
        }

        rt_thread_delay(rx_period_ticks);
    }
}


static void BMS_InfoLedInit(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;	
	/* 配置LED引脚为推挽输出 */
	GPIO_InitStruct.GPIO_Pin = LED1_PIN | LED2_PIN | LED3_PIN | LED4_PIN;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(LED1_PORT, &GPIO_InitStruct);
	
	/* 初始化LED状态为关闭 */
	GPIO_SetBits(LED1_PORT, LED1_PIN | LED2_PIN | LED3_PIN | LED4_PIN);
}

static void BMS_InfoLed(void)
{
	if (BMS_AnalysisData.SOC == 0)
	{
		GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET);// 全部LED熄灭
		GPIO_WriteBit(LED2_PORT, LED2_PIN, Bit_SET);
		GPIO_WriteBit(LED3_PORT, LED3_PIN, Bit_SET);
		GPIO_WriteBit(LED4_PORT, LED4_PIN, Bit_SET);
	}
	else if(BMS_AnalysisData.SOC > 0 && BMS_AnalysisData.SOC <= 0.25)
	{
		GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET); 
		GPIO_WriteBit(LED2_PORT, LED2_PIN, Bit_SET);   
		GPIO_WriteBit(LED3_PORT, LED3_PIN, Bit_SET);   
		GPIO_WriteBit(LED4_PORT, LED4_PIN, Bit_RESET); 
	}
	else if(BMS_AnalysisData.SOC > 0.25 && BMS_AnalysisData.SOC <= 0.5)
	{
		GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET); 
		GPIO_WriteBit(LED2_PORT, LED2_PIN, Bit_SET);
		GPIO_WriteBit(LED3_PORT, LED3_PIN, Bit_RESET);
		GPIO_WriteBit(LED4_PORT, LED4_PIN, Bit_RESET); 
	}
	else if(BMS_AnalysisData.SOC > 0.5 && BMS_AnalysisData.SOC <= 0.75)
	{
		GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_SET);
		GPIO_WriteBit(LED2_PORT, LED2_PIN, Bit_RESET);
		GPIO_WriteBit(LED3_PORT, LED3_PIN, Bit_RESET);
		GPIO_WriteBit(LED4_PORT, LED4_PIN, Bit_RESET);
	}
	else if(BMS_AnalysisData.SOC > 0.75 && BMS_AnalysisData.SOC <= 1)
	{
		GPIO_WriteBit(LED1_PORT, LED1_PIN, Bit_RESET); // 全部LED亮
		GPIO_WriteBit(LED2_PORT, LED2_PIN, Bit_RESET);
		GPIO_WriteBit(LED3_PORT, LED3_PIN, Bit_RESET);
		GPIO_WriteBit(LED4_PORT, LED4_PIN, Bit_RESET);
	}
}

// 实时打印BMS信息（使用整数格式化避免引入浮点 printf 库）
static void BMS_InfoPrintf(void)
{
	uint8_t index;
	char str[64];
	char fbuf[16]; // 浮点数格式化缓冲区

	LOG_D("/*************************************************************/");
	//系统模式
	sprintf(str, "System Mode = %s", BMS_GlobalParam.SysMode == BMS_MODE_SLEEP ? "Sleep" : (BMS_GlobalParam.SysMode == BMS_MODE_STANDBY ? "Standby" : (BMS_GlobalParam.SysMode == BMS_MODE_CHARGE ? "Charge" : (BMS_GlobalParam.SysMode == BMS_MODE_DISCHARGE ? "Discharge" : "Error"))));
	LOG_D("%s", str);
	rt_kprintf("\r\n");
	// 电池包实际容量
	format_float_3(fbuf, BMS_AnalysisData.CapacityReal);
	sprintf(str, "Battery Real Capacity = %sAh", fbuf);
	LOG_D("%s", str);

	// 电池包剩余容量
	format_float_3(fbuf, BMS_AnalysisData.CapacityRemain);
	sprintf(str, "Battery Remain Capacity = %sAh", fbuf);
	LOG_D("%s", str);
	rt_kprintf("\r\n");

	// SOC
	format_float_1(fbuf, BMS_AnalysisData.SOC * 100.0f);
	sprintf(str, "Battery SOC = %s%%", fbuf);
	LOG_D("%s", str);

	/*
	// SOH
	sprintf(str, "Battery SOH = %0.1f%", BMS_AnalysisData.SOH * 100);
	BMS_INFO("%s", str);

	// SOE
	sprintf(str, "Battery SOE = %0.1f%", BMS_AnalysisData.SOE * 100);
	BMS_INFO("%s", str);

	// SOP
	sprintf(str, "Battery SOP = %0.1f%", BMS_AnalysisData.SOP * 100);
	BMS_INFO("%s", str);
	*/	
	// rt_kprintf("\r\n");

	// 单体电芯最大电压
	format_float_3(fbuf, BMS_AnalysisData.CellVoltMax);
	sprintf(str, "Cell Max Voltage = %sV", fbuf);
	LOG_D("%s", str);

	// 单体电芯最小电压
	format_float_3(fbuf, BMS_AnalysisData.CellVoltMin);
	sprintf(str, "Cell Min Voltage = %sV", fbuf);
	LOG_D("%s", str);

	// 最大电压差
	format_float_3(fbuf, BMS_AnalysisData.MaxVoltageDifference);
	sprintf(str, "Cell Max Voltage Difference = %sV", fbuf);
	LOG_D("%s", str);

	// 平均电压
	format_float_3(fbuf, BMS_AnalysisData.AverageVoltage);
	sprintf(str, "Cell Average Voltage = %sV", fbuf);
	LOG_D("%s", str);

	// 实时功率
	format_float_3(fbuf, BMS_AnalysisData.PowerReal);
	sprintf(str, "Battery Real Power = %sW", fbuf);
	LOG_D("%s", str);
	rt_kprintf("\r\n");

	// 电池总电压
	format_float_3(fbuf, BMS_MonitorData.BatteryVoltage);
	sprintf(str, "Battery Voltage = %sV", fbuf);
	LOG_D("%s", str);	

	// 电池组电流
	format_float_3(fbuf, BMS_MonitorData.BatteryCurrent);
	sprintf(str, "Battery Current = %sA", fbuf);
	LOG_D("%s", str);

	// 温度
	for (index = 0; index < BMS_MonitorData.CellTempEffectiveNumber; index++)
	{
		format_float_1(fbuf, BMS_MonitorData.CellTemp[index]);
		sprintf(str, "Tempature %d = %s", index + 1, fbuf);
		LOG_D("%s", str);
	}
	// rt_kprintf("\r\n");

	// 电芯电压
	for (index = 0; index < BMS_GlobalParam.Cell_Real_Number; index++)
	{
		format_float_3(fbuf, BMS_MonitorData.CellVoltage[index]);
		sprintf(str, "Cell%-2d Voltage = %-5sV %s",
		index + 1, 
		fbuf,
		(BMS_BalanceData.BalanceRecord &  (1 << index)) > 0 ? "--->" : "");
		LOG_D("%s", str);
	}
	LOG_D("/*************************************************************/\r\n\r\n");
}

static void BMS_InfoTask(void *parameter)
{
    while(1)
    {
        BMS_InfoLed(); // 根据电量状态控制LED
        if(FlagInfoPrintf)
        {
            BMS_InfoPrintf(); // 实时打印BMS信息
        }

        rt_thread_delay(INFO_TASK_PERIOD); // 延时，控制任务周期
    }
}

/*由shell命令控制*/
void BMS_InfoControlPrintf(BMS_StateTypedef NewState)
{
    if(NewState == BMS_STATE_ENABLE)
    {
        FlagInfoPrintf = true; // 使能信息打印
    }
    else
    {
        FlagInfoPrintf = false; // 禁止信息打印
    }
}

// void BMS_InfoControl(int argc, char **argv)
// {
// 	if(argc == 2)
// 	{
// 		if(strcmp(argv[1], "on") == 0)
// 		{
// 			BMS_InfoControlPrintf(BMS_STATE_ENABLE); // 使能信息打印
// 		}
// 		else if(strcmp(argv[1], "off") == 0)
// 		{
// 			BMS_InfoControlPrintf(BMS_STATE_DISABLE); // 禁止信息打印
// 		}
// 		else
// 		{
// 			LOG_W("Invalid argument. Use 'on' or 'off'.");
// 		}
// 	}
// }
// MSH_CMD_EXPORT(BMS_InfoControl, Control BMS information printing on/off);
