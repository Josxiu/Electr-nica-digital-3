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
# =====================================================================
# Pulso de memoria - Practica 3 (C sobre el Raspberry Pi Pico SDK)
#
# Genera dos ejecutables:
#   pulso_memoria      -> el juego completo
#   pruebas_componente -> banco de pruebas de los modulos de hardware
#
# Requisitos: CMake >= 3.13, arm-none-eabi-gcc y el Pico SDK (>= 1.5.0,
# por la biblioteca pico_rand). La ruta del SDK se toma de la variable de
# entorno PICO_SDK_PATH o de la cache de CMake.
# =====================================================================

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Placa de destino. Cambiar a pico_w / pico2 si se usa otra.
set(PICO_BOARD pico CACHE STRING "Board type")

# El SDK busca Doxygen solo para sus propios docs. Con una ruta que tiene tilde
# doxygen.exe falla al generar CMakeDoxyfile.tpl y aborta la configuracion.
# Este proyecto genera su documentacion con su propio Doxyfile, asi que se
# desactiva la busqueda y se declara no encontrado.
set(CMAKE_DISABLE_FIND_PACKAGE_Doxygen TRUE)
set(DOXYGEN_FOUND FALSE)

# Localiza el SDK. Debe ir ANTES de project().
include(pico_sdk_import.cmake)

project(pulso_memoria C CXX ASM)

pico_sdk_init()

# ---------------------------------------------------------------------
# Modulos de acceso al hardware, compartidos por los dos ejecutables
# ---------------------------------------------------------------------
add_library(dispositivos STATIC
    src/tablero.c
    src/luces.c
    src/entradas.c
    src/sonido.c
)
target_include_directories(dispositivos PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(dispositivos PUBLIC pico_stdlib hardware_pwm)

# ---------------------------------------------------------------------
# Ejecutable principal: el juego
# ---------------------------------------------------------------------
add_executable(pulso_memoria src/main.c)
target_link_libraries(pulso_memoria PRIVATE dispositivos pico_stdlib pico_rand)

# Salida estandar por USB CDC.
# IMPORTANTE: el UART se desactiva a proposito. Sus pines por defecto son
# GP0 y GP1, que en este montaje son los segmentos E y D del display.
pico_enable_stdio_usb(pulso_memoria 1)
pico_enable_stdio_uart(pulso_memoria 0)

# Genera el .uf2 ademas del .elf
pico_add_extra_outputs(pulso_memoria)

# ---------------------------------------------------------------------
# Ejecutable de pruebas de componente
# ---------------------------------------------------------------------
add_executable(pruebas_componente test/pruebas_componente.c)
target_link_libraries(pruebas_componente PRIVATE dispositivos pico_stdlib)

pico_enable_stdio_usb(pruebas_componente 1)
pico_enable_stdio_uart(pruebas_componente 0)
pico_add_extra_outputs(pruebas_componente)

# ---------------------------------------------------------------------
# Rutas con caracteres no ASCII
#
# GNU ld no abre los directorios -L cuya ruta contiene caracteres no ASCII
# (aqui, la tilde de "Electronica"). El SDK genera pico_flash_region.ld dentro
# del directorio de compilacion y lo pasa con una ruta absoluta, asi que el
# enlace falla con "cannot open linker script file pico_flash_region.ld"
# aunque todos los objetos compilen.
#
# El enlazador se ejecuta desde el directorio de compilacion, de modo que
# basta con agregar la MISMA carpeta como ruta relativa: no contiene la tilde
# y el sistema operativo la resuelve contra el directorio actual. En rutas
# sin tildes esta linea no cambia nada.
# ---------------------------------------------------------------------
foreach(objetivo pulso_memoria pruebas_componente)
    target_link_options(${objetivo} PRIVATE
        "-Wl,-Lpico-sdk/src/rp2_common/pico_standard_link")
endforeach()

# ---------------------------------------------------------------------
# Avisos del compilador: se activan a proposito para que las pruebas de
# calidad de codigo tengan evidencia objetiva.
# ---------------------------------------------------------------------
foreach(objetivo dispositivos pulso_memoria pruebas_componente)
    target_compile_options(${objetivo} PRIVATE -Wall -Wextra)
endforeach()
