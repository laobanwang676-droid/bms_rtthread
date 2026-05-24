#ifndef __TIM_DELAY_H__
#define __TIM_DELAY_H__
#include <stdint.h>

typedef void (*tim_periodic_callback_t)(void);//定义一个无参数无返回值的函数类型用于回调函数

void tim_tick_init(void);
uint64_t tim_now(void);
uint64_t tim_get_us(void);
uint64_t tim_get_ms(void);
void tim_delay_us(uint32_t us);
void tim_delay_ms(uint32_t ms);
void function_address_passing(tim_periodic_callback_t callback);

#endif /* __tim_DELAY_H__ */
