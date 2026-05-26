#include <stdio.h>
#include "EcuM.h"
#include "can.h"
#include "CanIf.h"
#include "Rte.h"

static uint8_t s_ecum_inited = 0u;

void EcuM_Init(void)
{
	if (s_ecum_inited != 0u)
	{
		return;
	}

	Can_Init(NULL);//TODO:如果需要接收需要更改滤波器配置
	CanIf_Init();
	Rte_Init();

	s_ecum_inited = 1u;
}

void EcuM_StartupTwo(void)
{
	if (s_ecum_inited == 0u)
	{
		EcuM_Init();
	}

	(void)Can_SetControllerMode(0u, CAN_T_START);
	Rte_Start();
}
