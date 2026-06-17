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

#endif /* RTE_H */
