# STM32F103 BMS 项目 CMake 配置

# 1. 指定 CMake 最低版本
cmake_minimum_required(VERSION 3.15)

# 2. 定义工程名称和支持的语言 (C 语言、ASM 汇编)
project(BMS_STM32F103 C ASM)

# -----------------------------------------------------------
# 3. CPU 架构和编译优化选项
# -----------------------------------------------------------
# -mcpu=cortex-m3    : STM32F103 使用 Cortex-M3 核心
# -mthumb            : 使用 Thumb-2 指令集
# -O2 -g             : O2 优化 + 调试符号
# -Wall -Wextra      : 开启所有警告
set(CPU_FLAGS "-mcpu=cortex-m3 -mthumb")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS} -Os -g -ffunction-sections -fdata-sections -Wall -Wextra -D__weak=__attribute__((weak))")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPU_FLAGS} -Os -g -ffunction-sections -fdata-sections -Wall -Wextra -D__weak=__attribute__((weak))")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${CPU_FLAGS} -g -x assembler-with-cpp")

# 链接标志
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${CPU_FLAGS} -Wl,--gc-sections -Wl,--print-memory-usage")

# -----------------------------------------------------------
# 4. 收集源文件
# -----------------------------------------------------------
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "firmware/comsis/core/startup_stm32f10x_md_gcc.S"
    # 应用层源文件
    "ASW/*.c"
    "ASW/bms_app/*.c"
    "ASW/bms_config/*.c"
    # BSW 层源文件
    "BSW/ECUAL/*.c"
    "BSW/ECUM/*.c"
    "BSW/MCAL/can/*.c"
    "BSW/MCAL/delay/*.c"
    "BSW/MCAL/soft_i2c/*.c"
    "BSW/MCAL/usart/*.c"
    "BSW/SERVICES/*.c"
    "BSW/CDD/bq769/*.c"
    # Firmware 源文件
    "firmware/driver/src/*.c"
    "firmware/comsis/core/*.c"
    "firmware/comsis/core/*.S"
    "firmware/comsis/device/*.c"
    # RT-Thread 源文件 (排除其他芯片库)
    "third_lib/rt-thread/src/*.c"
    "third_lib/rt-thread/libcpu/arm/cortex-m3/*.c"
    "third_lib/rt-thread/libcpu/arm/cortex-m3/context_gcc.S"
    "third_lib/rt-thread/components/finsh/*.c"
    # RTE 源文件
    "RTE/*.c"
)

# 排除不需要的源文件
list(FILTER SOURCES EXCLUDE REGEX "third_lib/rt-thread/libcpu/risc-v")
list(FILTER SOURCES EXCLUDE REGEX "firmware/comsis/core/startup_stm32f10x_md.s")
list(FILTER SOURCES EXCLUDE REGEX "firmware/comsis/core/core_cm3.c")
# -----------------------------------------------------------
# 5. 定义可执行文件目标 (.elf)
# -----------------------------------------------------------
add_executable(${PROJECT_NAME}.elf ${SOURCES})

# -----------------------------------------------------------
# 5.1 定义 STM32 相关宏
# -----------------------------------------------------------
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
    STM32F10X_MD
    USE_STDPERIPH_DRIVER
    HSE_VALUE=8000000
)

# -----------------------------------------------------------
# 6. 指定头文件搜索路径
# -----------------------------------------------------------
target_include_directories(${PROJECT_NAME}.elf PRIVATE
    # 应用层头文件
    ASW
    ASW/bms_app
    ASW/bms_config
    # BSW 层头文件
    BSW/ECUAL
    BSW/ECUM
    BSW/MCAL/can
    BSW/MCAL/delay
    BSW/MCAL/soft_i2c
    BSW/MCAL/usart
    BSW/SERVICES
    BSW/CDD/bq769
    # Firmware 头文件
    firmware/driver/inc
    firmware/comsis/core
    firmware/comsis/device
    # RT-Thread 头文件
    third_lib/rt-thread/include
    third_lib/rt-thread/libcpu/arm/cortex-m3
    third_lib/rt-thread/components/finsh
    # RTE 头文件
    RTE
)

# -----------------------------------------------------------
# 7. 链接库和链接选项
# -----------------------------------------------------------
# 指定链接脚本
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/firmware/comsis/core/BMS_gcc.ld)

target_link_options(${PROJECT_NAME}.elf PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections              # 清理未使用的代码
    -Wl,--print-memory-usage       # 打印内存使用情况
    -Wl,-Map=${PROJECT_NAME}.map   # 生成 .map 文件便于分析
    --specs=nosys.specs            # 提供空实现以满足系统调用符号
    --specs=nano.specs             # 使用 newlib-nano 减小体积
    -Wl,-u,_printf_float
)

target_link_libraries(${PROJECT_NAME}.elf PRIVATE
    m
)

# 链接 CMSIS DSP 库（如果需要）
# target_link_libraries(${PROJECT_NAME}.elf PRIVATE
#     ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Lib/ARM/arm_cortexM3l_math.lib
# )

# -----------------------------------------------------------
# 8. 生成烧录文件 (.hex, .bin)
# -----------------------------------------------------------
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex 
            ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
    COMMENT "creating HEX file"
)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary 
            ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    COMMENT "creating BIN file"
)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
    COMMENT "printing size information"
)
