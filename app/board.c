/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-05-24                  the first version
 */

#include <rthw.h>
#include <rtthread.h>
#include "usart.h"
#include "delay.h"

#if defined(RT_USING_USER_MAIN) && defined(RT_USING_HEAP)
/*
 * Please modify RT_HEAP_SIZE if you enable RT_USING_HEAP
 * the RT_HEAP_SIZE max value = (sram size - ZI size), 1024 means 1024 bytes
 */
#define RT_HEAP_SIZE (15*1024)
static rt_uint8_t rt_heap[RT_HEAP_SIZE];

RT_WEAK void *rt_heap_begin_get(void)
{
    return rt_heap;
}

RT_WEAK void *rt_heap_end_get(void)
{
    return rt_heap + RT_HEAP_SIZE;
}
#endif

void rt_os_tick_callback(void)
{
    rt_interrupt_enter();
    
    rt_tick_increase();

    rt_interrupt_leave();
}

/**
 * This function will initial your board.
 */
void rt_hw_board_init(void)
{
    /* 
     * TODO 1: OS Tick Configuration
     * Enable the hardware timer and call the rt_os_tick_callback function
     * periodically with the frequency RT_TICK_PER_SECOND. 
     */
    tim_tick_init();  /* Initialize TIM3 for OS tick (1ms interval) */

    /* Call components board initial (use INIT_BOARD_EXPORT()) */
#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif

#if defined(RT_USING_USER_MAIN) && defined(RT_USING_HEAP)
    rt_system_heap_init(rt_heap_begin_get(), rt_heap_end_get());
#endif
}

#ifdef RT_USING_CONSOLE

static int uart_init(void)
{
    USART1_Init();  /* USART1 initialized with 115200 baud rate */
    return 0;
}
INIT_BOARD_EXPORT(uart_init);

void rt_hw_console_output(const char *str)
{
    while (*str != '\0')
    {
        if (*str == '\n')
        {
            USART_SendData(USART1, '\r');
            while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        }
        
        USART_SendData(USART1, *str);
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        
        str++;
    }
}

rt_int32_t rt_hw_console_getchar(void)
{
    rt_int32_t ch = -1;

    if (uart_rx_sem != RT_NULL)
    {
        rt_sem_take(uart_rx_sem, RT_WAITING_FOREVER);
    }

    rt_base_t level = rt_hw_interrupt_disable();//关闭中断 函数在汇编实现
    if (rx_in != rx_out)
    {
        ch = rx_buf[rx_out];  /* 从环形缓冲区读数据 */
        rx_out = (rx_out + 1) % RX_BUF_SIZE; /* 读指针前移 */
    }
    rt_hw_interrupt_enable(level);

    return ch;
}

#endif

