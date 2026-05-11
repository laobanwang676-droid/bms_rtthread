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

#include "bq769.h"




#define DBG_TAG "hal"
#define DBG_LVL DBG_LOG
#include "rtdbg.h"


// BMS唤醒
void BMS_HalCtrlWakeup(void)
{
	BQ769X0_Wakeup();
}

// 控制BMS进入睡眠模式
void BMS_HalCtrlSleep(void)
{
	BQ769X0_EntryShip();
}

// 控制放电状态
void BMS_HalCtrlDischarge(BMS_StateTypedef NewState)
{
	BQ769X0_ControlDSGOrCHG(DSG_CONTROL, (BQ769X0_StateTypedef)NewState);
}

// 控制充电状态
void BMS_HalCtrlCharge(BMS_StateTypedef NewState)
{
	BQ769X0_ControlDSGOrCHG(CHG_CONTROL, (BQ769X0_StateTypedef)NewState);
}

// 控制单节或多节电芯均衡,最多可以支持32节
void BMS_HalCtrlCellsBalance(BMS_CellIndexTypedef CellIndex, BMS_StateTypedef NewState)
{
	BQ769X0_CellBalanceControl((BQ769X0_CellIndexTypedef)CellIndex, (BQ769X0_StateTypedef)NewState);
}


