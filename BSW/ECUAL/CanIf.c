#include "CanIf.h"

static struct rt_mailbox s_canif_rx_mb;                 // RT 邮箱对象
static rt_ubase_t s_canif_rx_mb_pool[CANIF_RT_MB_SIZE]; // RT 邮箱消息池

static CanIf_RxMsgType s_canif_rx_pool[CANIF_RX_POOL_SIZE]; // 接收消息缓存池
static uint8_t s_canif_free_idx[CANIF_RX_POOL_SIZE];        // 空闲索引队列
static uint8_t s_canif_free_head = 0u;                       // 空闲队列头
static uint8_t s_canif_free_tail = 0u;                       // 空闲队列尾
static uint8_t s_canif_free_count = 0u;                      // 空闲数量
static uint8_t s_canif_inited = 0u;                          // 初始化标志

/**
 * @brief  初始化接收缓存池。
 * @retval 无。
 */
static void CanIf_InitPool(void)
{
	s_canif_free_head = 0u;
	s_canif_free_tail = 0u;
	s_canif_free_count = CANIF_RX_POOL_SIZE;

	for (uint8_t i = 0u; i < CANIF_RX_POOL_SIZE; i++)
	{
		s_canif_free_idx[i] = i;
	}
}

/**
 * @brief  从缓存池申请一个条目。
 * @retval 成功返回指针，失败返回 RT_NULL。
 */
static CanIf_RxMsgType* CanIf_AllocMsg(void)
{
	CanIf_RxMsgType* msg = RT_NULL;

	rt_enter_critical();  //进入临界区保护共享资源
	if (s_canif_free_count > 0u)
	{
		uint8_t idx = s_canif_free_idx[s_canif_free_tail];
		s_canif_free_tail = (uint8_t)((s_canif_free_tail + 1u) % CANIF_RX_POOL_SIZE);
		s_canif_free_count--;
		msg = &s_canif_rx_pool[idx];
	}
	rt_exit_critical();

	return msg;
}	

/**
 * @brief  释放一个缓存条目回池。
 * @param  msg: 待释放的消息指针。
 * @retval 无。
 */
static void CanIf_FreeMsg(CanIf_RxMsgType* msg)
{
	if (msg == RT_NULL)
	{
		return;
	}

	rt_enter_critical();
	if (s_canif_free_count < CANIF_RX_POOL_SIZE)
	{
		uint8_t idx = (uint8_t)(msg - s_canif_rx_pool);//两个同类型指针相减得到索引，编译器会自动帮你除以 sizeof(结构体)
		if (idx < CANIF_RX_POOL_SIZE)
		{
			s_canif_free_idx[s_canif_free_head] = idx;
			s_canif_free_head = (uint8_t)((s_canif_free_head + 1u) % CANIF_RX_POOL_SIZE);
			s_canif_free_count++;
		}
	}
	rt_exit_critical();
}

/**
 * @brief  初始化 CanIf 模块（创建邮箱与缓存池）。
 * @retval 无。
 */
void CanIf_Init(void)
{
	if (s_canif_inited != 0u)
	{
		return;
	}

	CanIf_InitPool();
	rt_mb_init(&s_canif_rx_mb, "canif_rx", s_canif_rx_mb_pool, CANIF_RT_MB_SIZE, RT_IPC_FLAG_FIFO);
	s_canif_inited = 1u;
}

/**
 * @brief  发送一帧 CAN 报文（CanIf -> MCAL）。
 * @param  TxPduId: 发送 PDU 的软件句柄。用于上层区分不同的发送请求，当前实现未使用。
 * @param  PduInfo: CAN PDU 信息。
 * @retval CAN_OK、CAN_NOT_OK 或 CAN_BUSY。
 */
Can_ReturnType CanIf_Transmit(PduIdType TxPduId, const Can_PduType* PduInfo)
{
	Can_PduType localPdu;

	if (PduInfo == RT_NULL)
	{
		return CAN_NOT_OK;
	}

	localPdu = *PduInfo;
	localPdu.swPduHandle = TxPduId;

	return Can_Write(0u, &localPdu);  
}

void CanIf_MainFunction_Read(void)
{
	Can_MainFunction_Read();
}
/**
 * @brief  从 CanIf 邮箱读取一条接收消息（阻塞/超时可选）。
 * @param  msg: 输出消息指针。
 * @param  timeout: 等待超时（rt_tick，-1 表示永久等待）。
 * @retval RT_EOK 成功，其它表示失败。
 */
rt_err_t CanIf_ReadRx(CanIf_RxMsgType* msg, rt_int32_t timeout)
{
	rt_ubase_t value = 0u;
	CanIf_RxMsgType* src = RT_NULL;
	rt_err_t err;

	if (msg == RT_NULL)
	{
		return -RT_ERROR;
	}

	if (s_canif_inited == 0u)
	{
		CanIf_Init();
	}

	err = rt_mb_recv(&s_canif_rx_mb, &value, timeout);
	if (err != RT_EOK)
	{
		return err;
	}

	src = (CanIf_RxMsgType*)value;
	if (src == RT_NULL)
	{
		return -RT_ERROR;
	}

	*msg = *src;
	CanIf_FreeMsg(src);
	return RT_EOK;
}

/**
 * @brief  上层接收指示回调（由 MCAL 调用）。
 * @param  Mailbox: 硬件信息（ID、控制器、HOH）。
 * @param  PduInfo: 负载指针与长度。
 * @retval 无。
 */
void CanIf_RxIndication(const Can_HwType* Mailbox, const PduInfoType* PduInfo)
{
	CanIf_RxMsgType* msg;

	if (Mailbox == RT_NULL || PduInfo == RT_NULL || PduInfo->SduDataPtr == RT_NULL)
	{
		return;
	}

	if (s_canif_inited == 0u)
	{
		CanIf_Init();
	}

	msg = CanIf_AllocMsg();
	if (msg == RT_NULL)
	{
		return;
	}

	msg->hw = *Mailbox;
	msg->dlc = (uint8_t)PduInfo->SduLength;
	if (msg->dlc > 8u)
	{
		msg->dlc = 8u;
	}

	for (uint8_t i = 0u; i < msg->dlc; i++)
	{
		msg->data[i] = PduInfo->SduDataPtr[i];
	}

	if (rt_mb_send(&s_canif_rx_mb, (rt_ubase_t)msg) != RT_EOK)
	{
		CanIf_FreeMsg(msg);
	}
}

/**
 * @brief  上层发送确认回调（由 MCAL 调用）。
 * @param  CanTxPduId: 发送 PDU 的软件句柄。
 * @retval 无。
 */
void CanIf_TxConfirmation(PduIdType CanTxPduId)
{
	(void)CanTxPduId;
	/* 预留给 RTE 或上层逻辑，当前不处理 */
}

/**
 * @brief  Bus-off 通知回调（由 MCAL 调用）。
 * @param  ControllerId: 控制器索引。
 * @retval 无。
 */
void CanIf_ControllerBusOff(uint8_t ControllerId)
{
	(void)ControllerId;
	/* 预留给 RTE 或上层逻辑，当前不处理 */
}

/**
 * @brief  控制器模式指示回调（由 MCAL 调用）。
 * @param  ControllerId: 控制器索引。
 * @param  ControllerMode: 当前模式。
 * @retval 无。
 */
void CanIf_ControllerModeIndication(uint8_t ControllerId, Can_ControllerStateType ControllerMode)
{
	(void)ControllerId;
	(void)ControllerMode;
	/* 预留给 RTE 或上层逻辑，当前不处理 */
}
