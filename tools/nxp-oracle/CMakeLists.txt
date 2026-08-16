# Out-of-tree build of NXP's MCUXpresso VGLite example evkmimxrt1170_08_BlitColor.
#
# WHY: it is the reference oracle for "does the GC355 render on THIS board?".
# Our own bare-metal port initialises the GPU (chip ID 0x355) but writes no
# pixels; NXP's example is known-good code for the same silicon, so running it
# converts guessing into diffing.
#
# BlitColor was chosen over SimplePath because it is one of only two examples in
# the set that does NOT start a FreeRTOS scheduler, and a blit is the simplest
# GPU write there is. It still #includes FreeRTOS.h (config/heap only), and the
# VGLite driver here uses its rtos port layer, so FreeRTOS sources are compiled.
#
# The reference checkout is READ-ONLY: nothing is written back into it.
cmake_minimum_required(VERSION 3.24)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(TC /Applications/ARM_10/bin/arm-none-eabi)
set(CMAKE_C_COMPILER   ${TC}-gcc)
set(CMAKE_CXX_COMPILER ${TC}-g++)
set(CMAKE_ASM_COMPILER ${TC}-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(blitcolor C ASM)

set(SDK $ENV{HOME}/Development/gs-vglite_examples_rt1170)
set(EX  ${SDK}/evkmimxrt1170_08_BlitColor)

file(GLOB EX_SRC     ${EX}/source/*.c)
file(GLOB DRV_SRC    ${SDK}/common/drivers/*.c)
file(GLOB BOARD_SRC  ${SDK}/common/board/*.c)
file(GLOB UTIL_SRC   ${SDK}/common/utilities/*.c)
file(GLOB DEV_SRC    ${SDK}/common/device/*.c)
file(GLOB START_SRC  ${SDK}/common/startup/*.c)
file(GLOB XIP_SRC    ${SDK}/common/xip/*.c)
file(GLOB WIN_SRC    ${SDK}/common/window/*.c)
file(GLOB_RECURSE COMP_SRC ${SDK}/common/component/*.c)
file(GLOB VIDEO_SRC  ${SDK}/common/video/*.c)
file(GLOB VG_SRC     ${SDK}/common/vglite/VGLite/*.c
                     ${SDK}/common/vglite/VGLite/rtos/*.c
                     ${SDK}/common/vglite/VGLiteKernel/*.c
                     ${SDK}/common/vglite/VGLiteKernel/rtos/*.c)
file(GLOB RTOS_SRC   ${SDK}/common/freertos/freertos-kernel/*.c
                     ${SDK}/common/freertos/freertos-kernel/portable/GCC/ARM_CM4F/*.c)
# MemMang ships several mutually-exclusive heap implementations; heap_4 is the
# one MCUXpresso projects use. Globbing the directory would define pvPortMalloc
# five times over.
list(APPEND RTOS_SRC ${SDK}/common/freertos/freertos-kernel/portable/MemMang/heap_4.c)

add_executable(blitcolor.elf
    ${EX_SRC} ${DRV_SRC} ${BOARD_SRC} ${UTIL_SRC} ${DEV_SRC}
    ${START_SRC} ${XIP_SRC} ${WIN_SRC} ${COMP_SRC} ${VIDEO_SRC} ${VG_SRC} ${RTOS_SRC})

target_include_directories(blitcolor.elf PRIVATE
    ${EX}/source
    ${SDK}/common/board
    ${SDK}/common/device
    ${SDK}/common/drivers
    ${SDK}/common/utilities
    ${SDK}/common/startup
    ${SDK}/common/xip
    ${SDK}/common/window
    ${SDK}/common/video
    ${SDK}/common/component
    ${SDK}/common/component/lists
    ${SDK}/common/component/uart
    ${SDK}/common/component/serial_manager
    ${SDK}/common/CMSIS
    ${SDK}/common/vglite/inc
    ${SDK}/common/vglite/VGLite
    ${SDK}/common/vglite/VGLite/rtos
    ${SDK}/common/vglite/VGLiteKernel
    ${SDK}/common/vglite/VGLiteKernel/rtos
    ${SDK}/common/freertos/freertos-kernel/include
    ${SDK}/common/freertos/freertos-kernel/portable/GCC/ARM_CM4F)

# CUSTOM_VGLITE_MEMORY_CONFIG=1 is REQUIRED by the example (it #errors without
# it) -- the app supplies vglite_heap[]/vglite_heap_size itself.
target_compile_definitions(blitcolor.elf PRIVATE
    CPU_MIMXRT1176DVMAA_cm7
    CUSTOM_VGLITE_MEMORY_CONFIG=1
    SDK_DEBUGCONSOLE=1
    SERIAL_PORT_TYPE_UART=1
    XIP_EXTERNAL_FLASH=1
    XIP_BOOT_HEADER_ENABLE=1
    SDK_OS_FREE_RTOS
    __USE_CMSIS)

target_compile_options(blitcolor.elf PRIVATE
    -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb
    -O1 -g -ffunction-sections -fdata-sections -fno-common
    -Wno-implicit-function-declaration -Wno-incompatible-pointer-types)

target_link_options(blitcolor.elf PRIVATE
    -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/ld/evkmimxrt1170_08_BlitColor_Debug.ld
    -L${CMAKE_CURRENT_SOURCE_DIR}/ld
    -Wl,--gc-sections -Wl,-Map=blitcolor.map
    --specs=nano.specs --specs=nosys.specs)
