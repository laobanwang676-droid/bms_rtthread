#include "Rte.h"
#include "CanIf.h"
#include "Com.h"
#include "CanTp.h"
#include "Dcm.h"
#include "Com_Cfg.h"
#include <rtthread.h>

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

Can_ReturnType Rte_MainFunction_ComTx(void)
{
	return Com_MainFunctionTx();
}

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
	CanTp_MsgType  respMsg;
	Can_PduType  pdu;
	uint32_t     txCanId;
	uint8_t      txData[8];
	uint8_t      sduBuffer[8];   /* Can_PduType.sdu 是指针，需要指向有效内存 */
	uint8_t      txDlc;
	uint8_t      i;

	Dcm_MainFunction();

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
