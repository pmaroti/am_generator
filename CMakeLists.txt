cmake_minimum_required(VERSION 3.22)

include(/srv/pico-sdk/pico_sdk_init.cmake)
project(am_generator C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()


add_executable(${PROJECT_NAME}
    main.cpp
    squarewave.pio
    ssd1306.c
)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/squarewave.pio)


# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(${PROJECT_NAME} pico_stdlib hardware_pwm hardware_pio hardware_i2c)

# create map/bin/hex/uf2 file in addition to ELF.

pico_add_extra_outputs(${PROJECT_NAME})

pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 1)