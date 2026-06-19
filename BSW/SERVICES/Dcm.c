/**
 * @file    Dcm.c 应用层诊断模块实现
 * @brief   DCM 模块实现 — UDS SID 分发与处理框架
 * 
 * 实现以下 UDS 服务骨架（待填充业务逻辑）：
 *   - 0x10  Diagnostic Session Control
 *   - 0x11  ECU Reset
 *   - 0x22  Read Data By Identifier
 *   - 0x2E  Write Data By Identifier
 *   - 0x3E  Tester Present
 *   - 0x14  Clear DTC Info (预留)
 *   - 0x19  Read DTC Info (预留)
 *   - 0x27  Security Access (预留)
 *   - 0x31  Routine Control (预留)
 */

#include "Dcm.h"
#include "Dcm_Cfg.h"

/*===========================================================================
 * 内部状态变量
 *===========================================================================*/
static Dcm_SessionType  Dcm_ActiveSession     = DCM_SESSION_DEFAULT;   /* 当前诊断会话类型（默认/编程/扩展/安全） */
static Dcm_SecLevelType Dcm_SecLevel          = DCM_SEC_LEVEL_LOCKED;  /* 当前安全访问等级（锁定/已解锁） */
static uint32_t         Dcm_TesterPresentTimer = 0u;                   /* TesterPresent 超时计数器（单位：ms） */
static bool             Dcm_TesterPresent_Seen = false;                 /* 标记本周期是否收到 TesterPresent 请求 */

/* 响应缓存 */
static CanTp_MsgType  Dcm_ResponseBuffer;                      /* UDS 响应帧发送缓冲区（CAN ID + 数据 + 长度） */
static bool           Dcm_ResponsePending      = false;          /* 标记是否有待发送的响应帧 */
static bool           Dcm_SuppressPositiveResp = false;          /* 抑制肯定响应标志（来自请求 byte[1] bit7） */

/* ECU 复位延迟标志 — 先发响应再复位 */
static bool         Dcm_EcuResetPending      = false;          /* ECU 复位挂起标志（响应发送后执行复位） */
static uint8_t      Dcm_EcuResetType         = 0u;             /* 待执行的复位类型（硬复位/软复位） */

/*===========================================================================
 * 内部函数前置声明
 *===========================================================================*/
static uint8_t  Dcm_GetMessageLength(const CanTp_MsgType* msg);
static bool     Dcm_IsFuncAddr(const CanTp_MsgType* msg);
static void     Dcm_SendPositiveResponse(uint8_t sid, const uint8_t* data, uint8_t len);
static void     Dcm_SendNegativeResponse(uint8_t sid, uint8_t nrc);
static void     Dcm_SessionTimeoutCheck(void);

/* --- 各 SID 处理函数 --- */
static Dcm_StatusType Dcm_HandleSessionControl   (const CanTp_MsgType* msg);
static Dcm_StatusType Dcm_HandleEcuReset         (const CanTp_MsgType* msg);
static Dcm_StatusType Dcm_HandleTesterPresent    (const CanTp_MsgType* msg);
static Dcm_StatusType Dcm_HandleReadDataById     (const CanTp_MsgType* msg);
static Dcm_StatusType Dcm_HandleWriteDataById    (const CanTp_MsgType* msg);
static Dcm_StatusType Dcm_HandleSecurityAccess   (const CanTp_MsgType* msg);
static Dcm_StatusType Dcm_HandleReadDtcInfo      (const CanTp_MsgType* msg);
static Dcm_StatusType Dcm_HandleClearDtcInfo     (const CanTp_MsgType* msg);
static Dcm_StatusType Dcm_HandleRoutineControl   (const CanTp_MsgType* msg);

/*===========================================================================
 * 公开 API
 *===========================================================================*/

void Dcm_Init(void)
{
    Dcm_ActiveSession      = DCM_SESSION_DEFAULT;
    Dcm_SecLevel           = DCM_SEC_LEVEL_LOCKED;
    Dcm_TesterPresentTimer = 0u;
    Dcm_TesterPresent_Seen = false;
    Dcm_ResponsePending    = false;
    Dcm_SuppressPositiveResp= false;
    Dcm_EcuResetPending    = false;
    Dcm_EcuResetType       = 0u;
}

void Dcm_MainFunction(void)
{
    Dcm_SessionTimeoutCheck();
}

Dcm_StatusType Dcm_ProcessRequest(const CanTp_MsgType* msg)
{
    uint8_t sid;  /* 诊断服务 ID（Service Identifier），从请求帧第一个字节提取 */

    if (msg == (const CanTp_MsgType*)0)
    {
        return DCM_E_PARAM;
    }

    if (msg->length < 1u)
    {
        return DCM_E_MSG_LEN;
    }

    sid = msg->data[0];

    /*
     * suppressPosRsp 位检查（所有带子功能的服务通用）
     * ISO 14229-1: 请求报文 byte[1] 的 bit7 = 1 表示抑制肯定响应
     * 适用于: 0x10, 0x11, 0x14, 0x19, 0x22(无子功能, 不适用),
     *          0x27, 0x2E, 0x31, 0x3E 等
     */
    Dcm_SuppressPositiveResp = false;
    if (msg->length >= 2u)
    {
        if ((msg->data[1] & 0x80u) != 0u)
        {
            Dcm_SuppressPositiveResp = true;
        }
    }

    /*===========================================================================
     * UDS SID 主分发器
     *===========================================================================*/
    switch (sid)
    {
        /* --- 0x10 诊断会话控制 --- */
        case DCM_SID_DIAG_SESSION_CTRL:
            return Dcm_HandleSessionControl(msg);

        /* --- 0x11 ECU 复位 --- */
        case DCM_SID_ECU_RESET:
            return Dcm_HandleEcuReset(msg);

        /* --- 0x14 清除 DTC (预留) --- */
        case DCM_SID_CLEAR_DTC_INFO:
            return Dcm_HandleClearDtcInfo(msg);

        /* --- 0x19 读取 DTC (预留) --- */
        case DCM_SID_READ_DTC_INFO:
            return Dcm_HandleReadDtcInfo(msg);

        /* --- 0x22 按 ID 读数据 --- */
        case DCM_SID_READ_DATA_BY_ID:
            return Dcm_HandleReadDataById(msg);

        /* --- 0x27 安全访问 (预留) --- */
        case DCM_SID_SECURITY_ACCESS:
            return Dcm_HandleSecurityAccess(msg);

        /* --- 0x2E 按 ID 写数据 --- */
        case DCM_SID_WRITE_DATA_BY_ID:
            return Dcm_HandleWriteDataById(msg);

        /* --- 0x31 例程控制 (预留) --- */
        case DCM_SID_ROUTINE_CONTROL:
            return Dcm_HandleRoutineControl(msg);

        /* --- 0x3E 测试仪保持 --- */
        case DCM_SID_TESTER_PRESENT:
            return Dcm_HandleTesterPresent(msg);

        /* --- 未知 SID --- */
        default:
            Dcm_SendNegativeResponse(sid, DCM_NRC_SERVICE_NOT_SUPPORTED);
            return DCM_E_SID_NOT_SUPPORTED;
    }
}

bool Dcm_GetResponse(CanTp_MsgType* respMsg)
{
    if (!Dcm_ResponsePending || respMsg == (CanTp_MsgType*)0)
    {
        return false;
    }

    *respMsg = Dcm_ResponseBuffer;
    Dcm_ResponsePending = false;
    return true;
}

void Dcm_TxConfirmation(void)
{
    /* 预留：可用于流控或发送确认处理 */
}

/*===========================================================================
 * 内部: 工具函数
 *===========================================================================*/

static uint8_t Dcm_GetMessageLength(const CanTp_MsgType* msg)
{
    return msg->length;
}

/**
 * @brief 判断是否为功能寻址请求（CAN ID 匹配功能寻址 ID）。
 *        功能寻址：不发送否定响应；只能收到单帧。
 */
static bool Dcm_IsFuncAddr(const CanTp_MsgType* msg)
{
    return (msg->canId == DCM_CANID_FUNC_REQ);
}

/**
 * @brief 构造肯定响应并存入发送缓冲区。
 *        实际 CAN 发送由 Rte_MainFunction_Dcm() 轮询 Dcm_GetResponse() 完成。
 * @param sid: 请求 SID
 * @param data: 响应数据负载（不含 SID 字节）
 * @param len:  数据负载长度
 * 
 * 响应格式: [SID|0x40] + data[0..len-1]
 */
static void Dcm_SendPositiveResponse(uint8_t sid, const uint8_t* data, uint8_t len)
{
    if (Dcm_SuppressPositiveResp)
    {
        return;  /* 请求要求抑制正响应 */
    }

    Dcm_ResponseBuffer.canId   = DCM_CANID_PHYS_RESP;       /* 物理响应 CAN ID */
    Dcm_ResponseBuffer.data[0] = sid | DCM_NRC_POSITIVE_RESP_MASK; /* 响应 SID = 请求 SID + 0x40 */
    Dcm_ResponseBuffer.length  = 1u;

    if (data != (const uint8_t*)0 && len > 0u)
    {
        uint8_t i;  /* 循环计数器：拷贝数据负载到响应缓冲区 */
        for (i = 0u; (i < len) && ((1u + i) < 8u); i++)
        {
            Dcm_ResponseBuffer.data[1u + i] = data[i];
        }
        Dcm_ResponseBuffer.length = 1u + i;
    }

    Dcm_ResponsePending = true; //表示有响应待发送，Rte_MainFunction_Dcm() 会轮询 Dcm_GetResponse() 获取并发送
}

/**
 * @brief 构造否定响应并存入发送缓冲区。
 *        实际 CAN 发送由 Rte_MainFunction_Dcm() 轮询 Dcm_GetResponse() 完成。
 * @param sid: 请求 SID
 * @param nrc: 否定响应码 NRC
 * 
 * 响应格式: [0x7F] + [SID] + [NRC]
 */
static void Dcm_SendNegativeResponse(uint8_t sid, uint8_t nrc)
{
    Dcm_ResponseBuffer.canId   = DCM_CANID_PHYS_RESP;  /* 物理响应 CAN ID */
    Dcm_ResponseBuffer.data[0] = 0x7Fu;                 /* 否定响应 SID 固定为 0x7F */
    Dcm_ResponseBuffer.data[1] = sid;                    /* 原请求的 SID */
    Dcm_ResponseBuffer.data[2] = nrc;                    /* 否定响应码（NRC） */
    Dcm_ResponseBuffer.length  = 3u;

    Dcm_ResponsePending = true;
}

/**
 * @brief  查询 ECU 是否有待执行的复位请求。
 */
bool Dcm_IsEcuResetPending(void)
{
    return Dcm_EcuResetPending;
}

/*===========================================================================
 * 会话超时管理
 *===========================================================================*/

static void Dcm_SessionTimeoutCheck(void)
{
    /* 默认会话不需要超时回退 */
    if (Dcm_ActiveSession == DCM_SESSION_DEFAULT)
    {
        return;
    }

    /* S3 Server 超时: 若在非默认会话且在 S3 时间内未收到 TesterPresent，回退默认会话 */
    /* TODO: 需要与 OS tick 对接，此处仅给出框架逻辑 */
    /*
    if (Dcm_TesterPresent_Seen)
    {
        Dcm_TesterPresentTimer = 0u;
        Dcm_TesterPresent_Seen = false;
    }
    else
    {
        Dcm_TesterPresentTimer += DCM_MAIN_CYCLE_MS;
        if (Dcm_TesterPresentTimer >= DCM_S3_SERVER_TIMEOUT_MS)
        {
            Dcm_ActiveSession = DCM_SESSION_DEFAULT;
            Dcm_SecLevel      = DCM_SEC_LEVEL_LOCKED;
            Dcm_TesterPresentTimer = 0u;
        }
    }
    */
    (void)Dcm_TesterPresentTimer;
}

/*===========================================================================
 * SID 0x10 — 诊断会话控制
 *===========================================================================*/
static Dcm_StatusType Dcm_HandleSessionControl(const CanTp_MsgType* msg)
{
    uint8_t subFunc;  /* 子功能码（去掉 bit7 suppressPosRsp 后的值） */

    if (Dcm_GetMessageLength(msg) < 2u)
    {
        Dcm_SendNegativeResponse(DCM_SID_DIAG_SESSION_CTRL, DCM_NRC_INCORRECT_MSG_LEN);
        return DCM_E_MSG_LEN;
    }

    subFunc = msg->data[1] & 0x7Fu;  /* 去掉 bit7 suppressPosRsp 因为在uds帧中第二个字节的bit7表示是否抑制正响应，实际子功能值在 bit0~6 */

    switch (subFunc)
    {
        case DCM_SESSION_DEFAULT:
        case DCM_SESSION_PROGRAMMING:
        case DCM_SESSION_EXTENDED:
        case DCM_SESSION_SAFETY:
            /* TODO: 执行会话切换的业务逻辑（如：通知应用层） */
            Dcm_ActiveSession = (Dcm_SessionType)subFunc;
            Dcm_SecLevel      = DCM_SEC_LEVEL_LOCKED;  /* 会话切换后安全锁定 */
            Dcm_TesterPresentTimer = 0u;
            {
                uint8_t respData[1];       /* 肯定响应数据：[子功能值] */
                respData[0] = subFunc;
                Dcm_SendPositiveResponse(DCM_SID_DIAG_SESSION_CTRL, respData, 1u);
            }
            return DCM_OK;

        default:
            Dcm_SendNegativeResponse(DCM_SID_DIAG_SESSION_CTRL, DCM_NRC_SUBFUNC_NOT_SUPPORTED);
            return DCM_E_SUBFUNC_NOT_SUPPORTED;
    }
}

/*===========================================================================
 * SID 0x11 — ECU 复位
 *===========================================================================*/
static Dcm_StatusType Dcm_HandleEcuReset(const CanTp_MsgType* msg)
{
    uint8_t resetType;       /* 复位类型子功能码（硬复位/软复位等） */
    uint8_t respData[1];     /* 肯定响应数据：[复位类型] */
    if (Dcm_GetMessageLength(msg) < 2u)
    {
        Dcm_SendNegativeResponse(DCM_SID_ECU_RESET, DCM_NRC_INCORRECT_MSG_LEN);
        return DCM_E_MSG_LEN;
    }

    resetType = msg->data[1] & 0x7Fu;

    switch (resetType)
    {
        case DCM_RESET_HARD:
        case DCM_RESET_SOFT:
            /* 先发肯定响应，再执行复位 */
            respData[0] = resetType;
            Dcm_SendPositiveResponse(DCM_SID_ECU_RESET, respData, 1u);
            /* 置位复位标志，由 RTE 在响应发送完成后执行实际复位 */
            Dcm_EcuResetPending = true;
            Dcm_EcuResetType    = resetType;
            return DCM_OK;

        case DCM_RESET_KEY_OFF_ON:
        case DCM_RESET_ENABLE_RAPID_PWR:
        case DCM_RESET_DISABLE_RAPID_PWR:
        default:
            Dcm_SendNegativeResponse(DCM_SID_ECU_RESET, DCM_NRC_SUBFUNC_NOT_SUPPORTED);
            return DCM_E_SUBFUNC_NOT_SUPPORTED;
    }
}

/*===========================================================================
 * SID 0x3E — 测试仪保持 (Tester Present)
 *===========================================================================*/
static Dcm_StatusType Dcm_HandleTesterPresent(const CanTp_MsgType* msg)
{
    uint8_t subFunc;  /* 子功能码：0x00=需要响应, 0x80=抑制响应 */

    if (Dcm_GetMessageLength(msg) < 2u)
    {
        Dcm_SendNegativeResponse(DCM_SID_TESTER_PRESENT, DCM_NRC_INCORRECT_MSG_LEN);
        return DCM_E_MSG_LEN;
    }

    subFunc = msg->data[1] & 0x7Fu;

    /* 子功能: 0x00(需要响应), 0x80(抑制响应, bit7=1) */
    if (subFunc != 0x00u)
    {
        Dcm_SendNegativeResponse(DCM_SID_TESTER_PRESENT, DCM_NRC_SUBFUNC_NOT_SUPPORTED);
        return DCM_E_SUBFUNC_NOT_SUPPORTED;
    }

    /* 刷新看门狗计时器 */
    Dcm_TesterPresent_Seen = true;

    /* 发送肯定响应（如未被抑制） */
    {
        uint8_t respData[1];       /* 肯定响应数据：[0x00] */
        respData[0] = 0x00u;
        Dcm_SendPositiveResponse(DCM_SID_TESTER_PRESENT, respData, 1u);
    }
    return DCM_OK;
}

/*===========================================================================
 * SID 0x22 — 按 ID 读取数据 (Read Data By Identifier)
 *===========================================================================*/
static Dcm_StatusType Dcm_HandleReadDataById(const CanTp_MsgType* msg)
{
    uint16_t did;  /* 数据标识符（Data Identifier），Big-Endian 2字节 */

    if (Dcm_GetMessageLength(msg) < 3u)
    {
        Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_INCORRECT_MSG_LEN);
        return DCM_E_MSG_LEN;
    }

    /* DID 是 Big-Endian 2 字节 */
    did = ((uint16_t)msg->data[1] << 8) | (uint16_t)msg->data[2];

    /*===========================================================================
     * DID 数据读取分发
     *===========================================================================*/
    switch (did)
    {
        /* --- 系统信息 --- */
        case DCM_DID_SYSTEM_SUPPLIER_ID:
            /* TODO: 返回供应商 ID */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_ECU_SERIAL_NUMBER:
            /* TODO: 返回 ECU 序列号 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_SOFTWARE_VERSION:
            /* TODO: 返回软件版本号 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_HARDWARE_VERSION:
            /* TODO: 返回硬件版本号 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_BOOT_SW_VERSION:
            /* TODO: 返回 Bootloader 版本号 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        /* --- BMS 实时数据 --- */
        case DCM_DID_BATTERY_VOLTAGE:
            /* TODO: Rte_Call_BmsRead_BatteryVoltage() → 4 bytes float */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_BATTERY_CURRENT:
            /* TODO: Rte_Call_BmsRead_BatteryCurrent() */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_BATTERY_SOC:
            /* TODO: Rte_Call_BmsRead_SOC() */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_BATTERY_SOH:
            /* TODO: Rte_Call_BmsRead_SOH() */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_MAX_CELL_VOLTAGE:
            /* TODO: 返回最高单体电压 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_MIN_CELL_VOLTAGE:
            /* TODO: 返回最低单体电压 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_MAX_CELL_TEMP:
            /* TODO: 返回最高单体温度 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_MIN_CELL_TEMP:
            /* TODO: 返回最低单体温度 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        /* --- BMS 状态 --- */
        case DCM_DID_BMS_STATUS:
            /* TODO: 返回 BMS 运行状态 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_CHARGE_STATUS:
            /* TODO: 返回充电状态 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_BALANCE_STATUS:
            /* TODO: 返回均衡状态 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_PROTECT_FLAG:
            /* TODO: 返回保护标志位 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_FAULT_CODE:
            /* TODO: 返回当前故障码 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        /* --- 标定参数 (可读写) --- */
        case DCM_DID_CHARGE_OVER_V_THD:
        case DCM_DID_DISCHARGE_UNDER_V_THD:
        case DCM_DID_CHARGE_OVER_C_THD:
        case DCM_DID_CELL_OV_THD:
        case DCM_DID_CELL_UV_THD:
        case DCM_DID_CELL_OT_THD:
        case DCM_DID_BALANCE_ENABLE_THD:
            /* TODO: 读取标定参数值 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        default:
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;
    }

    return DCM_OK;
}

/*===========================================================================
 * SID 0x2E — 按 ID 写入数据 (Write Data By Identifier)
 *===========================================================================*/
static Dcm_StatusType Dcm_HandleWriteDataById(const CanTp_MsgType* msg)
{
    uint16_t did;  /* 数据标识符（Data Identifier），Big-Endian 2字节 */

    if (Dcm_GetMessageLength(msg) < 3u)
    {
        Dcm_SendNegativeResponse(DCM_SID_WRITE_DATA_BY_ID, DCM_NRC_INCORRECT_MSG_LEN);
        return DCM_E_MSG_LEN;
    }

    /* DID 是 Big-Endian 2 字节 */
    did = ((uint16_t)msg->data[1] << 8) | (uint16_t)msg->data[2];

    /*===========================================================================
     * DID 数据写入分发
     *===========================================================================*/
    switch (did)
    {
        /* --- 标定参数 (可写) --- */
        case DCM_DID_CHARGE_OVER_V_THD:
            /* TODO: 校验数据长度 → 校验范围 → 写入 EEPROM/Flash */
            /* TODO: 检查当前会话是否允许写入（编程/扩展 + 安全解锁） */
            Dcm_SendNegativeResponse(DCM_SID_WRITE_DATA_BY_ID, DCM_NRC_CONDITIONS_NOT_CORRECT);
            break;

        case DCM_DID_DISCHARGE_UNDER_V_THD:
            Dcm_SendNegativeResponse(DCM_SID_WRITE_DATA_BY_ID, DCM_NRC_CONDITIONS_NOT_CORRECT);
            break;

        case DCM_DID_CHARGE_OVER_C_THD:
            Dcm_SendNegativeResponse(DCM_SID_WRITE_DATA_BY_ID, DCM_NRC_CONDITIONS_NOT_CORRECT);
            break;

        case DCM_DID_CELL_OV_THD:
            Dcm_SendNegativeResponse(DCM_SID_WRITE_DATA_BY_ID, DCM_NRC_CONDITIONS_NOT_CORRECT);
            break;

        case DCM_DID_CELL_UV_THD:
            Dcm_SendNegativeResponse(DCM_SID_WRITE_DATA_BY_ID, DCM_NRC_CONDITIONS_NOT_CORRECT);
            break;

        case DCM_DID_CELL_OT_THD:
            Dcm_SendNegativeResponse(DCM_SID_WRITE_DATA_BY_ID, DCM_NRC_CONDITIONS_NOT_CORRECT);
            break;

        case DCM_DID_BALANCE_ENABLE_THD:
            Dcm_SendNegativeResponse(DCM_SID_WRITE_DATA_BY_ID, DCM_NRC_CONDITIONS_NOT_CORRECT);
            break;

        default:
            /* 只读 DID 或未定义 DID */
            Dcm_SendNegativeResponse(DCM_SID_WRITE_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;
    }

    return DCM_OK;
}

/*===========================================================================
 * SID 0x27 — 安全访问 (Security Access)  [预留框架]
 *===========================================================================*/
static Dcm_StatusType Dcm_HandleSecurityAccess(const CanTp_MsgType* msg)
{
    /* TODO: 实现 Seed & Key 流程
     * Step 1: Tester 发送 0x27 01 (请求种子)
     *          ECU 回复 0x67 01 [Seed_4bytes]
     * Step 2: Tester 发送 0x27 02 [Key_4bytes]
     *          ECU 验证 Key，回复 0x67 02
     *          验证通过后 Dcm_SecLevel = DCM_SEC_LEVEL_LEVEL1
     */
    (void)msg;
    Dcm_SendNegativeResponse(DCM_SID_SECURITY_ACCESS, DCM_NRC_SUBFUNC_NOT_SUPPORTED);
    return DCM_E_SUBFUNC_NOT_SUPPORTED;
}

/*===========================================================================
 * SID 0x19 — 读取 DTC 信息 (Read DTC Information)  [预留框架]
 *===========================================================================*/
static Dcm_StatusType Dcm_HandleReadDtcInfo(const CanTp_MsgType* msg)
{
    /* TODO: 调用 Dem_GetDtcList() 返回 DTC 列表
     * 子功能:
     *   0x01 - reportNumberOfDTCByStatusMask
     *   0x02 - reportDTCByStatusMask
     *   0x04 - reportDTCSnapshotIdentification
     *   0x06 - reportDTCSnapshotRecordByDTCNumber
     *   0x0A - reportSupportedDTC
     */
    (void)msg;
    Dcm_SendNegativeResponse(DCM_SID_READ_DTC_INFO, DCM_NRC_SUBFUNC_NOT_SUPPORTED);
    return DCM_E_SUBFUNC_NOT_SUPPORTED;
}

/*===========================================================================
 * SID 0x14 — 清除 DTC 信息 (Clear Diagnostic Information)  [预留框架]
 *===========================================================================*/
static Dcm_StatusType Dcm_HandleClearDtcInfo(const CanTp_MsgType* msg)
{
    /* TODO: 调用 Dem_ClearDtcList() 清除 DTC
     * 参数: 3 字节 groupOfDTC (0xFFFFFF = 清除所有)
     */
    (void)msg;
    Dcm_SendNegativeResponse(DCM_SID_CLEAR_DTC_INFO, DCM_NRC_SUBFUNC_NOT_SUPPORTED);
    return DCM_E_SUBFUNC_NOT_SUPPORTED;
}

/*===========================================================================
 * SID 0x31 — 例程控制 (Routine Control)  [预留框架]
 *===========================================================================*/
static Dcm_StatusType Dcm_HandleRoutineControl(const CanTp_MsgType* msg)
{
    /* TODO: 实现自定义例程（如：强制均衡、自检、校准）
     * 子功能:
     *   0x01 - startRoutine
     *   0x02 - stopRoutine
     *   0x03 - requestRoutineResults
     * routineIdentifier: 2 bytes
     */
    (void)msg;
    Dcm_SendNegativeResponse(DCM_SID_ROUTINE_CONTROL, DCM_NRC_SUBFUNC_NOT_SUPPORTED);
    return DCM_E_SUBFUNC_NOT_SUPPORTED;
}
