#include <stdio.h>
#include "app.h"
#include "stm32f10x.h"
#include "rtthread.h"

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
        rt_thread_delay(100);  /* 延时500ms */
        // rt_kprintf("LED toggled in thread.\n");
    }   
}
