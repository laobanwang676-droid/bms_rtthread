#include "app.h"
#include "stdio.h"
#include "rtthread.h"
#include "can.h"
#include "Rte.h"
int main()
{   
    app_init();  /* 初始化应用程序 */
    while(1)
    {
        rt_thread_delay(200);  /* 主线程每秒钟延时一次，保持系统运行 */
    }   
}
