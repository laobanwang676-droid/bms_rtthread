#include <stdio.h>
#include <stdbool.h>
#include <rtthread.h>

#include "bms_protect.h"
#include "bms_monitor.h"
#include "bq769.h"
#include "bms_control.h"

#define DBG_TAG "protect"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"

// thread config
#define PROTECT_TASK_STACK_SIZE	256
#define PROTECT_TASK_PRIORITY	20
#define PROTECT_TASK_TIMESLICE	25

#define PROTECT_TASK_PERIOD		200
 
BMS_ProtectTypedef BMS_Protect = 
{
	.alert = FlAG_ALERT_NO,   // 无报警触发
	.param = 
	{
		.ShoutdownVoltage = INIT_SHUTDOWN_VOLTAGE,  // 关机电压(V)

		.OVProtect	= INIT_OV_PROTECT,     // 单体过压保护电压
		.OVRelieve	= INIT_OV_RELIEVE,     //单体过压解除电压
		.UVProtect	= INIT_UV_PROTECT,     //单体欠压保护电压
		.UVRelieve	= INIT_UV_RELIEVE,     //单体欠压恢复电压

		.OCCProtect = INIT_OCC_MAX,
		.OCDProtect = INIT_OCD_MAX,

		.OVDelay	= INIT_OV_DELAY,
		.UVDelay	= INIT_UV_DELAY,
		.OCDDelay	= INIT_OCD_DELAY,
		.SCDDelay	= INIT_SCD_DELAY,

		.OCDRelieve = INIT_OCD_RELIEVE,
		.SCDRelieve = INIT_SCD_RELIEVE,
		.OCCDelay	= INIT_OCC_DELAY,
		.OCCRelieve = INIT_OCC_RELIEVE,

		.OTCProtect = INIT_OTC_PROTECT,
		.OTCRelieve = INIT_OTC_RELIEVE,
		.OTDProtect = INIT_OTD_PROTECT,
		.OTDRelieve = INIT_OTD_RELIEVE,

		.LTCProtect = INIT_LTC_PROTECT,
		.LTCRelieve = INIT_LTC_RELIEVE,
		.LTDProtect = INIT_LTD_PROTECT,
		.LTDRelieve = INIT_LTD_RELIEVE,
	}
};
static void BMS_ProtectTask(void *parameter);

void BMS_ProtectInit(void)
{
	// 创建保护线程，指定线程函数、参数、栈大小、优先级和时间片
	rt_thread_t protect_thread = rt_thread_create("bms_protect", BMS_ProtectTask,
												 NULL, PROTECT_TASK_STACK_SIZE, 
												 PROTECT_TASK_PRIORITY, 
												 PROTECT_TASK_TIMESLICE);
	if (protect_thread != RT_NULL)
	{
		rt_thread_startup(protect_thread); // 启动保护线程
		LOG_I("BMS Protect thread created and started successfully.");
	}
	else
	{
		LOG_E("Failed to create BMS Protect thread.");
	}
}

//充电软件监控
//过流 过温 低温
static void BMS_ChargeMonitor(void)
{
	static uint16_t OCC_time = 0;//需要延时再判断
	// 充电过压保护
	if(BMS_MonitorData.BatteryCurrent < -BMS_Protect.param.OCCProtect)
	{
		OCC_time += PROTECT_TASK_PERIOD;
		if(OCC_time >= BMS_Protect.param.OCCDelay * 1000)
		{
			BMS_HalCtrlCharge(BMS_STATE_DISABLE); // 充电过流时关闭充电MOS
			BMS_Protect.alert |= FlAG_ALERT_OCC; // 设置充电过流报警标志
			OCC_time = 0;//重置计时器
			LOG_W("Charge:OCC Protect Tigger");
		}
	}

	if(BMS_MonitorData.CellTempEffectiveNumber == 0)
	{
		return;//没有有效温度数据时不进行过温和低温判断
	}
	else if(BMS_MonitorData.CellTemp[BMS_MonitorData.CellTempEffectiveNumber - 1] < BMS_Protect.param.LTCProtect)
	{
		BMS_HalCtrlCharge(BMS_STATE_DISABLE); // 充电过低温时关闭充电MOS
		BMS_Protect.alert |= FlAG_ALERT_LTC; // 设置充电低温报警标志
		LOG_W("Charge:LTC Protect Tigger");
	}
	else if(BMS_MonitorData.CellTemp[BMS_MonitorData.CellTempEffectiveNumber - 1] > BMS_Protect.param.OTCProtect)
	{
		BMS_HalCtrlCharge(BMS_STATE_DISABLE); // 充电过温时关闭充电MOS
		BMS_Protect.alert |= FlAG_ALERT_OTC; // 设置充电过温报警标志
		LOG_W("Charge:OTC Protect Tigger");
	}
}

//放电软件监控
//过温 低温
static void BMS_DischargeMonitor(void)
{
	if(BMS_MonitorData.CellTempEffectiveNumber == 0)
	{
		return;//没有有效温度数据时不进行过温和低温判断
	}
	else if(BMS_MonitorData.CellTemp[BMS_MonitorData.CellTempEffectiveNumber - 1] < BMS_Protect.param.LTDProtect)
	{
		BMS_HalCtrlDischarge(BMS_STATE_DISABLE); // 放电过低温时关闭放电MOS
		BMS_Protect.alert |= FlAG_ALERT_LTD; // 设置放电低温报警标志
		LOG_W("Discharge:LTD Protect Tigger");
	}
	else if(BMS_MonitorData.CellTemp[BMS_MonitorData.CellTempEffectiveNumber - 1] > BMS_Protect.param.OTDProtect)
	{
		BMS_HalCtrlDischarge(BMS_STATE_DISABLE); // 放电过温时关闭放电MOS
		BMS_Protect.alert |= FlAG_ALERT_OTD; // 设置放电过温报警标志
		LOG_W("Discharge:OTD Protect Tigger");
	}
}

// 充电过压(OV)硬件触发
void BMS_ProtectHwOV(void)
{
	if ((BMS_Protect.alert & FlAG_ALERT_OV) == FlAG_ALERT_NO) // 判断是为了防止多次触发
	{
		BMS_HalCtrlCharge(BMS_STATE_DISABLE);
		BMS_Protect.alert |= FlAG_ALERT_OV;
		LOG_W("Charge:OV Protect Tigger");
	}
}


// 放电过流(OCD)硬件触发
void BMS_ProtectHwOCD(void)
{
	if ((BMS_Protect.alert & FlAG_ALERT_OCD) == FlAG_ALERT_NO) // 判断是为了防止多次触发
	{
		BMS_HalCtrlDischarge(BMS_STATE_DISABLE);
		BMS_Protect.alert |= FlAG_ALERT_OCD;
		LOG_W("Discharge:OCD Protect Tigger");
	}
}

// 放电短路(SCD)硬件触发
void BMS_ProtectHwSCD(void)
{
	if ((BMS_Protect.alert & FlAG_ALERT_SCD) == FlAG_ALERT_NO) // 判断是为了防止多次触发
	{
		BMS_HalCtrlDischarge(BMS_STATE_DISABLE);
		BMS_Protect.alert |= FlAG_ALERT_SCD;
		LOG_W("Discharge:SCD Protect Tigger");
	}
}

// 放欠过压(UV)硬件触发
void BMS_ProtectHwUV(void)
{
	if ((BMS_Protect.alert & FlAG_ALERT_UV) == FlAG_ALERT_NO) // 判断是为了防止多次触发
	{
		BMS_HalCtrlDischarge(BMS_STATE_DISABLE);
		BMS_Protect.alert |= FlAG_ALERT_UV;
		LOG_W("Discharge:UV Protect Tigger");
	}
}

void BMS_ProtectHwDevice(void)
{

}

void BMS_ProtectHwOvrd(void)
{
	
}

//根据系统模式进行相应的保护监控
static void BMS_ProtectMode(void)
{
	switch(BMS_GlobalParam.SysMode)
	{
		case BMS_MODE_CHARGE:
			BMS_ChargeMonitor();
			break;
		
		case BMS_MODE_DISCHARGE:
			BMS_DischargeMonitor();
			break;
		
		default:
			break;
	}
}

//保护标志的清除
static void BMS_ProtectRelieveMonitor(void)
{
	static uint32_t RelieveCountCHG = 0, RelieveCountDSG = 0;
	
	// 充电保护的解除条件判断
	if (BMS_Protect.alert != FlAG_ALERT_NO)
	{
		if (BMS_Protect.alert & FlAG_ALERT_OV)
		{
			if (BMS_MonitorData.CellData[BMS_CELL_MAX-1].CellVoltage < BMS_Protect.param.OVRelieve)
			{
				BMS_Protect.alert &= ~FlAG_ALERT_OV;
				
				LOG_I("Charge:OV Relieve");
			}
		}
		else if (BMS_Protect.alert & FlAG_ALERT_OTC)
		{
			if (BMS_MonitorData.CellTemp[BMS_TEMP_MAX-1] < BMS_Protect.param.OTCRelieve)
			{
				BMS_Protect.alert &= ~FlAG_ALERT_OTC;
				BMS_HalCtrlCharge(BMS_GlobalParam.Charge);//这里不传入具体控制，而是传入期望值，是在shell命令中控制是否重新开启充电
				
				LOG_I("Charge:OTC Relieve");
			}
		}
		else if (BMS_Protect.alert & FlAG_ALERT_LTC)
		{
			if (BMS_MonitorData.CellTemp[0] > BMS_Protect.param.LTCRelieve)
			{
				BMS_Protect.alert &= ~FlAG_ALERT_LTC;
				BMS_HalCtrlCharge(BMS_GlobalParam.Charge);
				
				LOG_I("Charge:LTC Relieve");
			}
		}
		else if (BMS_Protect.alert & FlAG_ALERT_OCC)
		{
			RelieveCountCHG += PROTECT_TASK_PERIOD;
			if (RelieveCountCHG / 1000 >= BMS_Protect.param.OCCRelieve)
			{
				RelieveCountCHG = 0;

				BMS_Protect.alert &= ~FlAG_ALERT_OCC;
				BMS_HalCtrlCharge(BMS_GlobalParam.Charge);
				
				LOG_I("Charge:OCC Relieve");
			}
		}



		
		//放电保护的解除条件判断
		if (BMS_Protect.alert & FlAG_ALERT_UV)
		{
			if (BMS_MonitorData.CellData[0].CellVoltage > BMS_Protect.param.UVRelieve)
			{
				BMS_Protect.alert &= ~FlAG_ALERT_UV;
				
				LOG_I("Discharge:UV Relieve");
			}
		}
		else if (BMS_Protect.alert & FlAG_ALERT_OTD)
		{
			if (BMS_MonitorData.CellTemp[BMS_TEMP_MAX-1] < BMS_Protect.param.OTDRelieve)
			{
				BMS_Protect.alert &= ~FlAG_ALERT_OTD;
				BMS_HalCtrlDischarge(BMS_GlobalParam.Discharge);
				
				LOG_I("Discharge:OTD Relieve");
			}
		}
		else if (BMS_Protect.alert & FlAG_ALERT_LTD)
		{
			if (BMS_MonitorData.CellTemp[0] > BMS_Protect.param.LTDRelieve)
			{
				BMS_Protect.alert &= ~FlAG_ALERT_LTD;
				BMS_HalCtrlDischarge(BMS_GlobalParam.Discharge);
				
				LOG_I("Discharge:LTD Relieve");
			}
		}
		else if (BMS_Protect.alert & FlAG_ALERT_OCD)
		{
			RelieveCountDSG += PROTECT_TASK_PERIOD;
			if (RelieveCountDSG / 1000 >= BMS_Protect.param.OCDRelieve)
			{
				RelieveCountDSG = 0;

				BMS_Protect.alert &= ~FlAG_ALERT_OCD;
				BMS_HalCtrlDischarge(BMS_GlobalParam.Discharge);

				LOG_I("Discharge:OCD Relieve");
			}
		}
		else if (BMS_Protect.alert & FlAG_ALERT_SCD)
		{
			RelieveCountDSG += PROTECT_TASK_PERIOD;
			if (RelieveCountDSG / 1000 >= BMS_Protect.param.SCDRelieve)
			{
				RelieveCountDSG = 0;

				BMS_Protect.alert &= ~FlAG_ALERT_SCD;		
				BMS_HalCtrlDischarge(BMS_GlobalParam.Discharge);

				LOG_I("Discharge:SCD Relieve");
			}
		}
	}
}

static void BMS_ProtectTask(void *parameter)
{
	while (1)
	{
		BMS_ProtectMode();
		BMS_ProtectRelieveMonitor();
		
		rt_thread_mdelay(PROTECT_TASK_PERIOD);
	}
}
