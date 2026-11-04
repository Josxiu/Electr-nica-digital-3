# Octavia — Práctica 4 (Electrónica Digital III, UdeA)

Instrumento musical con driver propio del SysTick y DAC R-2R de 8 bits en la Raspberry Pi Pico (RP2040).

## Herramientas

- VS Code con la extensión Raspberry Pi Pico
- Pico SDK 2.3.1
- Toolchain ARM GNU 15.2.Rel1
- picotool 2.3.1
- `PICO_BOARD = pico` (RP2040)

## Compilar y cargar

1. Abrir la carpeta `Prac4` en VS Code (la extensión configura CMake y Ninja).
2. Compilar con **Compile Project**. Se genera `build/Prac4.uf2`.
3. Conectar la Pico con el botón BOOTSEL presionado y copiar `Prac4.uf2` a la unidad `RPI-RP2`.

Por línea de comandos (con el SDK y el toolchain en el PATH):

```
cmake -G Ninja -S . -B build -DPICO_BOARD=pico
ninja -C build
```

## Uso

- GP2–GP8: notas do a si. GP9 / GP10: bajar / subir octava (3 a 5). LED de octava en GP19–GP21.
- DAC R-2R en GP11 (LSB) a GP18 (MSB).
- Por el puerto serie USB: `i` activa o desactiva la interpolación; `f` cuenta las interrupciones del SysTick durante 1 s.
- Número de voces: `SINT_VOCES` en `include/sintetizador.h` (2 en la versión final). Para cambiarlo, editar y recompilar.

## Documentación

`doxygen Doxyfile` genera la documentación en `doc_c/html` (abrir `index.html`).
