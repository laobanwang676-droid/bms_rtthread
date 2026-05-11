#include <string.h>
#include "stm32f10x.h"
#include "stm32f10x_rcc.h"
#include "usart.h"
#include "delay.h"
#include <rtthread.h>

rt_uint8_t rx_buf[RX_BUF_SIZE];    /* 环形缓冲区 - 需要导出给设备框架 */
rt_uint16_t rx_in = 0;             /* 写指针 (中断专用) */
rt_uint16_t rx_out = 0;            /* 读指针 (系统专用) */
rt_sem_t uart_rx_sem = RT_NULL;

static void USART1_IO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    
    /* 使能GPIOA和GPIOC时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC, ENABLE);
    
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_13;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;  
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_SetBits(GPIOC,GPIO_Pin_13);
    GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;  
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;  
    GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void USART1_func_Init(void)
{
    USART_InitTypeDef USART_InitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;

    /* 使能USART1时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    USART_InitStruct.USART_BaudRate = 115200;                  
    USART_InitStruct.USART_WordLength = USART_WordLength_8b;    
    USART_InitStruct.USART_StopBits = USART_StopBits_1;          
    USART_InitStruct.USART_Parity = USART_Parity_No;             
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None; 
    USART_InitStruct.USART_Mode = USART_Mode_Tx | USART_Mode_Rx; 
    USART_Init(USART1, &USART_InitStruct);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    NVIC_InitStruct.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 5;  
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;         
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE ;
    NVIC_Init(&NVIC_InitStruct);
    USART_ITConfig(USART1, USART_IT_RXNE,ENABLE); 
    USART_Cmd(USART1, ENABLE);
}

void USART1_Init(void)
{
    USART1_IO_Init();
    USART1_func_Init();

    if (uart_rx_sem == RT_NULL)
    {
        uart_rx_sem = rt_sem_create("uart_rx", 0, RT_IPC_FLAG_FIFO);
    }
}

void USART_Send_Data( const uint8_t *data, uint16_t size)
{
	if(size == 0) 
        return;
	for(uint16_t i=0; i < size; i++)
	{
		while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
		
		USART_SendData(USART1, data[i]);
	}
	
	while(USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
}

// static void USART1_Receive_Data(void)
// {   
//     if(USART_GetFlagStatus(USART1, USART_FLAG_RXNE)) 
//     {
//         rxbuf[rxlen] = USART_ReceiveData(USART1);
//         if(rxlen < 9)
//         {
//             rxlen ++;
//         }
//         else
//         {
//             rxlen = 0;
//             memset(rxbuf, 0, sizeof(rxbuf));
//         }
//     }     
// }

void USART1_IRQHandler(void)
{   
    rt_interrupt_enter();
    if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        rt_uint16_t next_in = (rt_uint16_t)((rx_in + 1) % RX_BUF_SIZE);
        if (next_in != rx_out)
        {
            rx_buf[rx_in] = (rt_uint8_t)USART_ReceiveData(USART1);
            rx_in = next_in; // 环形缓冲区写指针前移

            if (uart_rx_sem != RT_NULL)
            {
                rt_sem_release(uart_rx_sem);
            }
        }
        else
        {
            (void)USART_ReceiveData(USART1); /* 缓冲区满，丢弃数据 */
        }
    }
    rt_interrupt_leave();
}

int fputc(int ch, FILE *f)
{
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}
