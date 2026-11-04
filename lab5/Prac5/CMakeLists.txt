# =============================================================================
# Práctica 5 — Medición de frecuencia y ciclo de trabajo (Electrónica Digital III)
#
# Un solo proyecto que genera CUATRO programas (.uf2):
#   v1_polling   (apps/main_polling.c) -> todo en el ciclo principal
#   v2_irq       (apps/main_irq.c)     -> todo en interrupciones
#   v3_hibrido   (apps/main_hibrido.c) -> las ISR atienden el hardware, el main procesa
#   prueba_uart  (test/prueba_uart.c)  -> prueba de componente del driver UART
#
# Los módulos comunes (include/ + src/) se compilan una vez como librería y
# los enlazan todos. Así las tres versiones comparten exactamente el mismo
# código de medición, formato, driver UART y SysTick; solo cambia el flujo,
# que está en cada main.
# =============================================================================

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# El SDK debe importarse antes de project()
include(pico_sdk_import.cmake)

project(Prac5 C CXX ASM)

pico_sdk_init()

# -----------------------------------------------------------------------------
# Parámetros de la prueba (se cambian al configurar, sin editar el código):
#   cmake -B build -DPRAC5_PERIODO_REPORTE_MS=20 -DPRAC5_CONSOLA_USB=OFF
# -----------------------------------------------------------------------------
set(PRAC5_PERIODO_REPORTE_MS 100 CACHE STRING
    "Periodo entre reportes en ms (nominal 100; máximo 134 por el SysTick)")
option(PRAC5_CONSOLA_USB
    "Consola de diagnóstico por el USB de la Pico ('e' imprime los contadores)" ON)

if(PRAC5_CONSOLA_USB)
    set(USAR_USB 1)
else()
    set(USAR_USB 0)
endif()

# Opciones de compilación iguales en todos los programas (la guía exige
# conservarlas entre versiones para que la comparación sea justa).
set(OPCIONES_COMPILACION -Wall -Wextra -O2)

# -----------------------------------------------------------------------------
# Librería común
# -----------------------------------------------------------------------------
add_library(prac5_comun STATIC
    src/medicion.c
    src/reporte.c
    src/entrada.c
    src/cola_flancos.c
    src/uart_tx_irq.c
    src/systick.c
    src/diagnostico.c
)
target_include_directories(prac5_comun PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_definitions(prac5_comun PUBLIC
    PERIODO_REPORTE_MS=${PRAC5_PERIODO_REPORTE_MS}u
    CONSOLA_USB=${USAR_USB}
)
target_link_libraries(prac5_comun PUBLIC
    pico_stdlib
    hardware_gpio
    hardware_uart
    hardware_irq
    hardware_sync
    hardware_timer
    hardware_clocks
)
target_compile_options(prac5_comun PRIVATE ${OPCIONES_COMPILACION})

# -----------------------------------------------------------------------------
# crear_programa(nombre fuente...): ejecutable con la configuración de siempre
#   - stdio por UART apagado: la UART0 es SOLO para los reportes.
#   - stdio por USB según PRAC5_CONSOLA_USB (consola de diagnóstico).
# Como prac5_comun es una librería estática, cada programa solo enlaza los
# módulos que usa: v1_polling no lleva la ISR del GPIO ni el driver UART.
# -----------------------------------------------------------------------------
function(crear_programa nombre)
    add_executable(${nombre} ${ARGN})
    target_link_libraries(${nombre} prac5_comun)
    target_compile_options(${nombre} PRIVATE ${OPCIONES_COMPILACION})
    pico_set_program_name(${nombre} "${nombre}")
    pico_set_program_version(${nombre} "0.2")
    pico_enable_stdio_uart(${nombre} 0)
    pico_enable_stdio_usb(${nombre} ${USAR_USB})
    pico_add_extra_outputs(${nombre})
endfunction()

crear_programa(v1_polling  apps/main_polling.c)
crear_programa(v2_irq      apps/main_irq.c)
crear_programa(v3_hibrido  apps/main_hibrido.c)
crear_programa(prueba_uart test/prueba_uart.c)
