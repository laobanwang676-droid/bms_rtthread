#ifndef __APP_H__
#define __APP_H__
#include <stdint.h>
#include <rtthread.h>

/* I2C互斥锁，保护BQ769X0接口 */
extern rt_mutex_t i2c_mutex;

void app_init(void);

#endif /*__APP_H__*/

