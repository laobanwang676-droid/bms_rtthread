#include "Rte.h"
#include "CanIf.h"
#include "Com.h"
#include "CanTp.h"
#include "Dcm.h"
#include "Com_Cfg.h"
#include <rtthread.h>

/*
 * AUTOSAR 分层说明:
 *   RTE 层引入 BMS 应用层头文件，负责将 ASW 数据同步到内部缓冲区。
 *   DCM (BSW 层) 仅通过 Rte_Read_* 接口访问数据，不直接引用 ASW 头文件。
 */
#include "bms_monitor.h"        /* BMS_MonitorData: 电压/电流/温度原始采集 */
#include "bms_analysis.h"       /* BMS_AnalysisData: SOC/SOH 分析数据 */
#include "bms_protect.h"        /* BMS_Protect: 保护报警标志 */
#include "bms_global_define.h"  /* BMS_GlobalParam: 系统模式/使能状态 */

/*===========================================================================
 * AUTOSAR RTE 诊断数据接口
 *
 * 数据流向: BMS 全局变量 (ASW) → Rte_Read_*() → DCM (BSW)
 * RTE 作为透明桥梁，直接读取 ASW 全局结构体，无额外数据拷贝。
 *===========================================================================*/

void Rte_Init(void)
{
	if(uds_rx_sem == RT_NULL)
	{
		uds_rx_sem = rt_sem_create("uds_rx", 0, RT_IPC_FLAG_FIFO);
	}

	Com_Init();
	CanTp_Init();
	Dcm_Init();
}

void Rte_Start(void)
{

}

void Rte_MainFunction_ComRx(void)
{
	CanIf_RxMsgType msg;

	CanIf_MainFunction_Read();

	while (CanIf_ReadRx(&msg, 0) == RT_EOK)
	{
		Com_RxIndication(&msg);
	}
}

/*
 * BMS 状态周期广播: SOC、总电压 → CAN ID 0x3F1
 */
Can_ReturnType Rte_MainFunction_ComTx(void)
{
	return Com_MainFunctionTx();
}

/*
can发送函数
*/
Can_ReturnType Rte_Call_CanIf_Transmit(PduIdType TxPduId, const Can_PduType* PduInfo)
{
	return CanIf_Transmit(TxPduId, PduInfo);
}

/*
 * UDS 诊断主任务：
 *   1. Dcm_MainFunction() — 会话超时管理
 *   2. DCM 响应 → CanTp (添加 PCI 头) → CanIf 发送
 *   3. CanTp_MainFunction() — 网络层超时管理
 */
void Rte_MainFunction_Dcm(void)
{
	static uint32_t lastTick = 0;
	uint32_t dt = 0;
	CanTp_MsgType  respMsg;
	Can_PduType  pdu;
	uint32_t     txCanId;
	uint8_t      txData[8];
	uint8_t      sduBuffer[8];   /* Can_PduType.sdu 是指针，需要指向有效内存 */
	uint8_t      txDlc;
	uint8_t      i;

	uint32_t now = (uint32_t)rt_tick_get();
	uint32_t elapsed_ticks = now - lastTick;
	lastTick = now;
	/* tick → ms: elapsed_ms = elapsed_ticks * 1000 / RT_TICK_PER_SECOND */
	dt = elapsed_ticks * (1000u / RT_TICK_PER_SECOND);

	Dcm_MainFunction(dt);

	/* 步骤1: DCM 响应 → CanTp (添加 PCI 头) */
	if (Dcm_GetResponse(&respMsg)) //将dcm文件中的中间存储结构体拷贝到&respMsg，并且标志位置为false说明数据已经处理
	{
		(void)CanTp_Transmit(respMsg.canId, respMsg.data, respMsg.length);
		Dcm_TxConfirmation();
	}

	/* 步骤2: CanTp 输出帧 → CanIf 物理发送 */
	if (CanTp_GetTxFrame(&txCanId, txData, &txDlc))
	{
		pdu.id          = txCanId;
		pdu.length      = txDlc;
		pdu.sdu         = sduBuffer;  /* 指针指向局部数组 */
		pdu.swPduHandle = (PduIdType)txCanId;
		for (i = 0u; i < txDlc && i < 8u; i++)
		{
			pdu.sdu[i] = txData[i];
		}

		(void)CanIf_Transmit((PduIdType)txCanId, &pdu);
		CanTp_TxConfirmation();
	}

	CanTp_MainFunction();
}

/*===========================================================================
 * RTE 诊断数据接口实现
 *
 * AUTOSAR 架构:
 *   ASW (BMS-App) 拥有全局数据结构，RTE 只做接口转发，不复制数据。
 *   DCM 通过 Rte_Read_* 接口读取数据，不直接引用 ASW 头文件。
 *
 * 信号编码规则 (与 CAN 信号 DBC 保持一致):
 *   - 电压类:  分辨率 0.1V
 *   - 电流类:  分辨率 0.1A
 *   - SOC/SOH: 分辨率 0.5%
 *   - 单体电压: 分辨率 0.001V
 *   - 温度类:  分辨率 1°C，偏移 40°C (raw = T + 40)
 *===========================================================================*/

/* --- Rte_Read 端口实现: DCM 调用以下接口读取 DID 对应数据 --- */

Rte_ReturnType Rte_Read_Dcm_BatteryVoltage(float* value)
{
    *value = BMS_MonitorData.BatteryVoltage;
    return RTE_E_OK;
}

Rte_ReturnType Rte_Read_Dcm_BatteryCurrent(float* value)
{
    *value = BMS_MonitorData.BatteryCurrent;
    return RTE_E_OK;
}

Rte_ReturnType Rte_Read_Dcm_SOC(float* value)
{
    *value = BMS_AnalysisData.SOC;
    return RTE_E_OK;
}

Rte_ReturnType Rte_Read_Dcm_CellVoltMax(float* value)
{
    *value = BMS_AnalysisData.CellVoltMax;
    return RTE_E_OK;
}

Rte_ReturnType Rte_Read_Dcm_CellVoltMin(float* value)
{
    *value = BMS_AnalysisData.CellVoltMin;
    return RTE_E_OK;
}

Rte_ReturnType Rte_Read_Dcm_CellTempMax(float* value)
{
    uint8_t cnt = BMS_GlobalParam.Temp_Real_Number;
    if (cnt == 0u || cnt > BMS_TEMP_MAX) { cnt = 1u; }
    *value = BMS_MonitorData.CellTemp[cnt - 1u];  /* 已排序，最后元素最大 */
    return RTE_E_OK;
}

Rte_ReturnType Rte_Read_Dcm_CellTempMin(float* value)
{
    *value = BMS_MonitorData.CellTemp[0];  /* 已排序，第一个元素最小 */
    return RTE_E_OK;
}

Rte_ReturnType Rte_Read_Dcm_SysMode(uint8_t* mode)
{
    *mode = (uint8_t)BMS_GlobalParam.SysMode;
    return RTE_E_OK;
}

Rte_ReturnType Rte_Read_Dcm_ChargeEnabled(bool* enabled)
{
    *enabled = (BMS_GlobalParam.Charge == BMS_STATE_ENABLE);
    return RTE_E_OK;
}

Rte_ReturnType Rte_Read_Dcm_DischargeEnabled(bool* enabled)
{
    *enabled = (BMS_GlobalParam.Discharge == BMS_STATE_ENABLE);
    return RTE_E_OK;
}

Rte_ReturnType Rte_Read_Dcm_BalanceActive(bool* active)
{
    *active = (BMS_GlobalParam.Balance == BMS_STATE_ENABLE);
    return RTE_E_OK;
}

Rte_ReturnType Rte_Read_Dcm_ProtectAlertFlags(uint16_t* flags)
{
    *flags = (uint16_t)BMS_Protect.alert;
    return RTE_E_OK;
}
