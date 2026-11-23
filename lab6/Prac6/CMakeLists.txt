# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Prac6 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(Prac6 src/main.c src/mpu6050.c src/pasos.c src/calorias.c)

pico_set_program_name(Prac6 "Prac6")
pico_set_program_version(Prac6 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(Prac6 0)
pico_enable_stdio_usb(Prac6 1)

# Add the standard library to the build
target_link_libraries(Prac6
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(Prac6 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

# Add any user requested libraries
target_link_libraries(Prac6
        hardware_spi
        hardware_i2c
        )

pico_add_extra_outputs(Prac6)

# Prueba del contador de pasos por el monitor serie (no es el programa final)
add_executable(prueba_pasos pruebas/prueba_pasos.c src/mpu6050.c src/pasos.c src/calorias.c)
target_include_directories(prueba_pasos PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(prueba_pasos pico_stdlib hardware_i2c)
pico_enable_stdio_usb(prueba_pasos 1)
pico_enable_stdio_uart(prueba_pasos 0)
pico_add_extra_outputs(prueba_pasos)

# Prueba de la pantalla OLED (no es el programa final)
add_executable(prueba_pantalla pruebas/prueba_pantalla.c src/pantalla.c)
target_include_directories(prueba_pantalla PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(prueba_pantalla pico_stdlib hardware_i2c)
pico_enable_stdio_usb(prueba_pantalla 1)
pico_enable_stdio_uart(prueba_pantalla 0)
pico_add_extra_outputs(prueba_pantalla)
