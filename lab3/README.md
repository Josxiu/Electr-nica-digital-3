# Pulso de memoria — versión C (Raspberry Pi Pico SDK)

Práctica 3 del Laboratorio de Electrónica Digital III, UdeA 2026-2.
Realización en C del mismo sistema especificado en la práctica 2: mismo
hardware, mismas reglas, mismos criterios de aceptación.

## Estructura

```
c_sdk/
├── CMakeLists.txt          configuración de compilación (2 ejecutables)
├── pico_sdk_import.cmake   localización del SDK
├── Doxyfile                documentación del código
├── include/
│   ├── pines.h             asignación de GPIO del montaje
│   ├── tiempo.h            base de tiempo en ms (sustituye a millis())
│   ├── tablero.h           display de 4 dígitos multiplexado
│   ├── luces.h             5 LEDs, eco y pálpito
│   └── entradas.h          pulsadores con antirrebote
├── src/
│   ├── main.c              lógica del juego y coordinación
│   ├── tablero.c
│   ├── luces.c
│   └── entradas.c
└── test/
    └── pruebas_componente.c  banco de pruebas de los módulos de hardware
```

La separación de responsabilidades es la misma de la práctica 2: acceso al
hardware en los módulos, lógica del juego y coordinación en `main.c`.

## Asignación de GPIO

| Función | GPIO |
|---|---|
| Segmentos A–G | 19, 17, 2, 1, 0, 18, 3 |
| Comunes dígitos 1–4 | 22, 21, 20, 4 |
| LEDs de secuencia 1–4 | 6, 7, 8, 9 |
| LED 5 (tiempo) | 5 |
| Pulsadores 1–4 | 11, 12, 13, 14 |
| Pulsador inicio/reinicio | 10 |

Display de cátodo común, resistencia de 220 Ω por segmento, sin transistores.
La polaridad se configura en `include/tablero.h` (`TAB_SEG_ACTIVO_ALTO`,
`TAB_COM_ACTIVO_ALTO`).

> **Aviso:** los segmentos E y D usan GP0 y GP1, pines por defecto de UART0.
> El `CMakeLists.txt` desactiva la salida estándar por UART
> (`pico_enable_stdio_uart 0`) para que el SDK no se apropie de ellos.

## Compilación

Requiere CMake ≥ 3.13, `arm-none-eabi-gcc` y el Pico SDK ≥ 1.5.0 (por
`pico_rand`), con `PICO_SDK_PATH` apuntando al SDK.

```
cmake -B build -G Ninja
cmake --build build
```

> **Rutas con tilde.** GNU `ld` no abre los directorios `-L` cuya ruta
> contiene caracteres no ASCII. Dentro de una carpeta como
> `Electrónica Digital III` todos los objetos compilan, pero el enlace falla con
> *cannot open linker script file pico_flash_region.ld*. Los espacios no dan
> problema; solo la tilde. El `CMakeLists.txt` lo resuelve agregando la misma
> carpeta del script como ruta **relativa** al directorio de compilación, que
> no contiene la tilde. En rutas sin tildes esa línea no tiene efecto.

Salidas en el directorio de compilación:

- `pulso_memoria.uf2` — el juego
- `pruebas_componente.uf2` — el banco de pruebas

Para cargar: mantener BOOTSEL al conectar el Pico y copiar el `.uf2` a la
unidad `RPI-RP2`, o bien `picotool load -f build/pulso_memoria.uf2`.

## Monitor serie

Salida estándar por USB CDC a 115200 baudios. Comandos de prueba:

| Tecla | Efecto |
|---|---|
| `t` | activa/desactiva el límite de tiempo |
| `v` | activa/desactiva las vidas |
| `r` | reinicia la partida |
| `e` | imprime el estado actual |

## Documentación

```
doxygen Doxyfile
```

Genera `doc_c/html/index.html`.

## Mediciones del entorno (para el análisis comparativo)

Tomadas el 19-09-2026 en el equipo de desarrollo, con SDK 2.3.0 (la extensión luego fijó 2.3.1), toolchain
GCC 15.2.1 (`15_2_Rel1`), CMake 4.3.4 y Ninja 1.13.2:

| Magnitud | Valor |
|---|---|
| Configuración + compilación limpia (248 objetos) | 23,5 s |
| Recompilación tras editar `main.c` | 1,2 s |
| `pulso_memoria`: text / bss | 42 072 B / 3 620 B |
| `pulso_memoria.uf2` | 76 288 B |
| `pruebas_componente`: text / bss | 38 336 B / 3 480 B |
| Avisos del compilador con `-Wall -Wextra` | ninguno |

## Actualización conjunta de GPIO

Los puntos del código donde varios GPIO forman un mismo patrón y se escriben
en una sola operación:

| Ubicación | Operación | Pines |
|---|---|---|
| `tablero.c` → `tablero_refrescar()` | `gpio_put_masked()` | 7 segmentos + 4 comunes |
| `tablero.c` → `tablero_init()` | `gpio_init_mask()`, `gpio_set_dir_out_masked()` | los 11 del display |
| `luces.c` → `luces_mascara()` | `gpio_put_masked()` | hasta 5 LEDs |
| `luces.c` → `luces_apagar_todo()` | `gpio_clr_mask()` | los 5 LEDs |
| `entradas.c` → `entradas_muestrear()` | `gpio_get_all()` | los 5 pulsadores |

El caso crítico es el refresco del display: segmentos y común conmutan en la
misma escritura de 32 bits al registro atómico del SIO, de modo que no existe
el instante en que el dígito anterior sigue encendido mostrando ya los
segmentos del siguiente. Por eso, a diferencia de la versión Arduino, el paso
de apagado previo del común deja de ser necesario.
