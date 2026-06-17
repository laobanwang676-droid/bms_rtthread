/**
 * @file    CanTp.c
 * @brief   CAN-TP 实现 — ISO 15765-2 单帧解析 + 多帧预留
 * 
 * 当前实现：
 * SF (Single Frame): 剥离 PCI 字节，提取纯 UDS payload
 * FF/CF/FC: 后续实现（刷写大包时需要）
 * [CAN-TP 面单 (PCI) 格式速查手册]
 * 所有报文的第一个字节 (Byte 0) 永远是 PCI 面单。它被劈成两半使用：
 * 高4位代表【我是谁 (帧类型)】，低4位代表【我的附加信息 (长度/序号/状态)】。
 * * 1. 单帧 (SF - Single Frame)
 * - Byte 0: [ 高4位: 0 ] [ 低4位: Payload 真实长度 (1~7字节) ]
 * - 提数据: 真实数据从 Byte 1 开始提取。
 * * 2. 首帧 (FF - First Frame)
 * - Byte 0: [ 高4位: 1 ] [ 低4位: 总长度的高 4 位 ]
 * - Byte 1: [ 总长度的低 8 位 ] (与 Byte 0 低4位拼起来就是完整总长度)
 * - 提数据: 真实数据从 Byte 2 开始，本帧只带头 6 个字节的数据。
 * * 3. 连续帧 (CF - Consecutive Frame)
 * - Byte 0: [ 高4位: 2 ] [ 低4位: SN 连续序号 (0~15 循环翻转) ]
 * - 提数据: 真实数据从 Byte 1 开始提取，最多 7 个字节。
 * * 4. 流控帧 (FC - Flow Control)
 * - Byte 0: [ 高4位: 3 ] [ 低4位: FS 状态 (0=继续发, 1=请稍等, 2=装不下了) ]
 * - Byte 1: [ BS 块大小 ] (允许对方连续发多少帧 CF，0代表不限制)
 * - Byte 2: [ STmin 间隔 ] (要求对方每发一帧 CF 必须等待的毫秒数)
 * * ============================================================================
 */

#include "CanTp.h"
#include <string.h>

/*===========================================================================
 * 内部状态变量
 *===========================================================================*/

/* 接收侧 */
static CanTp_RxStateType CanTp_RxState     = CANTP_STATE_IDLE;
static uint8_t           CanTp_RxBuffer[CANTP_MAX_RX_BUFFER];
static uint16_t          CanTp_RxTotalLen   = 0u;   /* FF 声明的总长度 */
static uint16_t          CanTp_RxOffset     = 0u;   /* 当前写入偏移 */
static uint8_t           CanTp_RxSnExpected = 0u;   /* 期望的下一个 CF 序号 */
static uint32_t          CanTp_RxCanId      = 0u;   /* 当前接收的 CAN ID */

/* 接收完成消息（纯 UDS payload） */
static CanTp_MsgType  CanTp_RxDoneMsg;
static bool           CanTp_RxPending       = false; //标志位 表示是否有新的消息待处理

/* 发送侧 */
static uint8_t   CanTp_TxData[CANTP_MAX_RX_BUFFER];
static uint16_t  CanTp_TxTotalLen  = 0u;
static uint16_t  CanTp_TxOffset    = 0u;
static uint32_t  CanTp_TxCanId     = 0u;
static bool      CanTp_TxActive    = false;

/* 待发送的单帧 CAN 帧 */
static uint32_t  CanTp_TxFrameCanId;
static uint8_t   CanTp_TxFrameData[8];
static uint8_t   CanTp_TxFrameDlc;
static bool      CanTp_TxFramePending = false;

/*===========================================================================
 * 公开 API
 *===========================================================================*/

void CanTp_Init(void)
{
    CanTp_RxState      = CANTP_STATE_IDLE;
    CanTp_RxTotalLen   = 0u;
    CanTp_RxOffset     = 0u;
    CanTp_RxSnExpected = 0u;
    CanTp_RxPending    = false;
    CanTp_TxActive     = false;
    CanTp_TxFramePending = false;
}

void CanTp_MainFunction(void)
{
    /* TODO: 超时检测
     * if (CanTp_RxState != CANTP_STATE_IDLE && timeout)
     * {
     *     CanTp_RxState = CANTP_STATE_IDLE;  // 超时回退
     * }
     */
}

/*===========================================================================
 * 接收侧
 *===========================================================================*/

CanTp_StatusType CanTp_RxIndication(uint32_t canId, const uint8_t* rawData, uint8_t dlc)
{
    uint8_t pciType;
    uint8_t pciByte;

    if (rawData == (const uint8_t*)0 || dlc == 0u)
    {
        return CANTP_E_PARAM;
    }

    pciByte = rawData[0];
    pciType = pciByte & CANTP_PCI_TYPE_MASK;

    switch (pciType)
    {
        /*=================================================================
         * 单帧 (Single Frame)
         * 格式: [PCI=0x0D] [payload...]
         * D = DL (0~7, 实际 payload 长度)
         * 从 rawData[1] 开始拷贝 D 字节
         *=================================================================*/
        case CANTP_PCI_SF:
        {
            uint8_t payloadLen = pciByte & CANTP_SF_DL_MASK;

            /* DL=0 在 ISO 15765 中未定义，视为错误 */
            if (payloadLen == 0u || payloadLen > 7u)
            {
                return CANTP_E_PARAM;
            }

            /* 校验 DLC 是否足够 */
            if ((uint8_t)(1u + payloadLen) > dlc)
            {
                return CANTP_E_PARAM;
            }

            CanTp_RxDoneMsg.canId  = canId;
            CanTp_RxDoneMsg.length = payloadLen;
            {
                uint8_t i;
                for (i = 0u; i < payloadLen; i++)
                {
                    CanTp_RxDoneMsg.data[i] = rawData[1u + i];
                }
            }
            CanTp_RxPending = true;

            /* SF 完成，状态保持 IDLE */
            CanTp_RxState = CANTP_STATE_IDLE;
            return CANTP_OK;
        }

        /*=================================================================
         * 首帧 (First Frame) — 预留
         * 格式: [PCI=0x1D] [DL_high] [payload_0..5]
         * 总长度 DL = ((PCI & 0x0F) << 8) | rawData[1]
         * payload 从 rawData[2] 开始，本帧含 6 字节
         *=================================================================*/
        case CANTP_PCI_FF:
        {
            /* TODO: 实现多帧接收
             * 1. 解析总长度 DL
             * 2. 分配/检查缓冲区
             * 3. 拷贝本帧 6 字节
             * 4. 回复 FC (CTS)
             * 5. 状态 → CANTP_STATE_RX_FF
             */
            (void)canId;
            CanTp_RxState = CANTP_STATE_IDLE;
            return CANTP_E_NOT_READY;
        }

        /*=================================================================
         * 连续帧 (Consecutive Frame) — 预留
         * 格式: [PCI=0x2S] [payload_0..6]
         * S = 序列号 0~15
         *=================================================================*/
        case CANTP_PCI_CF:
        {
            /* TODO: 
             * if (CanTp_RxState != CANTP_STATE_RX_FF && RxState != RX_CF)
             *     → 错误
             * 校验 SN == CanTp_RxSnExpected
             * 拷贝 rawData[1..] 到缓冲区
             * CanTp_RxOffset 到达 CanTp_RxTotalLen → 完成
             */
            CanTp_RxState = CANTP_STATE_IDLE;
            return CANTP_E_NOT_READY;
        }

        /*=================================================================
         * 流控帧 (Flow Control) — 预留
         * 格式: [PCI=0x3F] [FS] [BS] [STmin]
         *=================================================================*/
        case CANTP_PCI_FC:
        {
            /* TODO: 调整发送节奏 (BS, STmin) */
            return CANTP_OK;
        }

        default:
            return CANTP_E_PARAM;
    }
}

bool CanTp_GetRxMsg(CanTp_MsgType* msg)
{
    if (!CanTp_RxPending || msg == (CanTp_MsgType*)0)
    {
        return false;
    }

    *msg = CanTp_RxDoneMsg;
    CanTp_RxPending = false;
    return true;
}

/*===========================================================================
 * 发送侧
 *===========================================================================*/

CanTp_StatusType CanTp_Transmit(uint32_t canId, const uint8_t* data, uint8_t length)
{
    if (data == (const uint8_t*)0 || length == 0u)
    {
        return CANTP_E_PARAM;
    }

    /*=====================================================================
     * 单帧发送 (payload ≤ 7 bytes)
     * 构造: [PCI=0x0L] [payload...] [填充 AA]
     *=====================================================================*/
    if (length <= 7u)
    {
        uint8_t i;
        CanTp_TxFrameCanId = canId;
        CanTp_TxFrameData[0] = (uint8_t)(CANTP_PCI_SF | length);  /* PCI byte */
        for (i = 0u; i < length; i++)
        {
            CanTp_TxFrameData[1u + i] = data[i];
        }
        /* 剩余字节填充 0xAA (推荐值) */
        for (i = 1u + length; i < 8u; i++)
        {
            CanTp_TxFrameData[i] = 0xAAu;
        }
        CanTp_TxFrameDlc    = 8u;  /* CAN-TP 固定 8 字节 DLC */
        CanTp_TxFramePending = true;
        return CANTP_OK;
    }

    /*=====================================================================
     * 多帧发送 (payload > 7 bytes) — 预留
     * 1. 先发 FF: [0x1D][DL_high][payload_0..5]  (6 bytes payload)
     * 2. 等待 FC (CTS)
     * 3. 分包发 CF: [0x2S][payload_0..6]  (7 bytes each)
     *=====================================================================*/
    /* TODO: 实现多帧发送 */
    return CANTP_E_NOT_READY;
}

bool CanTp_GetTxFrame(uint32_t* canId, uint8_t* data, uint8_t* length)
{
    if (!CanTp_TxFramePending || canId == (uint32_t*)0 || data == (uint8_t*)0 || length == (uint8_t*)0)
    {
        return false;
    }

    *canId  = CanTp_TxFrameCanId;
    *length = CanTp_TxFrameDlc;
    {
        uint8_t i;
        for (i = 0u; i < 8u; i++)
        {
            data[i] = CanTp_TxFrameData[i];
        }
    }
    CanTp_TxFramePending = false;
    return true;
}

void CanTp_TxConfirmation(void)
{
    /* 预留：可用于多帧发送的流控 */
}
