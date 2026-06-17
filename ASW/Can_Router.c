#include "Com_Cfg.h"
#include "Rte_App.h"
#include "Com.h"
#include "CanTp.h"
#include "Dcm.h"
#include "bms_analysis.h"
#include "bms_monitor.h"
#include "rtthread.h"

rt_sem_t uds_rx_sem = RT_NULL; // 诊断请求接收信号量

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

    /*
     * UDS 诊断 CAN ID 路由
     * 物理寻址 (0x7E0) / 功能寻址 (0x7DF) → CanTp 剥离 PCI → DCM 处理
     *
     * 关键：CAN 总线上的原始帧带有 CAN-TP PCI 字节 (ISO 15765-2)，
     * 必须经过 CanTp 剥离后才能传给 DCM。
     *
     * 例：总线收到 [02 10 01 AA AA AA AA AA]
     *     CanTp 剥离 PCI → payload = [10 01], len=2
     *     DCM 收到: SID=0x10, subFunc=0x01 ✓
     */
    if ((msg->hw.CanId == COM_CANID_DIAG_PHYS_REQ) ||
        (msg->hw.CanId == COM_CANID_DIAG_FUNC_REQ))
    {
        CanTp_StatusType tpStatus;
        //如果id为uds诊断，就进行数据tp解析，然后存在该文件结构体中，后用CanTp_GetRxMsg(&tpMsg)传出
        tpStatus = CanTp_RxIndication(msg->hw.CanId, msg->data, msg->dlc);
        if (tpStatus == CANTP_OK)
        {
            CanTp_MsgType tpMsg;
            if (CanTp_GetRxMsg(&tpMsg))//传出CanTp解析后的结构体数据
            {
                (void)Dcm_ProcessRequest(&tpMsg);
                rt_sem_release(uds_rx_sem); // 释放信号量，通知 DCM 处理新请求
            }
        }
        return;
    }

    switch (msg->hw.CanId)//非uds诊断的ecu间消息，根据ID进行处理
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
