#include <stdio.h>
#include <stdlib.h>
#include "app.h"
#include "stm32f10x.h"
#include "rtthread.h"
#include "finsh.h"
#include "soft_i2c.h"
#include "bq769.h"
#include "bms_monitor.h"
#include "bms_global_define.h"
#include "bms_info.h"
#include "bms_analysis.h"
#include "bms_protect.h"
#include "bms_balance.h"
#include "EcuM.h"

uint16_t led_time = 200;  /* LED闪烁时间，单位ms */
rt_thread_t led_thread;

/* I2C互斥锁，保护BQ769X0接口 */
rt_mutex_t i2c_mutex;

static void led_init(void);

void app_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    
    NVIC_SetPriorityGrouping(NVIC_PriorityGroup_4); // 设置优先级分组4
	BQ769X0_InitDataTypedef InitData;

	InitData.AlertOps.ocd 	 = BMS_ProtectHwOCD;
	InitData.AlertOps.scd 	 = BMS_ProtectHwSCD;
	InitData.AlertOps.ov	 = BMS_ProtectHwOV;
	InitData.AlertOps.uv 	 = BMS_ProtectHwUV;	

	// // 使用硬件中断通知,如果烧写程序后必须重新上下电一次BQ芯片或者复位
	// // InitData.AlertOps.cc 	 = BMS_MonitorHwCurrent;
	InitData.AlertOps.cc 	 = NULL;

	// 这两个中断会造成系统故障
	// 第一个报警时设备故障,表示BQ芯片有问题了
	// 第二个报警可能存在被外界电磁信号干扰造成误判,之前出现过,换了个跟官方一样阻值的电阻就没出现过了
	InitData.AlertOps.device = BMS_ProtectHwDevice;
	InitData.AlertOps.ovrd 	 = BMS_ProtectHwOvrd;

	InitData.ConfigData.SCDDelay	 = (BQ769X0_SCDDelayTypedef)INIT_SCD_DELAY;
	InitData.ConfigData.OCDDelay	 = (BQ769X0_OCDDelayTypedef)INIT_OCD_DELAY;
	InitData.ConfigData.UVDelay	 	 = (BQ769X0_OVDelayTypedef)INIT_UV_DELAY;
	InitData.ConfigData.OVDelay	 	 = (BQ769X0_UVDelayTypedef)INIT_OV_DELAY;
	InitData.ConfigData.UVPThreshold = INIT_UV_PROTECT * 1000;
	InitData.ConfigData.OVPThreshold = INIT_OV_PROTECT * 1000;
	I2C_BusInitialize();
	
	/* 创建I2C互斥锁 */
	i2c_mutex = rt_mutex_create("i2c_mutex", RT_IPC_FLAG_FIFO);
	if (i2c_mutex == RT_NULL)
	{
		rt_kprintf("Failed to create I2C mutex!\n");
		return; 
	}
	
    // EcuM_Init();
    // EcuM_StartupTwo();
	BQ769X0_Initialize(&InitData);

    BMS_MonitorInit();
    BMS_ProtectInit();
    BMS_AnalysisInit();
    BMS_InfoInit();
    BMS_BalanceInit();
}

void led_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    
    /* 使能GPIOC时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_13;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;  
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_SetBits(GPIOC,GPIO_Pin_13);
    GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void led_toggle(void)
{
    GPIOC->ODR ^= GPIO_Pin_13;  /* 切换LED状态 */
}   

void led_task(void *parameter)
{
    while(1)
    {   
        led_toggle();
        rt_thread_delay(led_time);
    }   
}

static void led_init(void)
{
    led_gpio_init();
    led_thread = rt_thread_create("led", led_task, NULL, 256, 5, 10);
    rt_thread_startup(led_thread);
}

static int shell_test(int argc, char **argv)
{
    if(argc > 1)
    {
        led_time = atoi(argv[1]);
        if (led_time < 50) led_time = 50;  // 最小闪烁时间限制
    }
    else
    {
        led_time = 200;  // 默认闪烁时间
    }
    return 0;
}
MSH_CMD_EXPORT(shell_test, user shell test command);
