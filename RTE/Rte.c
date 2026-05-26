#include "Rte.h"
#include "CanIf.h"
#include "Com.h"

CanIf_RxMsgType CanIf_MessagePending = {0};

void Rte_Init(void)
{
	Com_Init();
}

void Rte_Start(void)
{

}

void Rte_MainFunction(void)
{
	
}

Can_ReturnType Rte_Call_CanIf_Transmit(PduIdType TxPduId, const Can_PduType* PduInfo)
{
	return CanIf_Transmit(TxPduId, PduInfo);
}
