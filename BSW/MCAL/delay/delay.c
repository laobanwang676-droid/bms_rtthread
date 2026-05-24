#include <stdint.h>
#include <string.h>
#include "delay.h"
#include "stm32f10x.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_rcc.h"

#define TIM3_PRESCALER   (71)        
#define TICKS_PER_US     (1)         // 1 tick = 1us
#define TICKS_PER_MS     (1000)      // 1ms = 1000 ticks
#define TIM3_AUTO_RELOAD (999)       // 1ms中断一次

static volatile uint64_t tim_tick_count;
static tim_periodic_callback_t periodic_callback;
extern void rt_os_tick_callback(void);

void tim_tick_init(void)
{ 
    // 初始化TIM3结构体
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    
    /* 使能TIM3时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    
    TIM_TimeBaseStructure.TIM_Prescaler = TIM3_PRESCALER;// 预分频值
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;// 向上计数模式
    TIM_TimeBaseStructure.TIM_Period = TIM3_AUTO_RELOAD;// 自动重装载值
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
    
    // 清除中断标志，初始化时可能产生一个更新中断
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    // 使能中断
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
    
    // 配置NVIC
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    
    // 启动定时器
    TIM_Cmd(TIM3, ENABLE);
}

uint64_t tim_now(void)
{
    uint64_t now, last_count;
    uint16_t cnt;
    
    do {
        last_count = tim_tick_count;
        cnt = TIM_GetCounter(TIM3);
        
        // 检查是否发生更新但未处理
        if (TIM_GetFlagStatus(TIM3, TIM_FLAG_Update) != RESET)
        {
            cnt = TIM_GetCounter(TIM3);
            now = last_count + TICKS_PER_MS + cnt;
        } else 
        {
            now = last_count + cnt;
        }
    } while (last_count != tim_tick_count);
    
    return now;
}

uint64_t tim_get_us(void)
{
    return tim_now() / TICKS_PER_US;
}

uint64_t tim_get_ms(void)
{
    return tim_now() / TICKS_PER_MS;
}

void tim_delay_us(uint32_t us)
{
    uint64_t now = tim_now();
    while (tim_now() - now < (uint64_t)us * TICKS_PER_US);
}

void tim_delay_ms(uint32_t ms)
{
    uint64_t now = tim_now();
    while (tim_now() - now < (uint64_t)ms * TICKS_PER_MS);
}

void function_address_passing(tim_periodic_callback_t callback)
{
    periodic_callback = callback;
}

// TIM3中断服务函数
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
     {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        tim_tick_count += TICKS_PER_MS;
        
        if (periodic_callback) 
        {
            periodic_callback();
        }
        rt_os_tick_callback();
    }
}
