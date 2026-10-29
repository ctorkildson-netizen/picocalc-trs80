# trs80 -- a TRS-80 Model I for the PicoCalc (Pico 2 W or Pico 2)
#
#   git clone --recursive https://github.com/ctorkildson-netizen/picocalc-trs80
#   cmake -B build -DPICO_SDK_PATH=/path/to/pico-sdk && cmake --build build
#
# makes build/trs80.uf2. The emulator itself (core/) is portable C; the
# PicoCalc's screen, keyboard, card and sound are in port/.
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(trs80 C CXX ASM)
pico_sdk_init()

set(CORE ${CMAKE_CURRENT_LIST_DIR}/core)
set(TS ${CMAKE_CURRENT_LIST_DIR}/modules/picocalc-text-starter)
set(DRV ${TS}/drivers)
set(SHELL_INC ${CMAKE_CURRENT_LIST_DIR}/modules/picocalc-shell/include)
if (NOT EXISTS ${DRV}/fat32.c)
    message(FATAL_ERROR "The submodules are missing: git submodule update --init")
endif()
include(${CMAKE_CURRENT_LIST_DIR}/modules/picocalc-extras/picker/picker.cmake)

add_executable(trs80
    port/main.c port/sound.c
    ${CORE}/m1.c ${CORE}/sz_z80.c ${CORE}/keys.c ${CORE}/sha1.c ${CORE}/fdc.c ${CORE}/dosclock.c
    ${DRV}/fat32.c ${DRV}/sdcard.c ${DRV}/font-5x10.c ${DRV}/font-8x10.c
    ${DRV}/keyboard.c ${DRV}/lcd.c ${DRV}/southbridge.c)
target_compile_definitions(trs80 PRIVATE M1_CPU_SZ)
# (warnings for this project's own code; the drivers have a few of their own)
set_source_files_properties(port/main.c port/sound.c ${CORE}/m1.c ${CORE}/keys.c ${CORE}/sha1.c
    ${CORE}/fdc.c ${CORE}/dosclock.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
target_include_directories(trs80 PRIVATE ${CORE} ${TS} ${DRV} ${SHELL_INC})
pico_set_program_name(trs80 "trs80")
pico_enable_stdio_uart(trs80 0)
pico_enable_stdio_usb(trs80 0)
target_link_libraries(trs80 picocalc_picker pico_stdlib hardware_gpio hardware_i2c
    hardware_spi hardware_pio hardware_powman hardware_pwm hardware_dma)
pico_add_extra_outputs(trs80)
