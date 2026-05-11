#include <stdio.h>
#include "app.h"
#include "stm32f10x.h"
#include "rtthread.h"
#include "finsh.h"

uint16_t led_time = 200;  /* LED闪烁时间，单位ms */
rt_thread_t led_thread;
void led_task(void *parameter);

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

void led_init(void)
{
    led_gpio_init();
    led_thread = rt_thread_create("led", led_task, NULL, 256, 5, 10);
    rt_thread_startup(led_thread);
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
        // rt_kprintf("LED toggled in thread.\n");
    }   
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
