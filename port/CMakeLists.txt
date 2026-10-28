# trs80 -- a TRS-80 Model I for the PicoCalc (Pico 2 W)
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(trs80 C CXX ASM)
pico_sdk_init()

set(TS ${CMAKE_CURRENT_LIST_DIR}/../../btkbd/modules/picocalc-text-starter)
set(SHELL_INC ${CMAKE_CURRENT_LIST_DIR}/../../btkbd/modules/picocalc-shell/include)
set(CORE ${CMAKE_CURRENT_LIST_DIR}/../core)
set(DRV ${TS}/drivers)

add_executable(trs80 main.c ${CORE}/m1.c ${CORE}/sz_z80.c ${CORE}/keys.c ${CORE}/sha1.c
    ${DRV}/fat32.c ${DRV}/sdcard.c ${DRV}/font-5x10.c ${DRV}/font-8x10.c
    ${DRV}/keyboard.c ${DRV}/lcd.c ${DRV}/southbridge.c)
target_compile_definitions(trs80 PRIVATE M1_CPU_SZ)
target_compile_options(trs80 PRIVATE -Wall -Wextra)
target_include_directories(trs80 PRIVATE ${CORE} ${TS} ${DRV} ${SHELL_INC})
pico_set_program_name(trs80 "trs80")
pico_enable_stdio_uart(trs80 0)
pico_enable_stdio_usb(trs80 0)
target_link_libraries(trs80 pico_stdlib hardware_gpio hardware_i2c hardware_spi hardware_pio)
pico_add_extra_outputs(trs80)
