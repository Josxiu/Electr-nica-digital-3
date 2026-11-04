# Práctica 5 — Medición de frecuencia y ciclo de trabajo

Electrónica Digital III · UdeA · 2026-2

La Pico recibe una señal cuadrada, mide su **frecuencia** y su **ciclo de trabajo** y envía el resultado al PC por una **UART física**, 10 veces por segundo. El mismo instrumento está implementado de tres formas para comparar los flujos de ejecución:

| Versión | Programa | Flancos | Ritmo de reportes | Envío UART |
|---|---|---|---|---|
| 1. Polling | `apps/main_polling.c` → `v1_polling.uf2` | `while` lee el pin | `while` consulta COUNTFLAG del SysTick | `uart_write_blocking` del SDK |
| 2. IRQs | `apps/main_irq.c` → `v2_irq.uf2` | ISR GPIO registra el flanco | ISR SysTick calcula y envía | driver propio `uart_tx_irq` |
| 3. Polling + IRQs | `apps/main_hibrido.c` → `v3_hibrido.uf2` | ISR GPIO encola, main procesa | ISR SysTick avisa, main calcula | driver propio `uart_tx_irq` |

Las tres usan el mismo driver de SysTick (el de la práctica 4) con el mismo periodo: en polling sin interrupción (se consulta COUNTFLAG) y en las otras dos con interrupción.

## Estructura

```
Prac5/
├── CMakeLists.txt         genera v1_polling, v2_irq, v3_hibrido y prueba_uart
├── apps/                  un main por versión (lo único que cambia es el flujo)
│   ├── main_polling.c
│   ├── main_irq.c
│   └── main_hibrido.c
├── include/  src/         módulos compartidos (.h / .c)
│   ├── config            pines, baudios, periodo de reporte, prioridades
│   ├── entrada           pin de entrada + ISR de flancos
│   ├── medicion          cálculo de f y D (sin hardware)
│   ├── reporte           formato "n,Δt,f,D,estado" (sin hardware)
│   ├── cola_flancos      cola ISR -> main (solo la versión híbrida)
│   ├── uart_tx_irq       driver propio de TX por interrupciones
│   ├── systick           driver del SysTick (reutilizado de la práctica 4)
│   └── diagnostico       consola USB con los contadores de la prueba
└── test/
    ├── prueba_uart.c      prueba del driver UART en la Pico
    └── pruebas_pc.c       pruebas de medicion y reporte en el PC
```

Como los módulos se compilan como librería estática, cada programa solo enlaza lo que usa: `v1_polling` no lleva la ISR del GPIO, la cola ni el driver UART.

## Montaje

| Señal | GPIO | Pin físico | Conexión |
|---|---|---|---|
| Entrada | GP15 | 20 | Salida del generador (0 a 3,3 V), opcional 1 kΩ en serie |
| UART0 TX | GP0 | 1 | RX del adaptador USB-UART (o GP5 de una Pico con *debugprobe*) |
| GND | GND | 3, 8, ... | GND del generador y GND del adaptador |

- El adaptador debe trabajar a **3,3 V**. No conectar su VCC a la Pico.
- Antes de conectar el generador, verificar con el osciloscopio que la señal esté entre 0 y 3,3 V.
- Los pines se cambian en `common/config.h`.

## Herramientas

- VS Code con la extensión Raspberry Pi Pico
- Pico SDK 2.3.1, toolchain ARM GNU 15.2.Rel1, picotool 2.3.1
- `PICO_BOARD = pico` (RP2040), reloj del sistema por defecto (125 MHz)
- Opciones de compilación (iguales en las tres versiones): `-Wall -Wextra -O2`

## Compilar y cargar

1. Abrir la carpeta `Prac5` en VS Code y compilar con **Compile Project**.
2. En `build/` quedan `v1_polling.uf2`, `v2_irq.uf2`, `v3_hibrido.uf2` y `prueba_uart.uf2`.
3. Conectar la Pico con BOOTSEL presionado y copiar el `.uf2` de la versión que se quiere probar a la unidad `RPI-RP2`.

Por línea de comandos:

```
cmake -G Ninja -S . -B build -DPICO_BOARD=pico
ninja -C build
```

## Uso

**Reportes (UART del adaptador):** abrir el puerto COM del adaptador a **115200 8N1**, sin control de flujo. Cada 100 ms llega una línea:

```
0,0,NA,NA,ESPERANDO
1,100,1000.000,25.0,OK
2,100,1000.000,25.0,OK
...
40,100,NA,NA,SIN_TRANSICIONES
```

**Consola de diagnóstico (USB de la Pico):** abrir el otro puerto COM (el de la Pico) y enviar la letra `e`. Se imprimen los contadores de la prueba: reportes programados, omitidos, descartados, flancos perdidos, desbordes de la cola, etc. Así se consulta el conteo de omisiones al terminar cada prueba. Esta consola nunca se mezcla con las líneas de datos.

**Opciones de la prueba** (se pasan al configurar, sin editar el código; el nombre del `.uf2` no cambia, así que conviene usar una carpeta de build por configuración):

| Opción de CMake | Valor por defecto | Uso |
|---|---|---|
| `PRAC5_PERIODO_REPORTE_MS` | `100` | Tasa de reportes para la prueba de efecto de la comunicación (1 a 134 ms). |
| `PRAC5_CONSOLA_USB` | `ON` | `OFF` quita la pila USB (y sus interrupciones). Útil para medir el límite de polling sin esa interferencia; los contadores se leen entonces con el depurador. |

```
cmake -G Ninja -S . -B build_20ms -DPICO_BOARD=pico -DPRAC5_PERIODO_REPORTE_MS=20
ninja -C build_20ms
```

## Pruebas de componente

- **Medición y reporte (en el PC, sin Pico):**
  ```
  gcc -std=c11 -Wall -Wextra -I include test/pruebas_pc.c src/medicion.c src/reporte.c -o pruebas_pc
  ./pruebas_pc
  ```
- **Driver UART (en la Pico):** cargar `prueba_uart.uf2`. Cada 2 s envía líneas de distintas longitudes, líneas seguidas sin esperar y una ráfaga que no cabe en el buffer. El resultado se ve en la consola USB y lo transmitido en la terminal del adaptador; ninguna línea debe llegar cortada.

## Decisiones de diseño (resumen)

- **Método de medición:** se toma el instante de cada flanco con el timer de 1 µs (`time_us_32()`) y, en cada ventana de reporte, se promedian todos los periodos completos: `f = N / ΣT`, `D = ΣtH / ΣT`. Promediar reduce el error por la resolución de 1 µs.
- **Representación numérica:** enteros en punto fijo (mHz y décimas de %), sin `float`. El M0+ no tiene FPU y así el redondeo es exacto.
- **Driver UART:** buffer circular de 512 B, todo o nada por línea (nunca salen líneas cortadas), interrupción de TX activa solo mientras hay datos y arranque con `irq_set_pending`. Ninguna función espera a la UART.
- **SysTick** (driver de la práctica 4) para el ritmo de reportes en las tres versiones: en polling se consulta COUNTFLAG y en las otras dos se usa su interrupción. El timer de 1 µs del RP2040 queda solo para los instantes de los flancos y para Δt.
- **Tramo ciego en polling:** mientras el ciclo arma y envía un reporte no lee el pin. Si en ese tramo cabe un pulso completo, sus dos flancos pasan sin verse y el periodo siguiente mediría el doble. Por eso, al terminar el reporte se compara la duración del tramo con el pulso más corto de la ventana anterior y, si cabe, se descarta el periodo en curso (`medicion_revisar_tramo_ciego`). Se pierde como mucho un periodo por ventana y queda contado en `cortes_polling`.
- **Prioridades:** GPIO (0x00) > UART (0x40) > SysTick (0x80). Un flanco nunca espera a un reporte.
- Un solo núcleo; sin DMA ni PIO.

## Documentación

`doxygen Doxyfile` genera la documentación en `doc_c/html` (abrir `index.html`).
