#include "app.h"
#include "stdio.h"
#include "rtthread.h"
int main()
{   
    led_init();
    while(1)
    {
        rt_thread_delay(1000);  /* 主线程每秒钟延时一次，保持系统运行 */
    }   
}
