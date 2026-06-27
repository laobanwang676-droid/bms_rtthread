/**
 * @file    Dcm_Cfg.h
 * @brief   DCM 模块静态配置 — DID 表、DTC 表、安全参数
 * 
 * 本文件由系统集成工程师根据实际需求配置。
 */

#ifndef DCM_CFG_H
#define DCM_CFG_H

#include "Dcm.h"

/*===========================================================================
 * 全局开关
 *===========================================================================*/
#define DCM_CFG_FUNC_ADDR_SUPPORT     (STD_ON)   /* 是否支持功能寻址 */
#define DCM_CFG_SECURITY_SUPPORT      (STD_ON)   /* 是否支持安全访问 0x27 */

/*===========================================================================
 * 诊断 CAN ID (已在 Dcm.h 中声明，此处供外部引用)
 *===========================================================================*/
#ifndef STD_ON
#define STD_ON  1u
#endif
#ifndef STD_OFF
#define STD_OFF 0u
#endif

/*===========================================================================
 * DID (Data Identifier) 定义表
 * 结构: 2 字节 DID 编号 → 数据指针/回读函数
 *===========================================================================*/

/* --- DID 编号定义（根据 BMS 应用自定义） --- */
/* 系统信息 */
#define DCM_DID_SYSTEM_SUPPLIER_ID       (0xF100u)  /* 供应商 ID */
#define DCM_DID_ECU_SERIAL_NUMBER        (0xF101u)  /* ECU 序列号 */
#define DCM_DID_SOFTWARE_VERSION         (0xF102u)  /* 软件版本号 */
#define DCM_DID_HARDWARE_VERSION         (0xF103u)  /* 硬件版本号 */
#define DCM_DID_BOOT_SW_VERSION          (0xF104u)  /* Bootloader 版本号 */
#define DCM_DID_ACTIVE_SESSION           (0xF184u)  /* 当前活跃诊断会话 (ISO 14229-1) */

/* BMS 实时数据 */
#define DCM_DID_BATTERY_VOLTAGE          (0x2001u)  /* 电池总电压 (V) */
#define DCM_DID_BATTERY_CURRENT          (0x2002u)  /* 电池电流 (A) */
#define DCM_DID_BATTERY_SOC              (0x2003u)  /* SOC (%) */
#define DCM_DID_BATTERY_SOH              (0x2004u)  /* SOH (%) */
#define DCM_DID_MAX_CELL_VOLTAGE         (0x2101u)  /* 最高单体电压 (mV) */
#define DCM_DID_MIN_CELL_VOLTAGE         (0x2102u)  /* 最低单体电压 (mV) */
#define DCM_DID_MAX_CELL_TEMP            (0x2103u)  /* 最高单体温度 (°C) */
#define DCM_DID_MIN_CELL_TEMP            (0x2104u)  /* 最低单体温度 (°C) */

/* BMS 状态 */
#define DCM_DID_BMS_STATUS               (0x3001u)  /* BMS 运行状态 */
#define DCM_DID_CHARGE_STATUS            (0x3002u)  /* 充电状态 */
#define DCM_DID_BALANCE_STATUS           (0x3003u)  /* 均衡状态 */
#define DCM_DID_PROTECT_FLAG             (0x3004u)  /* 保护标志位 */
#define DCM_DID_FAULT_CODE               (0x3005u)  /* 当前故障码 */

/* 标定参数 (可读写) */
#define DCM_DID_CHARGE_OVER_V_THD        (0x4001u)  /* 充电过压阈值 (mV) */
#define DCM_DID_DISCHARGE_UNDER_V_THD    (0x4002u)  /* 放电欠压阈值 (mV) */
#define DCM_DID_CHARGE_OVER_C_THD        (0x4003u)  /* 充电过流阈值 (A) */
#define DCM_DID_CELL_OV_THD              (0x4004u)  /* 单体过压阈值 (mV) */
#define DCM_DID_CELL_UV_THD              (0x4005u)  /* 单体欠压阈值 (mV) */
#define DCM_DID_CELL_OT_THD              (0x4006u)  /* 单体过温阈值 (°C) */
#define DCM_DID_BALANCE_ENABLE_THD       (0x4007u)  /* 均衡使能压差阈值 (mV) */

/*===========================================================================
 * DID 权限位定义
 *===========================================================================*/
#define DCM_DID_ACCESS_RD_DEFAULT        (0x01u)  /* 默认会话可读 */
#define DCM_DID_ACCESS_WR_PROGRAMMING    (0x02u)  /* 编程会话可写 */
#define DCM_DID_ACCESS_WR_EXTENDED       (0x04u)  /* 扩展会话可写 */
#define DCM_DID_ACCESS_RD_EXTENDED       (0x08u)  /* 扩展会话可读 */
#define DCM_DID_ACCESS_WR_SEC_UNLOCK     (0x10u)  /* 安全解锁后才可写 */
#define DCM_DID_ACCESS_RD_SEC_UNLOCK     (0x20u)  /* 安全解锁后才可读 */

/*===========================================================================
 * DTC 配置
 *===========================================================================*/
#define DCM_CFG_MAX_DTC_NUM             (32u)      /* 最大 DTC 数量 */

#endif /* DCM_CFG_H */
