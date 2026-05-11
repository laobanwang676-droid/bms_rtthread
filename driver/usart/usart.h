#ifndef _USART_H_
#define _USART_H_

#include "stm32f10x.h"
#include "stdio.h"
#include <rtthread.h>

#define RX_BUF_SIZE 128

extern rt_uint8_t rx_buf[RX_BUF_SIZE];
extern rt_uint16_t rx_in;  // 写指针 (中断专用)
extern rt_uint16_t rx_out; // 读指针 (系统专用)
extern rt_sem_t uart_rx_sem;

void USART1_Init(void);
void USART_Send_Data( const uint8_t *data, uint16_t size);
void USART1_IRQHandler(void);

int fputc(int ch, FILE *f);


#endif /*__TUSART_H__*/

