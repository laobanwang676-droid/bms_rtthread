#include <stdio.h>
#include <stdbool.h>


#include "bms_balance.h"
#include "bms_control.h"
#include "bms_monitor.h"
#include "bms_analysis.h"
#include "bms_Protect.h"

#define DBG_TAG "Balance"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"


// thread config
#define BALANCE_TASK_STACK_SIZE	512  //任务栈
#define BALANCE_TASK_PRIORITY	22    //任务优先级
#define BALANCE_TASK_TIMESLICE	25   //任务时间片
#define BALANCE_TASK_PERIOD		200   //任务周期，单位ms

static bool BalanceFlag = false; // 均衡标志，true表示正在均衡，false表示不均衡
uint32_t BalanceVoltRiseTime = 0; // 均衡电压上升时间，单位ms
rt_timer_t BalanceTime = 0; // 均衡时间计数器，单位ms

static BMS_StateTypedef BMS_CHGStateBackup; // 充电状态备份,用于快速充放电命令的执行
static BMS_StateTypedef BMS_DSGStateBackup;

BMS_BalanceDataTypedef BMS_BalanceData = 
{
	.SocStopChg   	= SOC_STOP_CHG_VALUE,    // 停止充电SOC值
	.SocStartChg  	= SOC_START_CHG_VALUE,    // 启动充电SOC值
	.SocStopDsg   	= SOC_STOP_DSG_VALUE,     // 停止放电SOC值
	.SocStartDsg  	= SOC_START_DSG_VALUE,     // 启动放电SOC值

	.BalanceStartVoltage = INIT_BALANCE_VOLTAGE,  // 均衡起始电压(V)
	.BalanceDiffeVoltage = BALANCE_DIFFE_VOLTAGE,   // 均衡差异电压(V)
	.BalanceCycleTime 	 = BALANCE_CYCLE_TIME,// 均衡周期时间(s)
	.BalanceRecord 		 = BMS_CELL_NULL,    // 均衡记录,正在均衡的会被位与上
	.BalanceReleaseFlag = false,             // 表示均衡条件是否满足
};

static void BMS_BalanceTask(void *param);
static void BalanceTimerCallback(void *parameter);

void BMS_BalanceInit(void)
{
	// 创建均衡线程，指定线程函数、参数、栈大小、优先级和时间片
	rt_thread_t balance_thread = rt_thread_create("bms_balance", BMS_BalanceTask,
												 NULL, BALANCE_TASK_STACK_SIZE, 
												 BALANCE_TASK_PRIORITY, 
												 BALANCE_TASK_TIMESLICE);
	
	BalanceTime = rt_timer_create("balance_timer", BalanceTimerCallback, NULL, BALANCE_TASK_PERIOD, RT_TIMER_FLAG_ONE_SHOT); // 创建均衡定时器，指定回调函数、参数、时间和单次触发模式

	if (balance_thread != RT_NULL)
	{
		rt_thread_startup(balance_thread); // 启动均衡线程
		LOG_I("BMS Balance thread created and started successfully.");
	}
	else
	{
		LOG_E("Failed to create BMS Balance thread.");
	}

	if(BalanceTime != RT_NULL)
	{
		LOG_I("BMS Balance timer created and started successfully.");
	}
	else
	{
		LOG_E("Failed to create BMS Balance timer.");
	}
}

static void BalanceTimerCallback(void *parameter)
{
	BMS_HalCtrlCellsBalance(BMS_CELL_ALL, BMS_STATE_DISABLE); // 
	BalanceFlag = false; // 先将均衡标志位置为false,后面根据均衡条件检查结果修改均衡标志位
	BMS_BalanceData.BalanceRecord = BMS_CELL_NULL; // 先将均衡记录清零,后面根据均衡条件检查结果修改均衡记录

	BalanceVoltRiseTime = 0;
	BalanceVoltRiseTime = rt_tick_from_millisecond(BALANCE_TASK_PERIOD) + rt_tick_get(); // 计算均衡电压上升时间，当前时间加上均衡周期时间
}

/*检查均衡条件是否满足*/
static bool BMS_BalanceCheck(void)
{
	if(BalanceVoltRiseTime >= rt_tick_get())
	{
		return false; // 均衡电压还未上升到位
	}

	if(BalanceFlag == true)
	{
		return false; // 已经在均衡
	}

	if(BMS_GlobalParam.SysMode != BMS_MODE_STANDBY && BMS_GlobalParam.SysMode != BMS_MODE_CHARGE)
	{
		BMS_BalanceData.BalanceReleaseFlag = false;
		return false; // 只有在待机和充电模式下才考虑均衡
	}

	if(BMS_MonitorData.CellData[BMS_GlobalParam.Cell_Real_Number - 1].CellVoltage < BMS_BalanceData.BalanceStartVoltage)
	{
		BMS_BalanceData.BalanceReleaseFlag = false;
		return false; // 最高电压未达到均衡起始电压
	}

	if(BMS_AnalysisData.MaxVoltageDifference < BMS_BalanceData.BalanceDiffeVoltage)
	{
		BMS_BalanceData.BalanceReleaseFlag = false;
		return false; // 电压差异未达到均衡条件
	}

	BMS_BalanceData.BalanceReleaseFlag = true; // 满足均衡条件

	return true; // 满足均衡条件
}

// 充放电管理
static void BMS_BalanceChgDsgManage(void)
{
	switch(BMS_GlobalParam.SysMode)
	{
		case BMS_MODE_CHARGE:
		{
			if (BMS_AnalysisData.SOC >= BMS_BalanceData.SocStopChg)
			{
				BMS_HalCtrlCharge(BMS_STATE_DISABLE);

				LOG_I("Stop Charge");
			}
		}break;

		case BMS_MODE_DISCHARGE:
		{
			if (BMS_AnalysisData.SOC <= BMS_BalanceData.SocStopDsg)
			{
				BMS_HalCtrlDischarge(BMS_STATE_DISABLE);

				LOG_I("Stop Discharge");
			}
		}break;

		case BMS_MODE_STANDBY:
		{
			if (BMS_GlobalParam.Charge == BMS_STATE_ENABLE)	
			{
				if ((BMS_Protect.alert & FLAG_ALERT_CHG_MASK) == FlAG_ALERT_NO)
				{
					if (BMS_BalanceData.BalanceReleaseFlag != true)
					{
						if (BMS_AnalysisData.SOC < BMS_BalanceData.SocStartChg)
						{
							BMS_HalCtrlCharge(BMS_STATE_ENABLE);
							
							LOG_I("Start Charge");
						}
					}
				}
			}


			if (BMS_GlobalParam.Discharge == BMS_STATE_ENABLE) 
			{
				if ((BMS_Protect.alert & FLAG_ALERT_DSG_MASK) == FlAG_ALERT_NO)
				{
					if (BMS_AnalysisData.SOC > BMS_BalanceData.SocStartDsg)
					{
						BMS_HalCtrlDischarge(BMS_STATE_ENABLE);
						
						LOG_I("Start Discharge");
					}
				}
			}
		}break;	
		default:;break;
	}

	// 可通过命令快速关闭充放电
	if (BMS_CHGStateBackup != BMS_GlobalParam.Charge)
	{
		if (BMS_GlobalParam.Charge == BMS_STATE_DISABLE)
		{
			BMS_HalCtrlCharge(BMS_STATE_DISABLE);
		}
		else if (BMS_GlobalParam.SysMode == BMS_MODE_SLEEP)  // 睡眠模式下可开启充电
		{
			BMS_HalCtrlCharge(BMS_STATE_ENABLE);
		}
		BMS_CHGStateBackup = BMS_GlobalParam.Charge;
	}
	if (BMS_DSGStateBackup != BMS_GlobalParam.Discharge)
	{
		if (BMS_GlobalParam.Discharge == BMS_STATE_DISABLE)
		{
			BMS_HalCtrlDischarge(BMS_STATE_DISABLE);
		}
		else if (BMS_GlobalParam.SysMode == BMS_MODE_SLEEP)  // 睡眠模式下可开启放电
		{
			BMS_HalCtrlDischarge(BMS_STATE_ENABLE);
		}
		BMS_DSGStateBackup = BMS_GlobalParam.Discharge;
	}
}

/*筛选需要均衡的电池
本bq芯片相邻电芯不能同时均衡
*/
static void BMS_BalanceSelect(void)
{
	bool BalanceSelected = false; // 是否已经选出需要均衡的电池
	uint32_t CellNumber;
	float MinVoltage = BMS_MonitorData.CellData[0].CellVoltage; // 最低电压，初始值为第一个电池的电压
	for(uint8_t i = 1; i < BMS_GlobalParam.Cell_Real_Number + 1; i++)
	{
		if(BMS_MonitorData.CellData[BMS_GlobalParam.Cell_Real_Number - i].CellVoltage - MinVoltage >= BMS_BalanceData.BalanceDiffeVoltage)
		{
			CellNumber = BMS_MonitorData.CellData[BMS_GlobalParam.Cell_Real_Number - i].CellNumber; // 从电压最高的电池开始比较

			if(CellNumber == 0 && ((BMS_BalanceData.BalanceRecord & (0x02)) == 0)) // 第一节需要均衡,且第二节没有被选中均衡
			{
				BalanceSelected = true;	
			}
			
			else if(CellNumber == BMS_GlobalParam.Cell_Real_Number - 1 
				&& ((BMS_BalanceData.BalanceRecord & (1 << (CellNumber - 1)))) == 0) // 最后一节需要均衡,且倒数第二节没有被选中均衡
			{
				BalanceSelected = true;
			}
			else // 中间的电池需要均衡
			{
				if((BMS_BalanceData.BalanceRecord & (1 << (CellNumber - 1))) == 0 // 前一节没有被选中均衡
					&& ((BMS_BalanceData.BalanceRecord & (1 << (CellNumber + 1)))) == 0) // 后一节没有被选中均衡
				{
					BalanceSelected = true;
				}
			}
		}
		else
		{
			break; // 电压差不满足均衡条件了,后面的电池就更不满足了,直接跳出循环
		}

		if(BalanceSelected == true)
		{
			BalanceSelected = false; // 已经选出需要均衡的电池了
			BMS_BalanceData.BalanceRecord |= 1 << CellNumber; // 将需要均衡的电池对应位置1 
			LOG_I("Balance Cell %d", CellNumber + 1);
		}
	}
}

/*设置均衡时间定时器*/
static void BMS_BalanceTimerStart(uint32_t Time)	
{
	uint32_t ticks = rt_tick_from_millisecond(Time * 1000);
	rt_timer_control(BalanceTime, RT_TIMER_CTRL_SET_TIME, &ticks); // 设置均衡时间定时器
	rt_timer_start(BalanceTime);
	LOG_I("Balance Timer Start............");

}

/*根据均衡标志位 写入bq寄存器*/
static void BMS_BalanceControl(void)
{
	if(BMS_BalanceData.BalanceRecord != BMS_CELL_NULL)
	{
		BMS_HalCtrlCellsBalance(BMS_BalanceData.BalanceRecord, BMS_STATE_ENABLE); // 使能均衡
		BalanceFlag = true;
		LOG_I("Balance Start............");

		BMS_BalanceTimerStart(BMS_BalanceData.BalanceCycleTime); // 启动均衡时间定时器
	}
}

static void BMS_BalanceEnter()
{
	if(BMS_BalanceCheck() == true) // 检查均衡条件是否满足
	{
		BMS_BalanceSelect(); // 筛选需要均衡的电池
		BMS_BalanceControl(); // 根据均衡标志位 写入bq寄存器
	}
}

static void BMS_BalanceTask(void *param)
{
	BMS_CHGStateBackup = BMS_GlobalParam.Charge; // 充电状态备份,用于快速充放电命令的执行
	BMS_DSGStateBackup = BMS_GlobalParam.Discharge;

	rt_thread_mdelay(500); // 等待系统稳定后再执行均衡控制，避免上电瞬间不满足均衡条件导致的误操作
	while(1)
	{
		BMS_BalanceEnter(); // 均衡控制

		BMS_BalanceChgDsgManage(); // 充放电管理

		rt_thread_delay(BALANCE_TASK_PERIOD); // 延时，控制任务周期
	}
}
