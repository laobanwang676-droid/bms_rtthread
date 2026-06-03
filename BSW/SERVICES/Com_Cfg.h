#ifndef COM_CFG_H
#define COM_CFG_H

#include "CanIf.h"

typedef struct
{
    float voltage_v;
    float soc_pct;
} Com_BmsStatusInput;

/* CAN IDs (standard 11-bit, from DBC) */
#define COM_CANID_BMS_STATUS          ((Can_IdType)0x3F1u)
#define COM_CANID_CELL_VOLT_REQ       ((Can_IdType)0x123u)
#define COM_CANID_CELL_VOLT_RESP      ((Can_IdType)0x321u)
#define COM_CANID_TEMP_REQ            ((Can_IdType)0x234u)
#define COM_CANID_TEMP_RESP           ((Can_IdType)0x432u)

/* PDU handles (software IDs) */
#define COM_TXPDU_BMS_STATUS          ((PduIdType)0u)

/* Signal layout for COM_CANID_BMS_STATUS */
#define COM_BMS_STATUS_DLC            ((uint8_t)4u)
#define COM_BMS_STATUS_PERIOD_MS      ((uint32_t)1000u)

/* Signal resolution (example: adjust to your DBC) */
#define COM_BMS_VOLTAGE_RESOLUTION_V  (0.1f)
#define COM_BMS_SOC_RESOLUTION_PCT    (0.1f)

uint8_t Com_GetBmsStatusInput(Com_BmsStatusInput* input);
void Com_RxIndicationHook(const CanIf_RxMsgType* msg);

#endif /* COM_CFG_H */
