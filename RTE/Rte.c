#include "Rte.h"
#include "CanIf.h"
#include "Com.h"

void Rte_Init(void)
{
	Com_Init();
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
