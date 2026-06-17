/**
 * @file    Dcm.h
 * @brief   Diagnostic Communication Manager (UDS) — 符合 ISO 14229-1
 * 
 * 模块职责：
 *   - 解析 UDS 诊断请求（CAN ID 0x7xx 功能寻址/物理寻址）
 *   - 会话管理（默认/编程/扩展）
 *   - 安全访问控制
 *   - DID 读写分发
 *   - DTC 管理请求转发给 DEM
 * 
 * 所属 AUTOSAR 层: BSW / SERVICES
 */

#ifndef DCM_H
#define DCM_H

#include <stdint.h>
#include <stdbool.h>
#include "CanTp.h"

/*===========================================================================
 * UDS SID 定义 (ISO 14229-1)
 *===========================================================================*/
/* --- 诊断/通信管理 --- */
#define DCM_SID_DIAG_SESSION_CTRL       (0x10u)  /* 诊断会话控制 */
#define DCM_SID_ECU_RESET               (0x11u)  /* ECU 复位 */
#define DCM_SID_SECURITY_ACCESS         (0x27u)  /* 安全访问 */
#define DCM_SID_TESTER_PRESENT          (0x3Eu)  /* 测试仪保持 */

/* --- 数据读写 --- */
#define DCM_SID_READ_DATA_BY_ID         (0x22u)  /* 按 ID 读取数据 */
#define DCM_SID_WRITE_DATA_BY_ID        (0x2Eu)  /* 按 ID 写入数据 */

/* --- DTC 管理 --- */
#define DCM_SID_READ_DTC_INFO           (0x19u)  /* 读取 DTC 信息 */
#define DCM_SID_CLEAR_DTC_INFO          (0x14u)  /* 清除 DTC 信息 */

/* --- 例程控制 --- */
#define DCM_SID_ROUTINE_CONTROL         (0x31u)  /* 例程控制 */

/*===========================================================================
 * UDS 诊断会话类型 (Sub-function of 0x10)
 *===========================================================================*/
typedef enum
{
    DCM_SESSION_DEFAULT       = 0x01u,  /* 默认会话 */
    DCM_SESSION_PROGRAMMING   = 0x02u,  /* 编程会话（刷写） */
    DCM_SESSION_EXTENDED      = 0x03u,  /* 扩展诊断会话 */
    DCM_SESSION_SAFETY        = 0x04u   /* 安全系统诊断会话 */
} Dcm_SessionType;

/*===========================================================================
 * UDS 复位类型 (Sub-function of 0x11)
 *===========================================================================*/
typedef enum
{
    DCM_RESET_HARD             = 0x01u,  /* 硬复位（上电复位） */
    DCM_RESET_KEY_OFF_ON       = 0x02u,  /* 模拟 IG OFF/ON */
    DCM_RESET_SOFT             = 0x03u,  /* 软复位 */
    DCM_RESET_ENABLE_RAPID_PWR = 0x04u,  /* 快速下电 */
    DCM_RESET_DISABLE_RAPID_PWR= 0x05u   /* 禁用快速下电 */
} Dcm_ResetType;

/*===========================================================================
 * UDS 否定响应码 NRC (Negative Response Code)
 *===========================================================================*/
#define DCM_NRC_POSITIVE_RESP_MASK       (0x40u)  /* 肯定响应偏移量 */
#define DCM_NRC_GENERAL_REJECT           (0x10u)  /* 一般拒绝 */
#define DCM_NRC_SERVICE_NOT_SUPPORTED    (0x11u)  /* 服务不支持 */
#define DCM_NRC_SUBFUNC_NOT_SUPPORTED    (0x12u)  /* 子功能不支持 */
#define DCM_NRC_INCORRECT_MSG_LEN        (0x13u)  /* 消息长度错误 */
#define DCM_NRC_CONDITIONS_NOT_CORRECT   (0x22u)  /* 条件不满足 */
#define DCM_NRC_REQUEST_SEQUENCE_ERROR   (0x24u)  /* 请求序列错误 */
#define DCM_NRC_REQUEST_OUT_OF_RANGE     (0x31u)  /* 请求超出范围 */
#define DCM_NRC_SECURITY_ACCESS_DENIED   (0x33u)  /* 安全访问拒绝 */
#define DCM_NRC_INVALID_KEY              (0x35u)  /* 密钥无效 */
#define DCM_NRC_EXCEED_NUM_ATTEMPTS      (0x36u)  /* 超过尝试次数 */
#define DCM_NRC_TIME_DELAY_NOT_EXPIRED   (0x37u)  /* 延迟时间未到 */
#define DCM_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED (0x70u) /* 禁止上传/下载 */

/*===========================================================================
 * 诊断会话超时 (ms)
 *===========================================================================*/
#define DCM_S3_SERVER_TIMEOUT_MS         (5000u)   /* S3 Server 超时（默认会话） */
#define DCM_P2_SERVER_MAX_MS             (50u)     /* P2 Server 最大响应时间 */
#define DCM_P2_STAR_SERVER_MAX_MS        (5000u)   /* P2* Server 扩展响应时间 */

/*===========================================================================
 * 诊断 CAN ID 配置 (物理寻址 / 功能寻址)
 *===========================================================================*/
/* 物理寻址: 诊断仪 ←→ 本 ECU 点对点 */
#define DCM_CANID_PHYS_REQ     ((uint32_t)0x7E0u)  /* 物理请求 ID (Tester→ECU) */
#define DCM_CANID_PHYS_RESP    ((uint32_t)0x7E8u)  /* 物理响应 ID (ECU→Tester) */

/* 功能寻址: 诊断仪 → 整车所有 ECU 广播 */
#define DCM_CANID_FUNC_REQ     ((uint32_t)0x7DFu)  /* 功能请求 ID (Tester→All) */

/*===========================================================================
 * 数据类型
 *===========================================================================*/
/* DCM 消息类型复用 CanTp_MsgType，避免重复定义 */
#define Dcm_MsgType CanTp_MsgType

/** @brief DCM 处理状态 */
typedef enum
{
    DCM_OK                        = 0x00u,
    DCM_E_PARAM                   = 0x01u,
    DCM_E_SID_NOT_SUPPORTED       = 0x02u,
    DCM_E_SUBFUNC_NOT_SUPPORTED   = 0x03u,
    DCM_E_SESSION_ERROR           = 0x04u,
    DCM_E_MSG_LEN                 = 0x05u,
    DCM_E_INTERNAL                = 0xFFu
} Dcm_StatusType;

/** @brief 安全访问级别 */
typedef enum
{
    DCM_SEC_LEVEL_LOCKED    = 0x00u,  /* 锁定（默认） */
    DCM_SEC_LEVEL_LEVEL1    = 0x01u   /* 解锁 Lv1 */
} Dcm_SecLevelType;

/*===========================================================================
 * API 函数声明
 *===========================================================================*/

/**
 * @brief  初始化 DCM 模块（会话回默认、安全锁定）。
 */
void Dcm_Init(void);

/**
 * @brief  主函数，由上层周期性调用以检查会话超时。
 *         周期建议: 10~100ms
 */
void Dcm_MainFunction(void);

/**
 * @brief  处理接收到的诊断 CAN 消息。
 * @param  msg: 指向接收消息的指针（CAN ID + 8 bytes + DLC）
 * @retval DCM_OK 处理成功；其它为错误。
 * 
 * @note   调用者需根据 CAN ID 区分物理/功能寻址后传入。
 *         功能寻址请求不发送否定响应。
 */
Dcm_StatusType Dcm_ProcessRequest(const CanTp_MsgType* msg);

/**
 * @brief  获取诊断 CAN 响应帧（用于 CanIf 发送）。
 * @param  respMsg: 输出响应消息指针。
 * @retval true:  有待发送的响应帧。
 * @retval false: 无待发送响应。
 */
bool Dcm_GetResponse(CanTp_MsgType* respMsg);

/**
 * @brief  确认响应已成功发送，释放发送锁。
 */
void Dcm_TxConfirmation(void);

/**
 * @brief  查询 ECU 是否有待执行的复位请求。
 * @retval true:  需要执行复位。
 * @retval false: 无待处理复位。
 * 
 * @note   DCM 在收到 0x11 复位请求时不会立即复位，
 *         而是置位此标志，等待 RTE 将响应帧发出后再执行复位。
 */
bool Dcm_IsEcuResetPending(void);

#endif /* DCM_H */
