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

/*
 * AUTOSAR 分层说明:
 *   DCM 属于 BSW/Services 层，通过 RTE 接口读取应用层 (ASW) 数据。
 *   DCM 不直接引用 BMS 应用层头文件，仅依赖 Rte.h 中声明的 Rte_Read_* API。
 *   数据流向: BMS-App → Rte_Write → RTE缓冲区 → Rte_Read → DCM
 */
#include "Rte.h"

/*===========================================================================
 * 内部状态变量
 *===========================================================================*/
static Dcm_SessionType  Dcm_ActiveSession     = DCM_SESSION_DEFAULT;   /* 当前诊断会话类型（默认/编程/扩展/安全） */
static Dcm_SecLevelType Dcm_SecLevel          = DCM_SEC_LEVEL_LOCKED;  /* 当前安全访问等级（锁定/已解锁） */
static uint32_t         Dcm_TesterPresentTimer = 0u;                   /* TesterPresent 超时计数器（单位：ms） */
static bool             Dcm_TesterPresent_Seen = false;                 /* 标记本周期是否收到重置计时的 TesterPresent 请求 */

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
static void     Dcm_SessionTimeoutCheck(uint32_t dt);

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

/* --- 会话/安全检查辅助函数 --- */
static bool Dcm_IsSessionAllowed(Dcm_SessionType minSession);
static bool Dcm_IsSecurityUnlocked(void);

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

void Dcm_MainFunction(uint32_t dt)
{
    Dcm_SessionTimeoutCheck(dt);
    //...后续可添加DTC状态，P2超时等
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
     * suppressPosRspMsgIndicationBit 检查
     * ISO 14229-1: 只有"带子功能的服务"才检查 byte[1] bit7。
     * 带子功能的 SID: 0x10, 0x11, 0x19, 0x27, 0x31, 0x3E
     * 不带子功能的 SID: 0x14(ClearDTC), 0x22(ReadDataById), 0x2E(WriteDataById)
     *   — byte[1] 是参数(如 DID 高字节)，不是子功能，不能检查 bit7。
     */
    Dcm_SuppressPositiveResp = false;
    if (msg->length >= 2u)
    {
        switch (sid)
        {
            case DCM_SID_DIAG_SESSION_CTRL:  /* 0x10 */
            case DCM_SID_ECU_RESET:          /* 0x11 */
            case DCM_SID_READ_DTC_INFO:      /* 0x19 */
            case DCM_SID_SECURITY_ACCESS:    /* 0x27 */
            case DCM_SID_ROUTINE_CONTROL:    /* 0x31 */
            case DCM_SID_TESTER_PRESENT:     /* 0x3E */
                if ((msg->data[1] & 0x80u) != 0u)
                {
                    Dcm_SuppressPositiveResp = true;
                }
                break;
            default:
                break;
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

/**
 * @brief  获取当前活跃的诊断会话类型。
 */
Dcm_SessionType Dcm_GetActiveSession(void)
{
    return Dcm_ActiveSession;
}

/**
 * @brief  获取当前安全访问等级。
 */
Dcm_SecLevelType Dcm_GetSecurityLevel(void)
{
    return Dcm_SecLevel;
}

/*===========================================================================
 * 会话/安全检查辅助函数
 *===========================================================================*/

/**
 * @brief  检查当前会话是否满足最低会话要求。
 * @param  minSession: 允许执行此服务的最低会话类型
 * @retval true:  当前会话满足要求
 * @retval false: 当前会话不满足要求
 *
 * 会话等级: DEFAULT(1) < PROGRAMMING(2) < EXTENDED(3) < SAFETY(4)
 * 例如: minSession=EXTENDED 表示只有 EXTENDED 和 SAFETY 会话允许。
 */
static bool Dcm_IsSessionAllowed(Dcm_SessionType minSession)
{
    return (Dcm_ActiveSession >= minSession);
}

/**
 * @brief  检查当前安全等级是否已解锁。
 * @retval true:  已解锁
 * @retval false: 锁定
 */
static bool Dcm_IsSecurityUnlocked(void)
{
    return (Dcm_SecLevel != DCM_SEC_LEVEL_LOCKED);
}

/*===========================================================================
 * 会话超时管理
 *===========================================================================*/

static void Dcm_SessionTimeoutCheck(uint32_t dt)
{
    /* 默认会话不需要超时回退 */
    if (Dcm_ActiveSession == DCM_SESSION_DEFAULT)
    {
        return;
    }

    /* S3 Server 超时: 若在非默认会话且在 S3 时间内未收到 TesterPresent，回退默认会话 */
    if (Dcm_TesterPresent_Seen)
    {
        Dcm_TesterPresentTimer = 0u;
        Dcm_TesterPresent_Seen = false;
    }
    else
    {
        Dcm_TesterPresentTimer += dt;
        if (Dcm_TesterPresentTimer >= DCM_S3_SERVER_TIMEOUT_MS)
        {
            Dcm_ActiveSession      = DCM_SESSION_DEFAULT;
            Dcm_SecLevel           = DCM_SEC_LEVEL_LOCKED;
            Dcm_TesterPresentTimer = 0u;
        }
    }
}

/*===========================================================================
 * DID 数据访问辅助函数 (Dcm_DataAccess)
 *
 * AUTOSAR 架构说明:
 *   DCM 模块通过 SchM (调度管理器) 或 Rte_Read 接口获取应用层数据。
 *   本项目 RTE 层较薄，此处封装物理量→UDS 编码的转换逻辑，
 *   将 BMS 原始浮点数据转换为 ISO 14229-1 定义的整数编码。
 *
 * 信号编码规则 (与 CAN 信号 DBC 保持一致):
 *   - 电压类:  uint16, 分辨率 0.1V,   物理值 = raw × 0.1 (V)
 *   - 电流类:   int16, 分辨率 0.1A,   物理值 = raw × 0.1 (A)
 *   - SOC/SOH:  uint8, 分辨率 0.5%,   物理值 = raw × 0.5 (%)
 *   - 单体电压: uint16, 分辨率 0.001V, 物理值 = raw × 0.001 (V)
 *   - 温度类:    int8, 分辨率 1°C,    偏移量 40°C (raw = T + 40)
 *===========================================================================*/

/** @brief float → uint16 缩放转换（四舍五入，带上下限饱和） */
static uint16_t Dcm_ScaleToUint16(float value, float resolution)
{
    int32_t raw;
    if (resolution <= 0.0f) return 0u;
    raw = (int32_t)(value / resolution + 0.5f);
    if (raw < 0)        return 0u;
    if (raw > (int32_t)0xFFFF) return 0xFFFFu;
    return (uint16_t)raw;
}

/** @brief float → int16 缩放转换（四舍五入，带上下限饱和） */
static int16_t Dcm_ScaleToInt16(float value, float resolution)
{
    int32_t raw;
    if (resolution <= 0.0f) return 0;
    raw = (int32_t)(value / resolution + (value >= 0.0f ? 0.5f : -0.5f));
    if (raw < (int32_t)-32768) return (int16_t)-32768;
    if (raw > (int32_t)32767)  return (int16_t)32767;
    return (int16_t)raw;
}

/** @brief float → uint8 缩放转换（四舍五入，带上下限饱和） */
static uint8_t Dcm_ScaleToUint8(float value, float resolution)
{
    int32_t raw;
    if (resolution <= 0.0f) return 0u;
    raw = (int32_t)(value / resolution + 0.5f);
    if (raw < 0)   return 0u;
    if (raw > 255) return 255u;
    return (uint8_t)raw;
}

/**
 * @brief  读取电池总电压 DID 0x2001
 * @param  data: 输出缓冲区（2字节，Big-Endian）
 * @param  len:  输出数据长度
 */
static void Dcm_ReadBatteryVoltage(uint8_t* data, uint8_t* len)
{
    float voltage;
    uint16_t raw;
    (void)Rte_Read_Dcm_BatteryVoltage(&voltage);
    raw = Dcm_ScaleToUint16(voltage, 0.1f);
    data[0] = (uint8_t)((raw >> 8) & 0xFFu);  /* MSB */
    data[1] = (uint8_t)(raw & 0xFFu);          /* LSB */
    *len = 2u;
}

/**
 * @brief  读取 SOC DID 0x2003
 * @param  data: 输出缓冲区（1字节）
 * @param  len:  输出数据长度
 *
 * BMS_AnalysisData.SOC 范围 0.0~1.0，编码为 0~1000（分辨率 0.1%）
 */
static void Dcm_ReadSOC(uint8_t* data, uint8_t* len)
{
    float soc;
    float soc_pct;
    uint16_t raw;
    (void)Rte_Read_Dcm_SOC(&soc);
    soc_pct = soc * 100.0f;  /* 0.0~1.0 → 0~100% */
    raw = Dcm_ScaleToUint16(soc_pct, 0.1f);
    data[0] = (uint8_t)((raw >> 8) & 0xFFu);  /* MSB */
    data[1] = (uint8_t)(raw & 0xFFu);          /* LSB */
    *len = 2u;
}

/**
 * @brief  读取最高单体电压 DID 0x2101
 * @param  data: 输出缓冲区（2字节，Big-Endian）
 * @param  len:  输出数据长度
 */
static void Dcm_ReadMaxCellVoltage(uint8_t* data, uint8_t* len)
{
    float vmax;
    uint16_t raw;
    (void)Rte_Read_Dcm_CellVoltMax(&vmax);
    raw = Dcm_ScaleToUint16(vmax, 0.001f);
    data[0] = (uint8_t)((raw >> 8) & 0xFFu);
    data[1] = (uint8_t)(raw & 0xFFu);
    *len = 2u;
}

/**
 * @brief  读取最低单体电压 DID 0x2102
 * @param  data: 输出缓冲区（2字节，Big-Endian）
 * @param  len:  输出数据长度
 */
static void Dcm_ReadMinCellVoltage(uint8_t* data, uint8_t* len)
{
    float vmin;
    uint16_t raw;
    (void)Rte_Read_Dcm_CellVoltMin(&vmin);
    raw = Dcm_ScaleToUint16(vmin, 0.001f);
    data[0] = (uint8_t)((raw >> 8) & 0xFFu);
    data[1] = (uint8_t)(raw & 0xFFu);
    *len = 2u;
}

/**
 * @brief  读取最高单体温度 DID 0x2103
 * @param  data: 输出缓冲区（1字节，偏移编码）
 * @param  len:  输出数据长度
 *
 * CellTemp[] 已从小到大排序，CellTemp[N-1] 为最高温度。
 * 编码: raw = 温度(°C) + 40，范围 -40~+215°C → 0~255。
 */
static void Dcm_ReadMaxCellTemp(uint8_t* data, uint8_t* len)
{
    float temp_c;
    int16_t raw;
    (void)Rte_Read_Dcm_CellTempMax(&temp_c);
    raw = (int16_t)(temp_c + 40.0f + 0.5f);
    if (raw < 0)   raw = 0;
    if (raw > 255) raw = 255;
    data[0] = (uint8_t)raw;
    *len = 1u;
}

/**
 * @brief  读取最低单体温度 DID 0x2104
 * @param  data: 输出缓冲区（1字节，偏移编码）
 * @param  len:  输出数据长度
 *
 * CellTemp[0] 为最低温度（已排序）。
 * 编码: raw = 温度(°C) + 40，范围 -40~+215°C → 0~255。
 */
static void Dcm_ReadMinCellTemp(uint8_t* data, uint8_t* len)
{
    float temp_c;
    int16_t raw;
    (void)Rte_Read_Dcm_CellTempMin(&temp_c);
    raw = (int16_t)(temp_c + 40.0f + 0.5f);
    if (raw < 0)   raw = 0;
    if (raw > 255) raw = 255;
    data[0] = (uint8_t)raw;
    *len = 1u;
}

/**
 * @brief  读取 BMS 运行状态 DID 0x3001
 * @param  data: 输出缓冲区（1字节，位域编码）
 * @param  len:  输出数据长度
 *
 * 位域定义:
 *   bit[2:0] — 系统模式 (0=NULL, 1=充电, 2=放电, 3=待机, 4=睡眠)
 *   bit[3]   — 充电使能 (1=允许)
 *   bit[4]   — 放电使能 (1=允许)
 *   bit[5]   — 均衡状态 (1=均衡中)
 *   bit[7:6] — 保留
 */
static void Dcm_ReadBmsStatus(uint8_t* data, uint8_t* len)
{
    uint8_t sysMode;
    bool chargeEn, dischargeEn, balanceAct;
    uint8_t status = 0u;

    (void)Rte_Read_Dcm_SysMode(&sysMode);
    (void)Rte_Read_Dcm_ChargeEnabled(&chargeEn);
    (void)Rte_Read_Dcm_DischargeEnabled(&dischargeEn);
    (void)Rte_Read_Dcm_BalanceActive(&balanceAct);

    status |= (sysMode & 0x07u);                          /* bit[2:0]: 系统模式 (3位, 可表示0~4) */
    if (chargeEn)    status |= 0x08u;                      /* bit3: 充电使能 */
    if (dischargeEn) status |= 0x10u;                      /* bit4: 放电使能 */
    if (balanceAct)  status |= 0x20u;                      /* bit5: 均衡中 */
    data[0] = status;
    *len = 1u;
}

/**
 * @brief  读取保护标志位 DID 0x3004
 * @param  data: 输出缓冲区（2字节，Big-Endian 位掩码）
 * @param  len:  输出数据长度
 *
 * 位定义与 BMS_ProtectAlertTypedef 枚举一致:
 *   bit0:  充电过压保护    bit1:  充电过流保护
 *   bit2:  充电过温保护    bit3:  充电低温保护
 *   bit4:  放电欠压保护    bit5:  放电过流保护
 *   bit6:  放电短路保护    bit7:  放电过温保护
 *   bit8:  放电低温保护    bit15~9: 保留
 */
static void Dcm_ReadProtectFlag(uint8_t* data, uint8_t* len)
{
    uint16_t flags;
    (void)Rte_Read_Dcm_ProtectAlertFlags(&flags);
    data[0] = (uint8_t)((flags >> 8) & 0xFFu);  /* MSB */
    data[1] = (uint8_t)(flags & 0xFFu);          /* LSB */
    *len = 2u;
}

/**
 * @brief  读取当前故障码 DID 0x3005
 * @param  data: 输出缓冲区（2字节，Big-Endian）
 * @param  len:  输出数据长度
 *
 * 将 BMS_Protect.alert 保护标志位映射为标准化 DTC 故障码。
 * 若多个故障同时存在，返回最高优先级的故障码。
 * 优先级: 短路 > 过流 > 过压 > 欠压 > 过温 > 低温。
 *
 * DTC 编码映射表:
 *   0x0101 — 充电过压    0x0102 — 放电欠压
 *   0x0201 — 充电过流    0x0202 — 放电过流    0x0203 — 放电短路
 *   0x0301 — 充电过温    0x0302 — 放电过温
 *   0x0303 — 充电低温    0x0304 — 放电低温
 *   0x0000 — 无故障
 */
static void Dcm_ReadFaultCode(uint8_t* data, uint8_t* len)
{
    uint16_t alert;
    uint16_t dtc = 0x0000u;
    (void)Rte_Read_Dcm_ProtectAlertFlags(&alert);

    /* 按优先级映射（高优先级先检查，覆盖低优先级） */
    if (alert & 0x0040u)      dtc = 0x0203u;  /* FlAG_ALERT_SCD  放电短路 */
    else if (alert & 0x0020u) dtc = 0x0202u;  /* FlAG_ALERT_OCD  放电过流 */
    else if (alert & 0x0002u) dtc = 0x0201u;  /* FlAG_ALERT_OCC  充电过流 */
    else if (alert & 0x0001u) dtc = 0x0101u;  /* FlAG_ALERT_OV   充电过压 */
    else if (alert & 0x0010u) dtc = 0x0102u;  /* FlAG_ALERT_UV   放电欠压 */
    else if (alert & 0x0080u) dtc = 0x0302u;  /* FlAG_ALERT_OTD  放电过温 */
    else if (alert & 0x0004u) dtc = 0x0301u;  /* FlAG_ALERT_OTC  充电过温 */
    else if (alert & 0x0100u) dtc = 0x0304u;  /* FlAG_ALERT_LTD  放电低温 */
    else if (alert & 0x0008u) dtc = 0x0303u;  /* FlAG_ALERT_LTC  充电低温 */

    data[0] = (uint8_t)((dtc >> 8) & 0xFFu);  /* DTC HighByte */
    data[1] = (uint8_t)(dtc & 0xFFu);          /* DTC LowByte */
    *len = 2u;
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

    /*===========================================================================
     * 会话前置检查 — ECU Reset 仅在非默认会话下允许
     *===========================================================================*/
    if (!Dcm_IsSessionAllowed(DCM_SESSION_EXTENDED))
    {
        Dcm_SendNegativeResponse(DCM_SID_ECU_RESET,
                                 DCM_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION);
        return DCM_E_SESSION_ERROR;
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
    uint8_t subFunc;         /* 子功能码（bit0~6） */
    bool suppressPosRsp;     /* bit7: 是否抑制正响应 */

    if (Dcm_GetMessageLength(msg) < 2u)
    {
        Dcm_SendNegativeResponse(DCM_SID_TESTER_PRESENT, DCM_NRC_INCORRECT_MSG_LEN);
        return DCM_E_MSG_LEN;
    }

    suppressPosRsp = (msg->data[1] & 0x80u) != 0u;  /* 提取 bit7 抑制标志 */
    subFunc = msg->data[1] & 0x7Fu;                  /* 提取 bit0~6 子功能值 */

    /* 子功能: 0x00=需要响应, 0x80=抑制响应(bit7=1, 实际子功能仍为0x00) */
    if (subFunc != 0x00u)
    {
        Dcm_SendNegativeResponse(DCM_SID_TESTER_PRESENT, DCM_NRC_SUBFUNC_NOT_SUPPORTED);
        return DCM_E_SUBFUNC_NOT_SUPPORTED;
    }

    /* 刷新 S3 超时计时器（无论是否抑制响应都要执行，保持非默认会话） */
    Dcm_TesterPresent_Seen = true;

    /* 仅当未抑制正响应时才发送肯定响应 */
    if (!suppressPosRsp)
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

        /* --- ISO 14229-1 标准 DID --- */
        case DCM_DID_ACTIVE_SESSION:
        {
            /* 0xF184 — 当前活跃诊断会话
             * 响应: [SID+0x40] [DID_H] [DID_L] [SessionType]
             * 编码: uint8, 0x01=默认, 0x02=编程, 0x03=扩展, 0x04=安全 */
            uint8_t respData[3];
            respData[0] = (uint8_t)((DCM_DID_ACTIVE_SESSION >> 8) & 0xFFu);
            respData[1] = (uint8_t)(DCM_DID_ACTIVE_SESSION & 0xFFu);
            respData[2] = (uint8_t)Dcm_ActiveSession;  /* 当前会话类型 */
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 3u);
            break;
        }

        /* --- BMS 实时数据 --- */
        case DCM_DID_BATTERY_VOLTAGE:
        {
            /* 响应: [SID+0x40] [DID_H] [DID_L] [VoltMSB] [VoltLSB]
             * 编码: uint16, 分辨率 0.1V, 范围 0~6553.5V */
            uint8_t respData[4];  /* DID(2B) + 电压数据(2B) */
            uint8_t dataLen;
            respData[0] = (uint8_t)((DCM_DID_BATTERY_VOLTAGE >> 8) & 0xFFu); /* DID MSB */
            respData[1] = (uint8_t)(DCM_DID_BATTERY_VOLTAGE & 0xFFu);        /* DID LSB */
            Dcm_ReadBatteryVoltage(&respData[2], &dataLen);
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 2u + dataLen);
            break;
        }

        case DCM_DID_BATTERY_CURRENT:
        {
            /* 响应: [SID+0x40] [DID_H] [DID_L] [CurrMSB] [CurrLSB]
             * 编码: int16, 分辨率 0.1A, 偏移 0, 充电为正/放电为负 */
            uint8_t respData[4];  /* DID(2B) + 电流数据(2B) */
            float current;
            int16_t raw;
            (void)Rte_Read_Dcm_BatteryCurrent(&current);
            raw = Dcm_ScaleToInt16(current, 0.1f);
            respData[0] = (uint8_t)((DCM_DID_BATTERY_CURRENT >> 8) & 0xFFu);
            respData[1] = (uint8_t)(DCM_DID_BATTERY_CURRENT & 0xFFu);
            respData[2] = (uint8_t)((raw >> 8) & 0xFFu);  /* MSB */
            respData[3] = (uint8_t)(raw & 0xFFu);          /* LSB */
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 4u);
            break;
        }

        case DCM_DID_BATTERY_SOC:
        {
            /* 响应: [SID+0x40] [DID_H] [DID_L] [SOC_MSB] [SOC_LSB]
             * 编码: uint16, 分辨率 0.1%, 范围 0~100% (raw 0~1000) */
            uint8_t respData[4];  /* DID(2B) + SOC数据(2B) */
            uint8_t dataLen;
            respData[0] = (uint8_t)((DCM_DID_BATTERY_SOC >> 8) & 0xFFu);
            respData[1] = (uint8_t)(DCM_DID_BATTERY_SOC & 0xFFu);
            Dcm_ReadSOC(&respData[2], &dataLen);
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 2u + dataLen);
            break;
        }

        case DCM_DID_BATTERY_SOH:
            /* TODO: BMS_AnalysisData.SOH 尚未实现，暂返回 0x31 */
            Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            break;

        case DCM_DID_MAX_CELL_VOLTAGE:
        {
            /* 响应: [SID+0x40] [DID_H] [DID_L] [VoltMSB] [VoltLSB]
             * 编码: uint16, 分辨率 0.001V, 范围 0~65.535V */
            uint8_t respData[4];
            uint8_t dataLen;
            respData[0] = (uint8_t)((DCM_DID_MAX_CELL_VOLTAGE >> 8) & 0xFFu);
            respData[1] = (uint8_t)(DCM_DID_MAX_CELL_VOLTAGE & 0xFFu);
            Dcm_ReadMaxCellVoltage(&respData[2], &dataLen);
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 2u + dataLen);
            break;
        }

        case DCM_DID_MIN_CELL_VOLTAGE:
        {
            /* 响应: [SID+0x40] [DID_H] [DID_L] [VoltMSB] [VoltLSB]
             * 编码: uint16, 分辨率 0.001V, 范围 0~65.535V */
            uint8_t respData[4];
            uint8_t dataLen;
            respData[0] = (uint8_t)((DCM_DID_MIN_CELL_VOLTAGE >> 8) & 0xFFu);
            respData[1] = (uint8_t)(DCM_DID_MIN_CELL_VOLTAGE & 0xFFu);
            Dcm_ReadMinCellVoltage(&respData[2], &dataLen);
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 2u + dataLen);
            break;
        }

        case DCM_DID_MAX_CELL_TEMP:
        {
            /* 响应: [SID+0x40] [DID_H] [DID_L] [Temp]
             * 编码: uint8, 偏移 40°C, raw = T+40, 范围 -40~+215°C */
            uint8_t respData[3];
            uint8_t dataLen;
            respData[0] = (uint8_t)((DCM_DID_MAX_CELL_TEMP >> 8) & 0xFFu);
            respData[1] = (uint8_t)(DCM_DID_MAX_CELL_TEMP & 0xFFu);
            Dcm_ReadMaxCellTemp(&respData[2], &dataLen);
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 2u + dataLen);
            break;
        }

        case DCM_DID_MIN_CELL_TEMP:
        {
            /* 响应: [SID+0x40] [DID_H] [DID_L] [Temp]
             * 编码: uint8, 偏移 40°C, raw = T+40, 范围 -40~+215°C */
            uint8_t respData[3];
            uint8_t dataLen;
            respData[0] = (uint8_t)((DCM_DID_MIN_CELL_TEMP >> 8) & 0xFFu);
            respData[1] = (uint8_t)(DCM_DID_MIN_CELL_TEMP & 0xFFu);
            Dcm_ReadMinCellTemp(&respData[2], &dataLen);
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 2u + dataLen);
            break;
        }

        /* --- BMS 状态 --- */
        case DCM_DID_BMS_STATUS:
        {
            /* 响应: [SID+0x40] [DID_H] [DID_L] [Status]
             * 位域: bit[2:0]=SysMode, bit3=充电, bit4=放电, bit5=均衡 */
            uint8_t respData[3];
            uint8_t dataLen;
            respData[0] = (uint8_t)((DCM_DID_BMS_STATUS >> 8) & 0xFFu);
            respData[1] = (uint8_t)(DCM_DID_BMS_STATUS & 0xFFu);
            Dcm_ReadBmsStatus(&respData[2], &dataLen);
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 2u + dataLen);
            break;
        }

        case DCM_DID_CHARGE_STATUS:
        {
            /* 响应: [SID+0x40] [DID_H] [DID_L] [ChargeState]
             * 0x00=禁用, 0x01=充电中, 其他=保留 */
            bool chargeEn;
            uint8_t respData[3];
            (void)Rte_Read_Dcm_ChargeEnabled(&chargeEn);
            respData[0] = (uint8_t)((DCM_DID_CHARGE_STATUS >> 8) & 0xFFu);
            respData[1] = (uint8_t)(DCM_DID_CHARGE_STATUS & 0xFFu);
            respData[2] = chargeEn ? 0x01u : 0x00u;
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 3u);
            break;
        }

        case DCM_DID_BALANCE_STATUS:
        {
            /* 响应: [SID+0x40] [DID_H] [DID_L] [BalanceState]
             * 0x00=未均衡, 0x01=均衡中 */
            bool balanceAct;
            uint8_t respData[3];
            (void)Rte_Read_Dcm_BalanceActive(&balanceAct);
            respData[0] = (uint8_t)((DCM_DID_BALANCE_STATUS >> 8) & 0xFFu);
            respData[1] = (uint8_t)(DCM_DID_BALANCE_STATUS & 0xFFu);
            respData[2] = balanceAct ? 0x01u : 0x00u;
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 3u);
            break;
        }

        case DCM_DID_PROTECT_FLAG:
        {
            /* 响应: [SID+0x40] [DID_H] [DID_L] [FlagMSB] [FlagLSB]
             * 2字节保护标志位掩码，定义见 BMS_ProtectAlertTypedef */
            uint8_t respData[4];
            uint8_t dataLen;
            respData[0] = (uint8_t)((DCM_DID_PROTECT_FLAG >> 8) & 0xFFu);
            respData[1] = (uint8_t)(DCM_DID_PROTECT_FLAG & 0xFFu);
            Dcm_ReadProtectFlag(&respData[2], &dataLen);
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 2u + dataLen);
            break;
        }

        case DCM_DID_FAULT_CODE:
        {
            /* 响应: [SID+0x40] [DID_H] [DID_L] [DtcHigh] [DtcLow]
             * 标准化 DTC 编码，优先级: 短路>过流>过压>欠压>过温>低温 */
            uint8_t respData[4];
            uint8_t dataLen;
            respData[0] = (uint8_t)((DCM_DID_FAULT_CODE >> 8) & 0xFFu);
            respData[1] = (uint8_t)(DCM_DID_FAULT_CODE & 0xFFu);
            Dcm_ReadFaultCode(&respData[2], &dataLen);
            Dcm_SendPositiveResponse(DCM_SID_READ_DATA_BY_ID, respData, 2u + dataLen);
            break;
        }

        /* --- 标定参数 (可读写, 需要扩展会话) --- */
        case DCM_DID_CHARGE_OVER_V_THD:
        case DCM_DID_DISCHARGE_UNDER_V_THD:
        case DCM_DID_CHARGE_OVER_C_THD:
        case DCM_DID_CELL_OV_THD:
        case DCM_DID_CELL_UV_THD:
        case DCM_DID_CELL_OT_THD:
        case DCM_DID_BALANCE_ENABLE_THD:
            /* 标定参数仅在扩展会话及以上可读 */
            if (!Dcm_IsSessionAllowed(DCM_SESSION_EXTENDED))
            {
                Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID,
                                         DCM_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION);
            }
            else
            {
                /* TODO: 从 EEPROM/Flash 读取标定参数值 */
                Dcm_SendNegativeResponse(DCM_SID_READ_DATA_BY_ID, DCM_NRC_REQUEST_OUT_OF_RANGE);
            }
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

    /*===========================================================================
     * 会话+安全前置检查（写入标定参数必须在扩展会话+安全解锁）
     *===========================================================================*/
    /* Step 1: 检查会话 — 标定参数写入仅在扩展会话及以上允许 */
    if (!Dcm_IsSessionAllowed(DCM_SESSION_EXTENDED))
    {
        Dcm_SendNegativeResponse(DCM_SID_WRITE_DATA_BY_ID,
                                 DCM_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION);
        return DCM_E_SESSION_ERROR;
    }

    /* Step 2: 检查安全等级 — 写入标定参数需要安全解锁 */
    if (!Dcm_IsSecurityUnlocked())
    {
        Dcm_SendNegativeResponse(DCM_SID_WRITE_DATA_BY_ID,
                                 DCM_NRC_SECURITY_ACCESS_DENIED);
        return DCM_E_SESSION_ERROR;
    }

    /* DID 是 Big-Endian 2 字节 */
    did = ((uint16_t)msg->data[1] << 8) | (uint16_t)msg->data[2];

    /*===========================================================================
     * DID 数据写入分发
     *===========================================================================*/
    switch (did)
    {
        /* --- 标定参数 (可写, 入口已检查扩展会话+安全解锁) --- */
        case DCM_DID_CHARGE_OVER_V_THD:
            /* TODO: 校验数据长度 → 校验范围 → 写入 EEPROM/Flash */
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
    /*===========================================================================
     * 会话前置检查 — Security Access 仅在非默认会话下允许
     *===========================================================================*/
    if (!Dcm_IsSessionAllowed(DCM_SESSION_EXTENDED))
    {
        Dcm_SendNegativeResponse(DCM_SID_SECURITY_ACCESS,
                                 DCM_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION);
        return DCM_E_SESSION_ERROR;
    }

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
    /*===========================================================================
     * 会话前置检查 — ClearDTC 仅在非默认会话下允许
     *===========================================================================*/
    if (!Dcm_IsSessionAllowed(DCM_SESSION_EXTENDED))
    {
        Dcm_SendNegativeResponse(DCM_SID_CLEAR_DTC_INFO,
                                 DCM_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION);
        return DCM_E_SESSION_ERROR;
    }

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
    /*===========================================================================
     * 会话前置检查 — Routine Control 仅在非默认会话下允许
     *===========================================================================*/
    if (!Dcm_IsSessionAllowed(DCM_SESSION_EXTENDED))
    {
        Dcm_SendNegativeResponse(DCM_SID_ROUTINE_CONTROL,
                                 DCM_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION);
        return DCM_E_SESSION_ERROR;
    }

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
