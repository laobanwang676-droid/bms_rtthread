#ifndef COM_CFG_H
#define COM_CFG_H

#include "can.h"

/* CAN IDs (standard 11-bit, from DBC) */
#define COM_CANID_BMS_STATUS          ((Can_IdType)0x3F1u)

/* PDU handles (software IDs) */
#define COM_TXPDU_BMS_STATUS          ((PduIdType)0u)

/* Signal layout for COM_CANID_BMS_STATUS */
#define COM_BMS_STATUS_DLC            ((uint8_t)3u)
#define COM_BMS_STATUS_PERIOD_MS      ((uint32_t)100u)

/* Signal resolution (example: adjust to your DBC) */
#define COM_BMS_VOLTAGE_RESOLUTION_V  (0.1f)
#define COM_BMS_SOC_RESOLUTION_PCT    (0.1f)

#endif /* COM_CFG_H */
