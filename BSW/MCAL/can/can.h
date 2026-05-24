#ifndef CAN_H
#define CAN_H

#include "stm32f10x.h"

/* AUTOSAR 相关定义 */
#define CAN_VENDOR_ID            0xFFFFu  // 示例供应商 ID
#define CAN_MODULE_ID            0x0023u  // 示例模块 ID
#define CAN_SW_MAJOR_VERSION     1u       // 软件主版本号
#define CAN_SW_MINOR_VERSION     0u       // 软件次版本号
#define CAN_SW_PATCH_VERSION     0u       // 软件补丁版本号

// AUTOSAR 标准返回值
typedef uint8_t Std_ReturnType;
#define E_OK      0x00u
#define E_NOT_OK  0x01u

typedef struct {
    uint16_t vendorID;
    uint16_t moduleID;
    uint8_t sw_major_version;
    uint8_t sw_minor_version;
    uint8_t sw_patch_version;
} Std_VersionInfoType;

// AUTOSAR CAN 相关基础类型
typedef uint32_t Can_IdType;
typedef uint16_t PduIdType;
typedef uint16_t Can_HwHandleType; // 硬件对象句柄（如邮箱编号或通道）

typedef uint16_t SduLengthType;
typedef uint8_t* SduDataPtrType;

typedef struct {
    SduLengthType SduLength;
    SduDataPtrType SduDataPtr;
} PduInfoType;

typedef enum {
    CAN_OK = 0x00u,
    CAN_NOT_OK = 0x01u,
    CAN_BUSY = 0x02u
} Can_ReturnType;

typedef enum {
    CAN_CS_UNINIT = 0x00u,
    CAN_CS_STOPPED = 0x01u,
    CAN_CS_STARTED = 0x02u,
    CAN_CS_SLEEP = 0x03u
} Can_ControllerStateType;

typedef enum {
    CAN_T_START = 0x00u,
    CAN_T_STOP = 0x01u,
    CAN_T_SLEEP = 0x02u,
    CAN_T_WAKEUP = 0x03u
} Can_StateTransitionType;

typedef enum {
    CAN_ERRORSTATE_ACTIVE = 0x00u,
    CAN_ERRORSTATE_PASSIVE = 0x01u,
    CAN_ERRORSTATE_BUSOFF = 0x02u
} Can_ErrorStateType;

typedef struct {
    Can_IdType CanId;
    uint8_t ControllerId;
    Can_HwHandleType Hoh;
} Can_HwType;

#define CAN_ID_EXTENDED_FLAG 0x80000000u
#define CAN_ID_RTR_FLAG      0x40000000u

// AUTOSAR PDU (Protocol Data Unit) 结构体
typedef struct {
    Can_IdType id;          // CAN ID (Bit 31 扩展帧标志, Bit 30 RTR 标志)
    uint8_t length;         // 数据长度 (DLC)
    uint8_t* sdu;           // 指向有效负载数据的指针 (Service Data Unit)
    PduIdType swPduHandle;  // 软件 PDU 句柄 (供上层回调使用，简易版可忽略)
} Can_PduType;

// AUTOSAR 初始化配置结构体 (简易版)
typedef struct {
    uint16_t prescaler;
    uint8_t sjw;
    uint8_t bs1;
    uint8_t bs2;
    FunctionalState autoBusOff;
    FunctionalState autoWakeUp;
    FunctionalState noAutoRetransmission;
} Can_ControllerConfigType;

typedef struct {
    uint8_t filterNumber;
    uint16_t idHigh;
    uint16_t idLow;
    uint16_t maskHigh;
    uint16_t maskLow;
    uint8_t fifoAssignment; // CAN_Filter_FIFO0 or CAN_Filter_FIFO1
    uint8_t filterMode;      // CAN_FilterMode_IdMask or CAN_FilterMode_IdList
    uint8_t filterScale;     // CAN_FilterScale_32bit or CAN_FilterScale_16bit
    FunctionalState active;
} Can_FilterConfigType;

typedef struct {
    const Can_ControllerConfigType* controller;
    const Can_FilterConfigType* filters;
    uint8_t filterCount;
} Can_ConfigType;

// 标准接口声明
void Can_Init(const Can_ConfigType* Config);
void Can_DeInit(void);
Can_ReturnType Can_Write(Can_HwHandleType HwTxPduId, const Can_PduType* PduInfo);
Std_ReturnType Can_SetControllerMode(uint8_t Controller, Can_StateTransitionType Transition);
Std_ReturnType Can_GetControllerMode(uint8_t Controller, Can_ControllerStateType* ControllerModePtr);
Std_ReturnType Can_GetControllerErrorState(uint8_t Controller, Can_ErrorStateType* ErrorStatePtr);
void Can_DisableControllerInterrupts(uint8_t Controller);
void Can_EnableControllerInterrupts(uint8_t Controller);
void Can_GetVersionInfo(Std_VersionInfoType* versioninfo);
void Can_MainFunction_Read(void);
void Can_MainFunction_Write(void);
void Can_MainFunction_BusOff(void);
void Can_MainFunction_Wakeup(void);
void Can_MainFunction_Mode(void);
void Can_MainFunction_Error(void);

void Can_IsrRxFifo0(void);
void Can_IsrRxFifo1(void);
void Can_IsrTx(void);
void Can_IsrBusOff(void);
void Can_IsrWakeup(void);
void Can_IsrError(void);

#endif /* CAN_H */
