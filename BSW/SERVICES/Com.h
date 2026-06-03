#ifndef COM_H
#define COM_H

#include "CanIf.h"

void Com_Init(void);
Can_ReturnType Com_MainFunctionTx(void);
void Com_RxIndication(const CanIf_RxMsgType* msg);

#endif /* COM_H */
