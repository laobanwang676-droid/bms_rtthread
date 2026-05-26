#include "Com.h"
#include "Com_Cfg.h"
#include "bms_analysis.h"
#include "bms_monitor.h"
#include "Rte.h"
#include <rtthread.h>

static rt_timer_t s_com_tx_timer = RT_NULL;

static void Com_BuildBmsStatus(uint8_t* data, uint8_t* dlc)
{
    uint16_t voltage_raw = 0u;
    uint16_t soc_raw = 0u;
    float voltage_v = BMS_MonitorData.BatteryVoltage;
    float soc_pct = BMS_AnalysisData.SOC * 100.0f;

    if (voltage_v < 0.0f)
    {
        voltage_v = 0.0f;
    }
    if (soc_pct < 0.0f)
    {
        soc_pct = 0.0f;
    }
    if (soc_pct > 100.0f)
    {
        soc_pct = 100.0f;
    }

    voltage_raw = (uint16_t)(voltage_v / COM_BMS_VOLTAGE_RESOLUTION_V);
    soc_raw = (uint16_t)(soc_pct / COM_BMS_SOC_RESOLUTION_PCT);

    data[0] = (uint8_t)(voltage_raw & 0xFFu);
    data[1] = (uint8_t)((voltage_raw >> 8) & 0xFFu);
    data[2] = (uint8_t)(soc_raw & 0xFFu);

    *dlc = COM_BMS_STATUS_DLC;
}

static void Com_TxTimerCallback(void* parameter)
{
    (void)parameter;
    if(BMS_MonitorData.complete == false || BMS_AnalysisData.complete == false)
    {
        return; // 数据未准备好，跳过本次发送
    }
    Com_MainFunctionTx();
}

void Com_Init(void)
{
    if (s_com_tx_timer != RT_NULL)
    {
        return;
    }

    s_com_tx_timer = rt_timer_create("com_tx", Com_TxTimerCallback, RT_NULL,
        rt_tick_from_millisecond(COM_BMS_STATUS_PERIOD_MS), RT_TIMER_FLAG_PERIODIC);
    if (s_com_tx_timer != RT_NULL)
    {
        (void)rt_timer_start(s_com_tx_timer);
    }
}

void Com_MainFunctionTx(void)
{
    uint8_t data[8] = {0u};
    uint8_t dlc = 0u;
    Can_PduType pdu;

    Com_BuildBmsStatus(data, &dlc);

    pdu.id = COM_CANID_BMS_STATUS;
    pdu.length = dlc;
    pdu.sdu = data;
    pdu.swPduHandle = COM_TXPDU_BMS_STATUS;

    (void)Rte_Call_CanIf_Transmit(COM_TXPDU_BMS_STATUS, &pdu);
}

void parse_can_message(const CanIf_RxMsgType* msg)
{
    switch (msg->hw.CanId)
    {
        case COM_CANID_BMS_STATUS:
            // TODO: 暂时预留接收处理，目前只做上报
            break;
        default:
            // 处理其他消息
            break;
    }
}

void Com_RxIndication(const CanIf_RxMsgType* msg)
{
    if (msg == RT_NULL)
    {
        return;
    }
    parse_can_message(msg);
}
