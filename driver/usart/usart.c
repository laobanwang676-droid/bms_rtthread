#include <string.h>
#include "stm32f10x.h"
#include "stm32f10x_rcc.h"
#include "usart.h"
#include "delay.h"
#include <rtthread.h>

rt_uint8_t rx_buf[RX_BUF_SIZE];    /* 环形缓冲区 - 需要导出给设备框架 */
rt_uint16_t rx_in = 0;             /* 写指针 (中断专用) */
rt_uint16_t rx_out = 0;            /* 读指针 (系统专用) */
rt_device_t uart_device = RT_NULL; /* 设备结构体 */
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
}

/* ==================== 设备框架接口实现 ==================== */

/* 设备read函数 - 从环形缓冲区读取数据 */
static rt_size_t uart_read(rt_device_t dev, rt_off_t pos, 
                           void *buffer, rt_size_t size)
{
    rt_uint8_t *buf = (rt_uint8_t*)buffer;
    rt_size_t read_size = 0;
    
    for(rt_size_t i = 0; i < size; i++)
    {
        /* 检查环形缓冲区是否有数据 */
        if(rx_in != rx_out)
        {
            buf[i] = rx_buf[rx_out];
            rx_out = (rx_out + 1) % RX_BUF_SIZE;
            read_size++;
        }
        else
        {
            break;  /* 无数据，返回已读字节数 */
        }
    }
    return read_size;
}

/* 设备write函数 - 通过USART发送数据 */
static rt_size_t uart_write(rt_device_t dev, rt_off_t pos,
                            const void *buffer, rt_size_t size)
{
    const rt_uint8_t *buf = (const rt_uint8_t*)buffer;
    USART_Send_Data(buf, size);
    return size;
}

/* 设备控制函数 */
static rt_err_t uart_control(rt_device_t dev, int cmd, void *args)
{
    return RT_EOK;
}

/* 设备初始化函数 */
static rt_err_t uart_device_init(rt_device_t dev)
{
    return RT_EOK;
}

/* 注册UART设备到RT-Thread系统 */
static int uart_device_register(void)
{
    uart_device = rt_device_create(RT_Device_Class_Char, "uart0");
    
    if(uart_device == RT_NULL)
    {
        rt_kprintf("uart device create failed!\n");
        return -1;
    }
    
    uart_device->init    = uart_device_init;
    uart_device->read    = uart_read;
    uart_device->write   = uart_write;
    uart_device->control = uart_control;
    
    rt_device_register(uart_device, "uart0", 
                       RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    
    rt_kprintf("uart device register successfully!\n");
    return 0;
}
INIT_DEVICE_EXPORT(uart_device_register);

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
        rx_buf[rx_in] = USART_ReceiveData(USART1);
        rx_in = (rx_in + 1) % RX_BUF_SIZE; // 环形缓冲区写指针前移
    }
    rt_interrupt_leave();
}

int fputc(int ch, FILE *f)
{
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}
