# STM32F103 ARM GCC 交叉编译工具链配置文件

# 1. 声明交叉编译目标系统为裸机系统
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 2. 指定编译器
# 说明：如果只写可执行文件名（不带完整路径），则需要把 ARM GCC 加入 PATH。
# 下面是 PATH 模式的写法：
set(CMAKE_C_COMPILER arm-none-eabi-gcc CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER arm-none-eabi-g++ CACHE FILEPATH "C++ compiler")
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc CACHE FILEPATH "ASM compiler")
set(CMAKE_AR arm-none-eabi-ar CACHE FILEPATH "Archiver")

# 原文（绝对路径写法，不依赖 PATH，保留作对比）
# set(CMAKE_C_COMPILER D:/arm_gcc/bin/arm-none-eabi-gcc.exe CACHE FILEPATH "C compiler")
# set(CMAKE_CXX_COMPILER D:/arm_gcc/bin/arm-none-eabi-g++.exe CACHE FILEPATH "C++ compiler")
# set(CMAKE_ASM_COMPILER D:/arm_gcc/bin/arm-none-eabi-gcc.exe CACHE FILEPATH "ASM compiler")
# set(CMAKE_AR D:/arm_gcc/bin/arm-none-eabi-ar.exe CACHE FILEPATH "Archiver")

# 3. 指定工具程序
# PATH 模式写法：
set(CMAKE_OBJCOPY arm-none-eabi-objcopy CACHE FILEPATH "objcopy tool")
set(CMAKE_OBJDUMP arm-none-eabi-objdump CACHE FILEPATH "objdump tool")
set(CMAKE_SIZE arm-none-eabi-size CACHE FILEPATH "size tool")
set(CMAKE_STRIP arm-none-eabi-strip CACHE FILEPATH "strip tool")

# 原文（绝对路径写法，不依赖 PATH，保留作对比）
# set(CMAKE_OBJCOPY D:/arm_gcc/bin/arm-none-eabi-objcopy.exe CACHE FILEPATH "objcopy tool")
# set(CMAKE_OBJDUMP D:/arm_gcc/bin/arm-none-eabi-objdump.exe CACHE FILEPATH "objdump tool")
# set(CMAKE_SIZE D:/arm_gcc/bin/arm-none-eabi-size.exe CACHE FILEPATH "size tool")
# set(CMAKE_STRIP D:/arm_gcc/bin/arm-none-eabi-strip.exe CACHE FILEPATH "strip tool")

# 4. 强制 CMake 跳过编译器检查
# （因为编译的是单片机代码，不是本地系统可执行文件）
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
