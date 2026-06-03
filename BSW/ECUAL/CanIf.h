#ifndef CANIF_H
#define CANIF_H

#include "can.h"
#include <rtthread.h>

/* CanIf 接收邮箱和缓存配置 */
#define CANIF_RT_MB_SIZE        16u   // RT 邮箱深度（保存指针）
#define CANIF_RX_POOL_SIZE      16u   // 接收缓存池数量

/**
 * @brief  CanIf 接收消息结构体。
 */
typedef struct
{
	Can_HwType hw;     // 硬件信息（ID、控制器、HOH）
	uint8_t dlc;       // 数据长度
	uint8_t data[8];   // 数据缓存
}CanIf_RxMsgType;

/**
 * @brief  初始化 CanIf 模块（创建邮箱与缓存池）。
 * @retval 无。
 */
void CanIf_Init(void);

/**
 * @brief  发送一帧 CAN 报文（CanIf -> MCAL）。
 * @param  TxPduId: 发送 PDU 的软件句柄。
 * @param  PduInfo: CAN PDU 信息。
 * @retval CAN_OK、CAN_NOT_OK 或 CAN_BUSY。
 */
Can_ReturnType CanIf_Transmit(PduIdType TxPduId, const Can_PduType* PduInfo);

/**
 * @brief  主循环读取函数，需定期调用以处理接收消息。
 * @retval 无。
 */
void CanIf_MainFunction_Read(void);

/**
 * @brief  从 CanIf 邮箱读取一条接收消息（阻塞/超时可选）。
 * @param  msg: 输出消息指针。
 * @param  timeout: 等待超时（rt_tick，-1 表示永久等待）。
 * @retval RT_EOK 成功，其它表示失败。
 */
rt_err_t CanIf_ReadRx(CanIf_RxMsgType* msg, rt_int32_t timeout);

/**
 * @brief  上层发送确认回调（由 MCAL 调用）。
 * @param  CanTxPduId: 发送 PDU 的软件句柄。
 * @retval 无。
 */
void CanIf_TxConfirmation(PduIdType CanTxPduId);

/**
 * @brief  上层接收指示回调（由 MCAL 调用）。
 * @param  Mailbox: 硬件信息（ID、控制器、HOH）。
 * @param  PduInfo: 负载指针与长度。
 * @retval 无。
 */
void CanIf_RxIndication(const Can_HwType* Mailbox, const PduInfoType* PduInfo);

/**
 * @brief  Bus-off 通知回调（由 MCAL 调用）。
 * @param  ControllerId: 控制器索引。
 * @retval 无。
 */
void CanIf_ControllerBusOff(uint8_t ControllerId);

/**
 * @brief  控制器模式指示回调（由 MCAL 调用）。
 * @param  ControllerId: 控制器索引。
 * @param  ControllerMode: 当前模式。
 * @retval 无。
 */
void CanIf_ControllerModeIndication(uint8_t ControllerId, Can_ControllerStateType ControllerMode);

#endif /* CANIF_H */
