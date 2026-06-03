#include "Com.h"
#include "Com_Cfg.h"

static uint8_t Com_BuildBmsStatus(uint8_t* data, uint8_t* dlc)
{
    uint16_t voltage_raw = 0u;
    uint16_t soc_raw = 0u;
    Com_BmsStatusInput input;

    if (Com_GetBmsStatusInput(&input) == 0u)
    {
        return 0u;
    }

    if (input.voltage_v < 0.0f)
    {
        input.voltage_v = 0.0f;
    }
    if (input.soc_pct < 0.0f)
    {
        input.soc_pct = 0.0f;
    }
    if (input.soc_pct > 100.0f)
    {
        input.soc_pct = 100.0f;
    }

    voltage_raw = (uint16_t)(input.voltage_v / COM_BMS_VOLTAGE_RESOLUTION_V);
    soc_raw = (uint16_t)(input.soc_pct / COM_BMS_SOC_RESOLUTION_PCT);

    data[0] = (uint8_t)(voltage_raw & 0xFFu);
    data[1] = (uint8_t)((voltage_raw >> 8) & 0xFFu);
    data[2] = (uint8_t)(soc_raw & 0xFFu);
    data[3] = (uint8_t)((soc_raw >> 8) & 0xFFu);

    *dlc = COM_BMS_STATUS_DLC;
    return 1u;
}

void Com_Init(void)
{
    /* Com 调度由 OS/RTE 驱动，避免与特定 OS 绑定 */
}

Can_ReturnType Com_MainFunctionTx(void)
{
    uint8_t data[8] = {0u};
    uint8_t dlc = 0u;
    Can_PduType pdu;

    if (Com_BuildBmsStatus(data, &dlc) == 0u)
    {
        return CAN_NOT_OK;
    }

    pdu.id = COM_CANID_BMS_STATUS;
    pdu.length = dlc;
    pdu.sdu = data;
    pdu.swPduHandle = COM_TXPDU_BMS_STATUS;

    return CanIf_Transmit(COM_TXPDU_BMS_STATUS, &pdu);
}

void Com_RxIndication(const CanIf_RxMsgType* msg)
{
    if (msg == RT_NULL)
    {
        return;
    }
    Com_RxIndicationHook(msg);
}
