#ifndef RTE_APP_H
#define RTE_APP_H

#include "can.h"

Can_ReturnType Rte_Call_CanIf_Transmit(PduIdType TxPduId, const Can_PduType* PduInfo);

#endif /* RTE_APP_H */
