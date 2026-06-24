#ifndef COM_CFG_H
#define COM_CFG_H

#include "CanIf.h"

typedef struct
{
    float voltage_v;
    float soc_pct;
} Com_BmsStatusInput;

extern rt_sem_t uds_rx_sem; // 声明接收信号量

/* CAN IDs (standard 11-bit, from DBC) */
#define COM_CANID_BMS_STATUS          ((Can_IdType)0x3F1u)

/* TODO: 以下 CAN ID 为其他 ECU 主动索取单体电压/温度的请求-响应通道，
 *       暂时注释，需要时取消注释 */
/* #define COM_CANID_CELL_VOLT_REQ       ((Can_IdType)0x123u) */
/* #define COM_CANID_CELL_VOLT_RESP      ((Can_IdType)0x321u) */
/* #define COM_CANID_TEMP_REQ            ((Can_IdType)0x234u) */
/* #define COM_CANID_TEMP_RESP           ((Can_IdType)0x432u) */

/* UDS 诊断 CAN IDs — 路由到 DCM 模块 */
#define COM_CANID_DIAG_PHYS_REQ       ((Can_IdType)0x7E0u)  /* 物理寻址请求 */
#define COM_CANID_DIAG_PHYS_RESP      ((Can_IdType)0x7E8u)  /* 物理寻址响应 */
#define COM_CANID_DIAG_FUNC_REQ       ((Can_IdType)0x7DFu)  /* 功能寻址请求 */

/* PDU handles (software IDs) */
#define COM_TXPDU_BMS_STATUS          ((PduIdType)0u)

/* Signal layout for COM_CANID_BMS_STATUS */
#define COM_BMS_STATUS_DLC            ((uint8_t)8u)
#define COM_BMS_STATUS_PERIOD_MS      ((uint32_t)1000u)

/* Signal resolution (adjust to your DBC) */
#define COM_BMS_VOLTAGE_RESOLUTION_V  (0.1f)
#define COM_BMS_SOC_RESOLUTION_PCT    (0.1f)

uint8_t Com_GetBmsStatusInput(Com_BmsStatusInput* input);
void Com_RxIndicationHook(const CanIf_RxMsgType* msg);

#endif /* COM_CFG_H */
