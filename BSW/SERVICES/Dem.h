/**
 * @file    Dem.h
 * @brief   Diagnostic Event Manager — DTC 故障码管理 (ISO 14229-1)
 * 
 * 模块职责：
 *   - DTC 存储与状态位管理（testFailed/pending/confirmed）
 *   - 快照数据 (Snapshot) 与扩展数据 (Extended Data)
 *   - 故障去抖 (Debounce) 机制
 *   - 供 DCM 模块查询和清除 DTC
 * 
 * 所属 AUTOSAR 层: BSW / SERVICES
 * 
 * 注意：本文件为框架占位，DTC 存储逻辑待实现。
 */

#ifndef DEM_H
#define DEM_H

#include <stdint.h>
#include <stdbool.h>

/*===========================================================================
 * DTC 状态位 (ISO 14229-1 Annex D)
 *===========================================================================*/
typedef struct
{
    uint8_t testFailed            : 1;  /* bit0: 当前循环检测到故障 */
    uint8_t testFailedThisOpCycle : 1;  /* bit1: 本次操作循环检测到故障 */
    uint8_t pendingDTC            : 1;  /* bit2: 待确认 DTC */
    uint8_t confirmedDTC          : 1;  /* bit3: 已确认 DTC */
    uint8_t testNotCompleted      : 1;  /* bit4: 自上次清除后未完成测试 */
    uint8_t testFailedSinceClear  : 1;  /* bit5: 自上次清除后检测到故障 */
    uint8_t testIncomplete        : 1;  /* bit6: 测试未完成 */
    uint8_t warningIndicator      : 1;  /* bit7: 警告指示灯请求 */
} Dem_DtcStatusType;

/*===========================================================================
 * DTC 格式 (3-byte DTC 编码)
 *   HighByte: 类别 (00=Powertrain, C0=Chassis, U0=Network...)
 *   MidByte + LowByte: 故障码编号
 *===========================================================================*/
typedef struct
{
    uint8_t dtcHighByte;
    uint8_t dtcMidByte;
    uint8_t dtcLowByte;
} Dem_DtcType;

/*===========================================================================
 * DTC 记录条目
 *===========================================================================*/
typedef struct
{
    Dem_DtcType       dtcCode;       /* 3 字节 DTC 码 */
    Dem_DtcStatusType status;        /* DTC 状态位 */
    uint32_t          occurrenceCnt; /* 发生次数 */
    uint32_t          timestamp;     /* 首帧时间戳 (ms) */
    /* TODO: 快照数据 (Snapshot Record) */
    /* TODO: 扩展数据 (Extended Data Record) */
} Dem_DtcRecordType;

/*===========================================================================
 * DEM 初始化和主函数大小
 *===========================================================================*/
#define DEM_MAX_DTC_RECORD   (32u)

/*===========================================================================
 * API 声明
 *===========================================================================*/

/**
 * @brief  初始化 DEM 模块（清空 DTC 表）。
 */
void Dem_Init(void);

/**
 * @brief  DEM 主函数，周期性调用以处理去抖等逻辑。
 */
void Dem_MainFunction(void);

/**
 * @brief  设置一个 DTC 的故障状态。
 * @param  dtc:      DTC 编码
 * @param  failed:   true = 检测到故障, false = 故障恢复
 * 
 * @note   内部会进行去抖计数，符合条件时更新 confirmedDTC。
 */
void Dem_SetDtcStatus(const Dem_DtcType* dtc, bool failed);

/**
 * @brief  获取 DTC 总数（按状态掩码过滤）。
 * @param  statusMask: 状态掩码 (如 0x08 = 只统计 confirmed)
 * @retval 符合条件的 DTC 数量。
 */
uint8_t Dem_GetNumberOfDtcByStatusMask(uint8_t statusMask);

/**
 * @brief  获取 DTC 列表。
 * @param  statusMask: 状态掩码
 * @param  dtcList:    输出 DTC 列表指针
 * @param  maxCount:   列表最大容量
 * @retval 实际返回的 DTC 数量。
 */
uint8_t Dem_GetDtcByStatusMask(uint8_t statusMask, Dem_DtcRecordType* dtcList, uint8_t maxCount);

/**
 * @brief  清除所有 DTC 记录。通常在 0x14 服务处理后调用。
 */
void Dem_ClearAllDtc(void);

/**
 * @brief  获取指定 DTC 的快照数据。
 */
/* TODO: Dem_GetSnapshotByDtc() */

#endif /* DEM_H */
