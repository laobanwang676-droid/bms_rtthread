#include "Com_Cfg.h"
#include "Rte_App.h"
#include "Com.h"
#include "bms_analysis.h"
#include "bms_monitor.h"

/*
 * 将浮点数按 10 倍比例转换为有符号整数，并进行四舍五入。
 * 例如：3.26 -> 33，-3.26 -> -33。
 * 用于 CAN 报文中以 0.1 为精度传输电压、温度等物理量。
 */ 
static int16_t Com_ScaleToInt10(float value)
{
    if (value >= 0.0f)
    {
        return (int16_t)(value * 10.0f + 0.5f);
    }

    return (int16_t)(value * 10.0f - 0.5f);
}

static uint8_t Com_SendCellVoltageResponse(void)
{
    uint8_t count = BMS_GlobalParam.Cell_Real_Number;
    uint8_t data[8] = {0u};
    Can_PduType pdu;

    if (count == 0u || count > BMS_CELL_MAX)
    {
        count = BMS_CELL_MAX;
    }

    if (count > sizeof(data))
    {
        count = sizeof(data);
    }

    for (uint8_t i = 0u; i < count; i++)
    {
        int16_t voltage_x10;

        voltage_x10 = Com_ScaleToInt10(BMS_MonitorData.CellData[i].CellVoltage);

        if (voltage_x10 < 0)
        {
            data[i] = 0u;
        }
        else if (voltage_x10 > 255)
        {
            data[i] = 255u;
        }
        else
        {
            data[i] = (uint8_t)voltage_x10;
        }
    }

    pdu.id = COM_CANID_CELL_VOLT_RESP;
    pdu.length = count;
    pdu.sdu = data;
    pdu.swPduHandle = COM_CANID_CELL_VOLT_RESP;

    if (Rte_Call_CanIf_Transmit(COM_CANID_CELL_VOLT_RESP, &pdu) != CAN_OK)
    {
        return 0u;
    }

    return 1u;
}

static uint8_t Com_SendTemperatureResponse(void)
{
    uint8_t count = (uint8_t)BMS_MonitorData.CellTempEffectiveNumber;

    if (count == 0u || count > BMS_TEMP_MAX)
    {
        count = BMS_GlobalParam.Temp_Real_Number;
    }

    if (count == 0u || count > BMS_TEMP_MAX)
    {
        count = BMS_TEMP_MAX;
    }

    for (uint8_t i = 0u; i < count; i++)
    {
        uint8_t data[8] = {0u};
        Can_PduType pdu;
        int16_t temp_x10;

        temp_x10 = Com_ScaleToInt10(BMS_MonitorData.CellTemp[i]);

        data[0] = (uint8_t)(i + 1u);
        data[1] = (uint8_t)(temp_x10 & 0xFF);
        data[2] = (uint8_t)((temp_x10 >> 8) & 0xFF);

        pdu.id = COM_CANID_TEMP_RESP;
        pdu.length = 3u;
        pdu.sdu = data;
        pdu.swPduHandle = COM_CANID_TEMP_RESP;

        if (Rte_Call_CanIf_Transmit(COM_CANID_TEMP_RESP, &pdu) != CAN_OK)
        {
            return 0u;
        }
    }

    return 1u;
}

// 获取BMS状态输入
uint8_t Com_GetBmsStatusInput(Com_BmsStatusInput* input)
{
    if (input == 0)
    {
        return 0u;
    }

    if (BMS_MonitorData.complete == false || BMS_AnalysisData.complete == false)
    {
        return 0u;
    }

    input->voltage_v = BMS_MonitorData.BatteryVoltage;
    input->soc_pct = BMS_AnalysisData.SOC * 100.0f;
    return 1u;
}

//处理外部CAN消息。外部必须是数据帧，只解析ID
void Com_RxIndicationHook(const CanIf_RxMsgType* msg)
{
    if (msg == 0)
    {
        return;
    }

    switch (msg->hw.CanId)//TODO:后续处理
    {
        case COM_CANID_CELL_VOLT_REQ:
            (void)Com_SendCellVoltageResponse();
            break;
        case COM_CANID_TEMP_REQ:
            (void)Com_SendTemperatureResponse();
            break;
        default:
            break;
    }
}
