#include "can.h"
#include "stm32f10x_can.h"
#include <stddef.h>

#define CAN_CONTROLLER_ID       0u  // 仅一个CAN，索引为 0
#define CAN_TX_MAILBOX_COUNT    3u  // STM32F103 有 3 个发送邮箱

static const Can_ConfigType* g_canConfig = NULL;   // 全局配置指针，存储初始化时传入的配置
static volatile Can_ControllerStateType g_canControllerState = CAN_CS_UNINIT;  // 当前控制器状态
static volatile uint8_t g_canInterruptsEnabled = 1u;        // 中断使能状态跟踪
static PduIdType g_canTxSwHandle[CAN_TX_MAILBOX_COUNT];     // 发送邮箱软件句柄跟踪数组，索引对应邮箱编号
static uint8_t g_canTxPending[CAN_TX_MAILBOX_COUNT];        // 发送邮箱占用状态跟踪，为 0 表示空闲，为 1 表示占用

/**
 * @brief  上层接收指示弱回调。
 * @param  Mailbox: 硬件信息（ID、控制器、HOH）。
 * @param  PduInfo: 负载指针与长度。
 * @retval 无。
 */
__weak void CanIf_RxIndication(const Can_HwType* Mailbox, const PduInfoType* PduInfo)
{
    (void)Mailbox;
    (void)PduInfo;
}

/**
 * @brief  上层发送确认弱回调。
 * @param  CanTxPduId: 发送 PDU 的软件句柄。
 * @retval 无。
 */
__weak void CanIf_TxConfirmation(PduIdType CanTxPduId)
{
    (void)CanTxPduId;
}

/**
 * @brief  Bus-off 通知弱回调。
 * @param  ControllerId: 控制器索引。
 * @retval 无。
 */
__weak void CanIf_ControllerBusOff(uint8_t ControllerId)
{
    (void)ControllerId;
}

/**
 * @brief  控制器模式指示弱回调。
 * @param  ControllerId: 控制器索引。
 * @param  ControllerMode: 当前模式。
 * @retval 无。
 */
__weak void CanIf_ControllerModeIndication(uint8_t ControllerId, Can_ControllerStateType ControllerMode)
{
    (void)ControllerId;
    (void)ControllerMode;
}

/**
 * @brief  清空发送邮箱跟踪状态。
 * @retval 无。
 */
static void Can_ResetTxTracking(void)
{
    for (uint8_t i = 0; i < CAN_TX_MAILBOX_COUNT; i++)
    {
        g_canTxPending[i] = 0u;
        g_canTxSwHandle[i] = 0u;
    }
}

/**
 * @brief  校验控制器索引是否合法。
 * @param  Controller: 控制器索引。
 * @retval 合法返回 1，否则返回 0。
 */
static uint8_t Can_IsValidController(uint8_t Controller)
{
    return (Controller == CAN_CONTROLLER_ID) ? 1u : 0u;
}

/**
 * @brief  将控制器配置应用到 STM32 CAN 初始化结构体。
 * @param  cfg: 控制器配置。
 * @param  init: STM32 CAN 初始化结构体。
 * @retval 无。
 */
static void Can_ApplyControllerConfig(const Can_ControllerConfigType* cfg, CAN_InitTypeDef* init)
{
    if (cfg == NULL || init == NULL)
    {
        return;
    }

    if (cfg->prescaler != 0u)
    {
        init->CAN_Prescaler = cfg->prescaler;
    }
    if (cfg->sjw != 0u)
    {
        init->CAN_SJW = cfg->sjw;
    }
    if (cfg->bs1 != 0u)
    {
        init->CAN_BS1 = cfg->bs1;
    }
    if (cfg->bs2 != 0u)
    {
        init->CAN_BS2 = cfg->bs2;
    }
    init->CAN_ABOM = cfg->autoBusOff;
    init->CAN_AWUM = cfg->autoWakeUp;
    init->CAN_NART = cfg->noAutoRetransmission;
}

/**
 * @brief  配置 CAN 硬件滤波器。
 * @param  Config: CAN 驱动配置。
 * @retval 无。
 */
static void Can_ApplyFilters(const Can_ConfigType* Config)
{
    CAN_FilterInitTypeDef CAN_FilterInitStructure;

    if (Config == NULL || Config->filters == NULL || Config->filterCount == 0u)
    {
        CAN_FilterInitStructure.CAN_FilterNumber = 0;
        CAN_FilterInitStructure.CAN_FilterMode = CAN_FilterMode_IdMask;
        CAN_FilterInitStructure.CAN_FilterScale = CAN_FilterScale_32bit;
        CAN_FilterInitStructure.CAN_FilterIdHigh = 0x0000;
        CAN_FilterInitStructure.CAN_FilterIdLow = 0x0000;
        CAN_FilterInitStructure.CAN_FilterMaskIdHigh = 0x0000;
        CAN_FilterInitStructure.CAN_FilterMaskIdLow = 0x0000;
        CAN_FilterInitStructure.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
        CAN_FilterInitStructure.CAN_FilterActivation = ENABLE;
        CAN_FilterInit(&CAN_FilterInitStructure);
        return;
    }

    for (uint8_t i = 0; i < Config->filterCount; i++)
    {
        const Can_FilterConfigType* filter = &Config->filters[i];

        CAN_FilterInitStructure.CAN_FilterNumber = filter->filterNumber;
        CAN_FilterInitStructure.CAN_FilterMode = filter->filterMode;
        CAN_FilterInitStructure.CAN_FilterScale = filter->filterScale;
        CAN_FilterInitStructure.CAN_FilterIdHigh = filter->idHigh;
        CAN_FilterInitStructure.CAN_FilterIdLow = filter->idLow;
        CAN_FilterInitStructure.CAN_FilterMaskIdHigh = filter->maskHigh;
        CAN_FilterInitStructure.CAN_FilterMaskIdLow = filter->maskLow;
        CAN_FilterInitStructure.CAN_FilterFIFOAssignment = filter->fifoAssignment;
        CAN_FilterInitStructure.CAN_FilterActivation = filter->active;
        CAN_FilterInit(&CAN_FilterInitStructure);
    }
}

/**
 * @brief  读取 RX FIFO 中所有报文并通知上层。
 * @param  fifo: CAN_FIFO0 或 CAN_FIFO1。
 * @retval 无。
 */
static void Can_ProcessRxFifo(uint8_t fifo)
{
    while (CAN_MessagePending(CAN1, fifo) > 0u)
    {
        CanRxMsg RxMessage;
        uint8_t payload[8];  //用于存储接收数据的临时缓冲区
        Can_HwType hw;
        PduInfoType pduInfo;
        Can_IdType canId;

        CAN_Receive(CAN1, fifo, &RxMessage);

        for (uint8_t i = 0; i < RxMessage.DLC; i++)
        {
            payload[i] = RxMessage.Data[i];
        }

        if (RxMessage.IDE == CAN_Id_Extended)
        {
            canId = CAN_ID_EXTENDED_FLAG | (RxMessage.ExtId & 0x1FFFFFFFu);
        }
        else
        {
            canId = RxMessage.StdId & 0x7FFu;
        }

        if (RxMessage.RTR == CAN_RTR_Remote)
        {
            canId |= CAN_ID_RTR_FLAG;
        }

        hw.CanId = canId;
        hw.ControllerId = CAN_CONTROLLER_ID;
        hw.Hoh = 0u;

        pduInfo.SduLength = RxMessage.DLC;
        pduInfo.SduDataPtr = payload;

        CanIf_RxIndication(&hw, &pduInfo);
    }
}

/**
 * @brief  从 ESR 读取控制器错误状态。
 * @retval CAN_ERRORSTATE_*。
 */
static Can_ErrorStateType Can_ReadErrorState(void)
{
    if ((CAN1->ESR & CAN_ESR_BOFF) != 0u)
    {
        return CAN_ERRORSTATE_BUSOFF;
    }
    if ((CAN1->ESR & CAN_ESR_EPVF) != 0u)
    {
        return CAN_ERRORSTATE_PASSIVE;
    }
    return CAN_ERRORSTATE_ACTIVE;
}

// 静态底层初始化函数保留，供 Can_Init 内部调用
/**
 * @brief  初始化 CAN GPIO 引脚。
 * @retval 无。
 */
static void CAN_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    // 外部已开 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11; 
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12; 
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; 
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

/**
 * @brief  初始化 CAN NVIC 并开启中断。
 * @retval 无。
 */
static void CAN_NVIC_Init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn; 
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 4; 
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; 
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; 
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = USB_HP_CAN1_TX_IRQn;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = CAN1_SCE_IRQn;
    NVIC_Init(&NVIC_InitStructure);

    CAN_ITConfig(CAN1, CAN_IT_FMP0 | CAN_IT_TME | CAN_IT_ERR | CAN_IT_BOF | CAN_IT_WKU, ENABLE); 
}

//AUTOSAR 标准初始化接口
/**
 * @brief  初始化 CAN 控制器。
 * @param  Config: CAN 驱动配置。
 * @retval 无。
 */
void Can_Init(const Can_ConfigType* Config)
{
    CAN_InitTypeDef CAN_InitStructure;
    CAN_StructInit(&CAN_InitStructure);

    g_canConfig = Config;
    CAN_GPIO_Init(); // 内部调用 GPIO 初始化
    
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE); 

    CAN_InitStructure.CAN_Mode = CAN_Mode_Normal; 
    CAN_InitStructure.CAN_TTCM = DISABLE; 
    CAN_InitStructure.CAN_ABOM = DISABLE; // 建议在强干扰环境下开启 ENABLE
    CAN_InitStructure.CAN_AWUM = DISABLE; 
    CAN_InitStructure.CAN_NART = ENABLE;  
    CAN_InitStructure.CAN_RFLM = DISABLE; 
    CAN_InitStructure.CAN_TXFP = DISABLE; 

    // 波特率设置: 提示，你的代码注释写着 500k，但 36M/6 / (1+8+7) = 375kbps。
    // 如果要准确的 500kbps，建议设为 BS1=8, BS2=3
    CAN_InitStructure.CAN_SJW = CAN_SJW_1tq; 
    CAN_InitStructure.CAN_BS1 = CAN_BS1_8tq; 
    CAN_InitStructure.CAN_BS2 = CAN_BS2_7tq; 
    CAN_InitStructure.CAN_Prescaler = 6;     

    if (Config != NULL)
    {
        Can_ApplyControllerConfig(Config->controller, &CAN_InitStructure);
    }

    if (CAN_Init(CAN1, &CAN_InitStructure) != CAN_InitStatus_Success)
    {
        while(1); 
    }

    Can_ApplyFilters(Config);

    CAN_NVIC_Init(); // 内部调用 NVIC 初始化

    Can_ResetTxTracking();
    g_canControllerState = CAN_CS_STOPPED;
}

/**
 * @brief  反初始化 CAN 控制器。
 * @retval 无。
 */
void Can_DeInit(void)
{
    CAN_ITConfig(CAN1, CAN_IT_FMP0 | CAN_IT_TME | CAN_IT_ERR | CAN_IT_BOF | CAN_IT_WKU, DISABLE);
    CAN_DeInit(CAN1);
    g_canConfig = NULL;
    g_canControllerState = CAN_CS_UNINIT;
    Can_ResetTxTracking();
}

//AUTOSAR 标准发送接口
/**
 * @brief  发送一个 CAN PDU。
 * @param  HwTxPduId: 硬件发送句柄（本驱动未使用）。
 * @param  PduInfo: CAN PDU 信息。
 * @retval CAN_OK、CAN_NOT_OK 或 CAN_BUSY。
 */
Can_ReturnType Can_Write(Can_HwHandleType HwTxPduId, const Can_PduType* PduInfo)
{
    CanTxMsg TxMessage;
    uint8_t mailbox;

    (void)HwTxPduId;

    // 参数校验：防止空指针操作
    if (PduInfo == NULL || PduInfo->sdu == NULL || PduInfo->length > 8) {
        return CAN_NOT_OK;
    }

    if (g_canControllerState != CAN_CS_STARTED)
    {
        return CAN_NOT_OK;
    }

    // AUTOSAR 核心解析逻辑：依靠最高位 (Bit 31) 判断是否为扩展帧
    if ((PduInfo->id & CAN_ID_EXTENDED_FLAG) == CAN_ID_EXTENDED_FLAG) 
    {
        TxMessage.IDE = CAN_Id_Extended;
        TxMessage.ExtId = PduInfo->id & 0x1FFFFFFFU; // 清除最高位的标志位，保留真实 29位 ID
        TxMessage.StdId = 0;
    } 
    else 
    {
        TxMessage.IDE = CAN_Id_Standard;
        TxMessage.StdId = PduInfo->id & 0x7FFU;      // 保留低 11位 ID
        TxMessage.ExtId = 0;
    }

    if ((PduInfo->id & CAN_ID_RTR_FLAG) == CAN_ID_RTR_FLAG)
    {
        TxMessage.RTR = CAN_RTR_Remote;
    }
    else
    {
        TxMessage.RTR = CAN_RTR_Data;
    }
    TxMessage.DLC = PduInfo->length;

    for (uint8_t i = 0; i < PduInfo->length; i++)
    {
        TxMessage.Data[i] = PduInfo->sdu[i];
    }

    mailbox = CAN_Transmit(CAN1, &TxMessage);
    
    // 返回状态处理
    if (mailbox == CAN_TxStatus_NoMailBox) 
    {
        return CAN_BUSY; // 所有硬件邮箱均已满，发送失败
    }

    if (mailbox < CAN_TX_MAILBOX_COUNT)
    {
        g_canTxSwHandle[mailbox] = PduInfo->swPduHandle;
        g_canTxPending[mailbox] = 1u;
    }

    return CAN_OK; // 发送成功写入硬件邮箱
}

/**
 * @brief  切换控制器模式。
 * @param  Controller: 控制器索引。
 * @param  Transition: 状态迁移请求。
 * @retval 成功返回 E_OK，否则 E_NOT_OK。
 */
Std_ReturnType Can_SetControllerMode(uint8_t Controller, Can_StateTransitionType Transition)
{
    uint8_t status;

    if (!Can_IsValidController(Controller))
    {
        return E_NOT_OK;
    }

    switch (Transition)
    {
        case CAN_T_START:
            status = CAN_OperatingModeRequest(CAN1, CAN_OperatingMode_Normal);
            if (status == CAN_ModeStatus_Success)
            {
                g_canControllerState = CAN_CS_STARTED;
                CanIf_ControllerModeIndication(Controller, CAN_CS_STARTED);
                return E_OK;
            }
            break;
        case CAN_T_STOP:
            status = CAN_OperatingModeRequest(CAN1, CAN_OperatingMode_Initialization);
            if (status == CAN_ModeStatus_Success)
            {
                g_canControllerState = CAN_CS_STOPPED;
                CanIf_ControllerModeIndication(Controller, CAN_CS_STOPPED);
                return E_OK;
            }
            break;
        case CAN_T_SLEEP:
            status = CAN_OperatingModeRequest(CAN1, CAN_OperatingMode_Sleep);
            if (status == CAN_ModeStatus_Success)
            {
                g_canControllerState = CAN_CS_SLEEP;
                CanIf_ControllerModeIndication(Controller, CAN_CS_SLEEP);
                return E_OK;
            }
            break;
        case CAN_T_WAKEUP:
            status = CAN_OperatingModeRequest(CAN1, CAN_OperatingMode_Normal);
            if (status == CAN_ModeStatus_Success)
            {
                g_canControllerState = CAN_CS_STARTED;
                CanIf_ControllerModeIndication(Controller, CAN_CS_STARTED);
                return E_OK;
            }
            break;
        default:
            break;
    }

    return E_NOT_OK;
}

/**
 * @brief  获取当前控制器模式。
 * @param  Controller: 控制器索引。
 * @param  ControllerModePtr: 输出指针。
 * @retval 成功返回 E_OK，否则 E_NOT_OK。
 */
Std_ReturnType Can_GetControllerMode(uint8_t Controller, Can_ControllerStateType* ControllerModePtr)
{
    if (!Can_IsValidController(Controller) || ControllerModePtr == NULL)
    {
        return E_NOT_OK;
    }

    *ControllerModePtr = g_canControllerState;
    return E_OK;
}

/**
 * @brief  获取当前控制器错误状态。
 * @param  Controller: 控制器索引。
 * @param  ErrorStatePtr: 输出指针。
 * @retval 成功返回 E_OK，否则 E_NOT_OK。
 */
Std_ReturnType Can_GetControllerErrorState(uint8_t Controller, Can_ErrorStateType* ErrorStatePtr)
{
    if (!Can_IsValidController(Controller) || ErrorStatePtr == NULL)
    {
        return E_NOT_OK;
    }

    *ErrorStatePtr = Can_ReadErrorState();
    return E_OK;
}

/**
 * @brief  关闭控制器中断。
 * @param  Controller: 控制器索引。
 * @retval 无。
 */
void Can_DisableControllerInterrupts(uint8_t Controller)
{
    if (!Can_IsValidController(Controller))
    {
        return;
    }

    CAN_ITConfig(CAN1, CAN_IT_FMP0 | CAN_IT_TME | CAN_IT_ERR | CAN_IT_BOF | CAN_IT_WKU, DISABLE);
    g_canInterruptsEnabled = 0u;
}

/**
 * @brief  开启控制器中断。
 * @param  Controller: 控制器索引。
 * @retval 无。
 */
void Can_EnableControllerInterrupts(uint8_t Controller)
{
    if (!Can_IsValidController(Controller))
    {
        return;
    }

    CAN_ITConfig(CAN1, CAN_IT_FMP0 | CAN_IT_TME | CAN_IT_ERR | CAN_IT_BOF | CAN_IT_WKU, ENABLE);
    g_canInterruptsEnabled = 1u;
}

/**
 * @brief  获取模块版本信息。
 * @param  versioninfo: 输出指针。
 * @retval 无。
 */
void Can_GetVersionInfo(Std_VersionInfoType* versioninfo)
{
    if (versioninfo == NULL)
    {
        return;
    }

    versioninfo->vendorID = CAN_VENDOR_ID;
    versioninfo->moduleID = CAN_MODULE_ID;
    versioninfo->sw_major_version = CAN_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = CAN_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = CAN_SW_PATCH_VERSION;
}

/**
 * @brief  轮询 RX FIFO 并通知上层。
 * @retval 无。
 */
void Can_MainFunction_Read(void)
{
    if (g_canControllerState != CAN_CS_STARTED)
    {
        return;
    }

    Can_ProcessRxFifo(CAN_FIFO0);
    Can_ProcessRxFifo(CAN_FIFO1);
}

/**
 * @brief  轮询发送邮箱并确认已完成发送。
 * @retval 无。
 */
void Can_MainFunction_Write(void)
{
    if (g_canControllerState != CAN_CS_STARTED)
    {
        return;
    }

    for (uint8_t mailbox = 0; mailbox < CAN_TX_MAILBOX_COUNT; mailbox++)
    {
        if (g_canTxPending[mailbox] == 0u)
        {
            continue;
        }

        if (CAN_TransmitStatus(CAN1, mailbox) == CAN_TxStatus_Ok)
        {
            g_canTxPending[mailbox] = 0u;
            CanIf_TxConfirmation(g_canTxSwHandle[mailbox]);
        }
        else if (CAN_TransmitStatus(CAN1, mailbox) == CAN_TxStatus_Failed)
        {
            g_canTxPending[mailbox] = 0u;
        }
    }
}

/**
 * @brief  轮询 Bus-off 状态。
 * @retval 无。
 */
void Can_MainFunction_BusOff(void)
{
    if (Can_ReadErrorState() == CAN_ERRORSTATE_BUSOFF)
    {
        CanIf_ControllerBusOff(CAN_CONTROLLER_ID);
    }
}

/**
 * @brief  轮询唤醒状态（占位）。
 * @retval 无。
 */
void Can_MainFunction_Wakeup(void)
{
    /* No polling action required in this minimal driver. */
}

/**
 * @brief  轮询模式变化（占位）。
 * @retval 无。
 */
void Can_MainFunction_Mode(void)
{
    /* Mode indication is handled during SetControllerMode. */
}

/**
 * @brief  轮询错误状态（最小化 Bus-off 处理）。
 * @retval 无。
 */
void Can_MainFunction_Error(void)
{
    if (g_canControllerState == CAN_CS_STARTED)
    {
        Can_MainFunction_BusOff();
    }
}

/**
 * @brief  RX FIFO0 中断处理（驱动层）。
 * @retval 无。
 */
void Can_IsrRxFifo0(void)
{
    if (g_canInterruptsEnabled != 0u)
    {
        Can_ProcessRxFifo(CAN_FIFO0);
    }
}

/**
 * @brief  RX FIFO1 中断处理（驱动层）。
 * @retval 无。
 */
void Can_IsrRxFifo1(void)
{
    if (g_canInterruptsEnabled != 0u)
    {
        Can_ProcessRxFifo(CAN_FIFO1);
    }
}

/**
 * @brief  发送中断处理（驱动层）。
 * @retval 无。
 */
void Can_IsrTx(void)
{
    if (g_canInterruptsEnabled != 0u)
    {
        Can_MainFunction_Write();
    }
}

/**
 * @brief  Bus-off 中断处理（驱动层）。
 * @retval 无。
 */
void Can_IsrBusOff(void)
{
    Can_MainFunction_BusOff();
}

/**
 * @brief  唤醒中断处理（驱动层）。
 * @retval 无。
 */
void Can_IsrWakeup(void)
{
    Can_MainFunction_Wakeup();
}

/**
 * @brief  错误中断处理（驱动层）。
 * @retval 无。
 */
void Can_IsrError(void)
{
    Can_MainFunction_Error();
}

/**
 * @brief  MCU RX0 中断入口。
 * @retval 无。
 */
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    Can_IsrRxFifo0();
}

/**
 * @brief  MCU TX 中断入口。
 * @retval 无。
 */
void USB_HP_CAN1_TX_IRQHandler(void)
{
    Can_IsrTx();
}

/**
 * @brief  MCU CAN SCE 中断入口。
 * @retval 无。
 */
void CAN1_SCE_IRQHandler(void)
{
    Can_IsrError();
}
