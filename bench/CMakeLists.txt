# z80bench -- the two Z80 cores timed on the PicoCalc (Pico 2 W)
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(z80bench C CXX ASM)
pico_sdk_init()

set(TS ${CMAKE_CURRENT_LIST_DIR}/../../btkbd/modules/picocalc-text-starter)
set(SHELL_INC ${CMAKE_CURRENT_LIST_DIR}/../../btkbd/modules/picocalc-shell/include)
set(CORE ${CMAKE_CURRENT_LIST_DIR}/../core)
set(DRV ${TS}/drivers)

set(SZ_RENAME z80_init=sz_init z80_step=sz_step z80_debug_output=sz_dbg z80_gen_nmi=sz_nmi z80_gen_int=sz_int)
add_library(m1sz OBJECT ${CORE}/m1.c ${CORE}/sz_z80.c)
target_compile_definitions(m1sz PRIVATE M1_CPU_SZ ${SZ_RENAME}
    m1_init=m1sz_init m1_run=m1sz_run m1_vram=m1sz_vram m1_instructions=m1sz_instructions m1_key=m1sz_key m1_size=m1sz_size)
add_library(m1ch OBJECT ${CORE}/m1.c)
target_compile_definitions(m1ch PRIVATE M1_CPU_CHIPS
    m1_init=m1ch_init m1_run=m1ch_run m1_vram=m1ch_vram m1_instructions=m1ch_instructions m1_key=m1ch_key m1_size=m1ch_size)
foreach(t m1sz m1ch)
    target_include_directories(${t} PRIVATE ${CORE})
endforeach()

add_executable(z80bench main.c $<TARGET_OBJECTS:m1sz> $<TARGET_OBJECTS:m1ch>
    ${DRV}/fat32.c ${DRV}/sdcard.c ${DRV}/font-5x10.c ${DRV}/font-8x10.c
    ${DRV}/keyboard.c ${DRV}/lcd.c ${DRV}/southbridge.c)
target_include_directories(z80bench PRIVATE ${CORE} ${TS} ${DRV} ${SHELL_INC})
pico_enable_stdio_uart(z80bench 0)
pico_enable_stdio_usb(z80bench 0)
target_link_libraries(z80bench pico_stdlib hardware_gpio hardware_i2c hardware_spi hardware_pio hardware_clocks)
pico_add_extra_outputs(z80bench)
