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
#ifndef BMS_CONTROL_H__
#define BMS_CONTROL_H__



#include "bms_global_define.h"


void BMS_HalCtrlWakeup(void);
void BMS_HalCtrlSleep(void);
void BMS_HalCtrlDischarge(BMS_StateTypedef NewState);
void BMS_HalCtrlCharge(BMS_StateTypedef NewState);
void BMS_HalCtrlCellsBalance(BMS_CellIndexTypedef CellIndex, BMS_StateTypedef NewState);



#endif /* BMS_CONTROL_H__ */


