#ifndef RTE_H
#define RTE_H

#include "Rte_Type.h"
#include "Rte_App.h"

void Rte_Init(void);
void Rte_Start(void);
void Rte_MainFunction_ComRx(void);
Can_ReturnType Rte_MainFunction_ComTx(void);

/* UDS 诊断周期性任务 */
void Rte_MainFunction_Dcm(void);

/*===========================================================================
 * RTE 诊断数据接口 (ASW ↔ DCM 数据桥梁)
 *
 * AUTOSAR 架构职责划分:
 *   ASW (BMS-App) 拥有全局数据结构 (BMS_MonitorData, BMS_AnalysisData 等)
 *   RTE 提供 Rte_Read 接口，DCM 通过接口读取数据，不直接访问 ASW 变量
 *===========================================================================*/

/* --- DCM 数据读取接口 (Rte_Read 端口) --- */
Rte_ReturnType Rte_Read_Dcm_BatteryVoltage(float* value);
Rte_ReturnType Rte_Read_Dcm_BatteryCurrent(float* value);
Rte_ReturnType Rte_Read_Dcm_SOC(float* value);
Rte_ReturnType Rte_Read_Dcm_CellVoltMax(float* value);
Rte_ReturnType Rte_Read_Dcm_CellVoltMin(float* value);
Rte_ReturnType Rte_Read_Dcm_CellTempMax(float* value);
Rte_ReturnType Rte_Read_Dcm_CellTempMin(float* value);
Rte_ReturnType Rte_Read_Dcm_SysMode(uint8_t* mode);
Rte_ReturnType Rte_Read_Dcm_ChargeEnabled(bool* enabled);
Rte_ReturnType Rte_Read_Dcm_DischargeEnabled(bool* enabled);
Rte_ReturnType Rte_Read_Dcm_BalanceActive(bool* active);
Rte_ReturnType Rte_Read_Dcm_ProtectAlertFlags(uint16_t* flags);

#endif /* RTE_H */
