#include "stm32f10x.h"

static void CAN_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    //外部已开RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); // 使能GPIOA时钟
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11; // CAN_RX
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; // 上拉输入
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12; // CAN_TX
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; // 复用推挽输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

static void CAN_Function_Init(void)
{
    CAN_InitTypeDef CAN_InitStructure;
    CAN_FilterInitTypeDef CAN_FilterInitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE); // 使能CAN1时钟
    //CAN初始化
    CAN_InitStructure.CAN_TTCM = DISABLE; // 禁止时间触发通信模式
    CAN_InitStructure.CAN_ABOM = DISABLE; // 禁止自动离线管理
    CAN_InitStructure.CAN_AWUM = DISABLE; // 禁止自动唤醒模式
    CAN_InitStructure.CAN_NART = DISABLE; // 禁止报文重复传送
    CAN_InitStructure.CAN_RFLM = DISABLE; // 允许报文丢失
    CAN_InitStructure.CAN_TXFP = DISABLE; // 发送请求由软件产生

    //CAN波特率设置为500kbps，APB1时钟为36MHz
    CAN_InitStructure.CAN_SJW = CAN_SJW_1tq; // 同步跳转时间1个时间单位
    CAN_InitStructure.CAN_BS1 = CAN_BS1_8tq; // 时间段1为8个时间单位
    CAN_InitStructure.CAN_BS2 = CAN_BS2_7tq; // 时间段2为7个时间单位
    CAN_InitStructure.CAN_Prescaler = 3; // 分频系数为3，得到12MHz的时间基准

    if (CAN_Init(CAN1, &CAN_InitStructure) != CAN_InitStatus_Success)
    {
        // 初始化失败处理
        while(1);
    }

    //CAN滤波器配置，接受所有ID的报文
    CAN_FilterInitStructure.CAN_FilterNumber = 0; // 滤波器0
    CAN_FilterInitStructure.CAN_FilterMode = CAN_FilterMode_IdMask; // 标识符掩码模式
    CAN_FilterInitStructure.CAN_FilterScale = CAN_FilterScale_32bit; // 32位滤波器
    CAN_FilterInitStructure.CAN_FilterIdHigh = 0x0000; // 接受所有ID
    CAN_FilterInitStructure.CAN_FilterIdLow = 0x0000;
    CAN_FilterInitStructure.CAN_FilterMaskIdHigh = 0x0000; // 不使用掩码
    CAN_FilterInitStructure.CAN_FilterMaskIdLow = 0x0000;
    CAN_FilterInitStructure.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0; // 分配到FIFO0
    CAN_FilterInitStructure.CAN_FilterActivation = ENABLE; // 激活滤波器
    CAN_FilterInit(&CAN_FilterInitStructure);
}

static void CAN_NVIC_Init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn; // CAN接收中断
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 4; // 抢占优先级0
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; // 子优先级0
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; // 使能中断
    NVIC_Init(&NVIC_InitStructure);
}

void CAN_InitAll(void)
{
    CAN_GPIO_Init();
    CAN_Function_Init();
    CAN_NVIC_Init();
}