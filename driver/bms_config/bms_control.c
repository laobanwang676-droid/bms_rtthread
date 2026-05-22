/*
 * Copyright (C) 2021-2099 PLKJ Development Team
 *
 * SPDX-License-Identifier: CC BY-NC 4.0
 *
 * http://creativecommons.org/licenses/by-nc/4.0/
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "bms_control.h"
#include <rtthread.h>
#include "bms_global_define.h"
#include "bms_monitor.h"
#include "bms_info.h"
#include "bq769.h"

/* 外部I2C互斥锁声明 */
extern rt_mutex_t i2c_mutex;

#define DBG_TAG "hal"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"


// BMS唤醒
void BMS_HalCtrlWakeup(void)
{
	/* 获取I2C互斥锁 */
	rt_mutex_take(i2c_mutex, RT_WAITING_FOREVER);
	
	BQ769X0_Wakeup();
	
	/* 释放I2C互斥锁 */
	rt_mutex_release(i2c_mutex);
}

// 控制BMS进入睡眠模式
void BMS_HalCtrlSleep(void)
{
	/* 获取I2C互斥锁 */
	rt_mutex_take(i2c_mutex, RT_WAITING_FOREVER);
	
	BQ769X0_EntryShip();
	
	/* 释放I2C互斥锁 */
	rt_mutex_release(i2c_mutex);
}

// 控制放电状态
void BMS_HalCtrlDischarge(BMS_StateTypedef NewState)
{
	/* 获取I2C互斥锁 */
	rt_mutex_take(i2c_mutex, RT_WAITING_FOREVER);
	
	BQ769X0_ControlDSGOrCHG(DSG_CONTROL, (BQ769X0_StateTypedef)NewState);
	
	/* 释放I2C互斥锁 */
	rt_mutex_release(i2c_mutex);
}

// 控制充电状态
void BMS_HalCtrlCharge(BMS_StateTypedef NewState)
{
	/* 获取I2C互斥锁 */
	rt_mutex_take(i2c_mutex, RT_WAITING_FOREVER);
	
	BQ769X0_ControlDSGOrCHG(CHG_CONTROL, (BQ769X0_StateTypedef)NewState);
	
	/* 释放I2C互斥锁 */
	rt_mutex_release(i2c_mutex);
}

// 控制单节或多节电芯均衡,最多可以支持32节
void BMS_HalCtrlCellsBalance(BMS_CellIndexTypedef CellIndex, BMS_StateTypedef NewState)
{
	/* 获取I2C互斥锁 */
	rt_mutex_take(i2c_mutex, RT_WAITING_FOREVER);
	
	BQ769X0_CellBalanceControl((BQ769X0_CellIndexTypedef)CellIndex, (BQ769X0_StateTypedef)NewState);
	
	/* 释放I2C互斥锁 */
	rt_mutex_release(i2c_mutex);
}


/****************************** 数据采集接口（HAL层，带I2C互斥锁保护） *********************************/

// 采集电芯电压
void BMS_HalUpdateCellVoltage(void)
{
	/* 获取I2C互斥锁 */
	rt_mutex_take(i2c_mutex, RT_WAITING_FOREVER);
	
	BQ769X0_UpdateCellVolt();     // 更新单体电芯电压
	BQ769X0_UpdateBatVolt();      // 更新电池组总电压
	
	/* 释放I2C互斥锁 */
	rt_mutex_release(i2c_mutex);
}

// 采集电芯温度
void BMS_HalUpdateCellTemperature(void)
{
	/* 获取I2C互斥锁 */
	rt_mutex_take(i2c_mutex, RT_WAITING_FOREVER);
	
	BQ769X0_UpdateTsTemp();
	
	/* 释放I2C互斥锁 */
	rt_mutex_release(i2c_mutex);
}

// 采集电池电流
void BMS_HalUpdateCellCurrent(void)
{
	/* 获取I2C互斥锁 */
	rt_mutex_take(i2c_mutex, RT_WAITING_FOREVER);
	
	BQ769X0_UpdateCurrent();
	
	/* 释放I2C互斥锁 */
	rt_mutex_release(i2c_mutex);
}

/******************************************************************************/


/***************************************** 控制类 *********************************/

/* 睡眠唤醒由命令控制不合理，应该由系统控制
static void BMS_CmdWakeup(void)
{
	BMS_HalCtrlWakeup();
}
MSH_CMD_EXPORT(BMS_CmdWakeup, wakeup);


static void BMS_CmdSleep(void)
{
	BMS_HalCtrlSleep();
}
MSH_CMD_EXPORT(BMS_CmdSleep, sleep);
*/


static void BMS_CmdOpenDSG(void)
{
	BMS_GlobalParam.Discharge = BMS_STATE_ENABLE;
}
MSH_CMD_EXPORT(BMS_CmdOpenDSG, Open DSG);



static void BMS_CmdCloseDSG(void)
{
	BMS_GlobalParam.Discharge = BMS_STATE_DISABLE;
}
MSH_CMD_EXPORT(BMS_CmdCloseDSG, Close DSG);



static void BMS_CmdOpenCHG(void)
{
	BMS_GlobalParam.Charge = BMS_STATE_ENABLE;
}
MSH_CMD_EXPORT(BMS_CmdOpenCHG, Open CHG);



static void BMS_CmdCloseCHG(void)
{
	BMS_GlobalParam.Charge = BMS_STATE_DISABLE;
}
MSH_CMD_EXPORT(BMS_CmdCloseCHG, Close CHG);





static void BMS_CmdOpenBalance(void)
{
	BMS_GlobalParam.Balance = BMS_STATE_ENABLE;
}
MSH_CMD_EXPORT(BMS_CmdOpenBalance, Open Balance);



static void BMS_CmdCloseBalance(void)
{
	BMS_GlobalParam.Balance = BMS_STATE_DISABLE;
}
MSH_CMD_EXPORT(BMS_CmdCloseBalance, Close Balance);


static void BMS_CmdLoadDetect(void)
{
	if (BQ769X0_LoadDetect() == true)
	{
		LOG_I("Load Detected");
	}
	else
	{		
		LOG_I("No Load Was Detected");
	}
}
MSH_CMD_EXPORT(BMS_CmdLoadDetect, Load Detect);





static void BMS_CmdOpenInfo(void)
{
	BMS_InfoControlPrintf(BMS_STATE_ENABLE);
}
MSH_CMD_EXPORT(BMS_CmdOpenInfo, Open Info Printf);


static void BMS_CmdCloseInfo(void)
{
	BMS_InfoControlPrintf(BMS_STATE_DISABLE);
}
MSH_CMD_EXPORT(BMS_CmdCloseInfo, Close Info Printf);
