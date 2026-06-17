/**
 * @file    CanTp.h
 * @brief   CAN Transport Protocol — ISO 15765-2 网络层
 * 
 * 模块职责：
 *   - 剥离 CAN-TP 协议控制信息 (PCI) 字节
 *   - 单帧 (SF): 提取 payload 长度并去掉 PCI 头
 *   - 首帧 (FF): 记录总长度，回复流控帧 (预留)
 *   - 连续帧 (CF): 按序号拼接数据 (预留)
 *   - 流控帧 (FC): 控制发送节奏 (预留)
 * 
 * 所属 AUTOSAR 层: BSW / SERVICES
 * 
 * 数据流向:
 *   CanIf (8 bytes raw) → CanTp (剥离 PCI) → DCM (纯 UDS payload)
 *   DCM (UDS response) → CanTp (添加 PCI) → CanIf (8 bytes raw x N帧)
 */

#ifndef CANTP_H
#define CANTP_H

#include <stdint.h>
#include <stdbool.h>

/*===========================================================================
 * CAN-TP PCI 类型定义 (ISO 15765-2)
 *===========================================================================*/
#define CANTP_PCI_SF              (0x00u)  /* 单帧 (Single Frame), byte0高4bit=0 */
#define CANTP_PCI_FF              (0x10u)  /* 首帧 (First Frame),  byte0高4bit=1 */
#define CANTP_PCI_CF              (0x20u)  /* 连续帧 (Consecutive), byte0高4bit=2 */
#define CANTP_PCI_FC              (0x30u)  /* 流控帧 (Flow Control), byte0高4bit=3 */

#define CANTP_PCI_TYPE_MASK       (0xF0u)  /* 高4位掩码 */
#define CANTP_SF_DL_MASK          (0x0Fu)  /* SF 低4位 = 数据长度 */
#define CANTP_CF_SN_MASK          (0x0Fu)  /* CF 低4位 = 序列号 */
#define CANTP_FF_DL_MASK          (0x0FFFu)/* FF 低12位 = 数据总长度 */

/* 流控帧标志 */
#define CANTP_FC_CTS              (0x00u)  /* Continue To Send */
#define CANTP_FC_WAIT             (0x01u)  /* Wait */
#define CANTP_FC_OVFLW            (0x02u)  /* Overflow */

/* 超时与大小 */
#define CANTP_RX_TIMEOUT_MS       (1000u)  /* 接收超时 */
#define CANTP_TX_TIMEOUT_MS       (1000u)  /* 发送超时 */
#define CANTP_MAX_RX_BUFFER       (4096u)  /* 最大接收缓存 (支持大包) */
#define CANTP_ST_MIN_MS           (0u)     /* 最小帧间隔 */

/*===========================================================================
 * 数据类型
 *===========================================================================*/

/** @brief CAN-TP 接收状态 */
typedef enum
{
    CANTP_STATE_IDLE      = 0x00u,  /* 空闲，等待新消息 */
    CANTP_STATE_RX_FF     = 0x01u,  /* 收到首帧，等待连续帧 */
    CANTP_STATE_RX_CF     = 0x02u,  /* 正在接收连续帧 */
    CANTP_STATE_COMPLETE  = 0x03u   /* 接收完成 */
} CanTp_RxStateType;

/** @brief CAN-TP 处理结果 */
typedef enum
{
    CANTP_OK              = 0x00u,
    CANTP_E_PARAM         = 0x01u,
    CANTP_E_TIMEOUT       = 0x02u,
    CANTP_E_SN_ERROR      = 0x03u,  /* 序列号错误 */
    CANTP_E_BUFFER_OVFL   = 0x04u,  /* 缓冲区溢出 */
    CANTP_E_NOT_READY     = 0x05u   /* 未准备好 */
} CanTp_StatusType;

/** @brief CAN-TP 消息 (纯 UDS payload，无 PCI 字节) */
typedef struct
{
    uint32_t canId;       /* 原始 CAN ID (用于路由) */
    uint8_t  data[8];     /* 纯 UDS payload (单帧最多7字节，多帧通过缓冲) */
    uint8_t  length;      /* payload 有效长度 */
} CanTp_MsgType;

/*===========================================================================
 * API 函数声明
 *===========================================================================*/

/**
 * @brief  初始化 CAN-TP 模块。
 */
void CanTp_Init(void);

/**
 * @brief  CAN-TP 主函数，处理超时等。
 */
void CanTp_MainFunction(void);

/**
 * @brief  喂入一帧原始 CAN 数据（来自 CanIf）。
 *         CAN-TP 内部根据 PCI 类型决定：
 *         - SF: 直接剥离 PCI，立即产生完整 UDS 消息
 *         - FF: 记录总长，回复 FC，进入接收状态
 *         - CF: 拼接到缓冲区
 *         - FC: 控制发送流
 * 
 * @param  canId:    CAN 帧 ID
 * @param  rawData:  原始 8 字节 CAN 数据
 * @param  dlc:     CAN 帧 DLC
 * @retval CANTP_OK         成功（SF 消息已就绪，用 CanTp_GetRxMsg 取出）
 * @retval CANTP_E_NOT_READY 正在接收多帧，尚未完成
 * @retval 其它错误码。
 */
CanTp_StatusType CanTp_RxIndication(uint32_t canId, const uint8_t* rawData, uint8_t dlc);

/**
 * @brief  取出已接收完成的纯 UDS 消息（供 DCM 使用）。
 * @param  msg: 输出消息指针。
 * @retval true:  有新消息。
 * @retval false: 无新消息。
 */
bool CanTp_GetRxMsg(CanTp_MsgType* msg);

/**
 * @brief  发送 UDS 响应（自动添加 PCI 头，单帧直接发，多帧自动分包）。
 * @param  canId:  目标 CAN ID (通常为物理响应 ID)
 * @param  data:   纯 UDS payload
 * @param  length: payload 长度
 * @retval CANTP_OK 成功（或已加入发送队列）。
 */
CanTp_StatusType CanTp_Transmit(uint32_t canId, const uint8_t* data, uint8_t length);

/**
 * @brief  获取待发送的 CAN 帧（供 RTE 轮询发送）。
 * @param  canId:  输出 CAN ID
 * @param  data:   输出 8 字节数据
 * @param  length: 输出 DLC
 * @retval true:  有待发送帧。
 * @retval false: 无待发送帧。
 */
bool CanTp_GetTxFrame(uint32_t* canId, uint8_t* data, uint8_t* length);

/**
 * @brief  确认发送完成。
 */
void CanTp_TxConfirmation(void);

#endif /* CANTP_H */
