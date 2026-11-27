# Práctica 6 — CalCamino

Electrónica Digital III · Universidad de Antioquia · 2026-2

Podómetro portátil implementado en C con el Pico SDK 2.3.1 sobre Raspberry Pi Pico (RP2040), organizado mediante la estrategia de **polling + interrupciones** (sin RTOS ni esperas activas en los drivers).

## Funciones principales

- Conteo de pasos con acelerómetro MPU-6500 (módulo GY-521, `i2c1` a 100 kHz, muestreo a 200 Hz por pin `INT`).
- Estimación del gasto calórico de la caminata con la ecuación del ACSM (`VO2 = 0,1 v + 3,5` y `5 kcal / L O2`).
- Estimación opcional de la distancia recorrida en la sesión (`d = N * L`, mostrada en `m` o `km` debajo de las kilocalorías).
- Medición periódica del voltaje de alimentación (3 pilas AA alcalinas a `VSYS`, leídas por `GP29 / ADC3`) y porcentaje de batería restante.
- Visualización en pantalla OLED SSD1306 de 128×64 (`i2c0` a 400 kHz) con driver por interrupciones y refresco cada 500 ms.
- Reposo automático tras más de 60 s sin caminar desde el final de la marcha (apaga la OLED y duerme el procesador con `__wfi()` entre interrupciones), conservando la sesión y despertando al confirmar una marcha nueva.
- Configuración de peso (40–150 kg) y estatura (140–200 cm) y reinicio de sesión con tres botones (`MODO`, `MAS`, `MENOS`).

## Estructura del proyecto

- `include/` y `src/`: módulos de lógica sin SDK (`pasos`, `calorias`, `estado`, `bateria`), drivers por interrupción (`mpu6050`, `pantalla`, `botones`, `bateria_adc`), `config.h` y `main.c`.
- `pruebas/`: programas de prueba en placa (`prueba_pasos`, `prueba_pantalla`, `prueba_botones`, `prueba_bateria`) y en el PC (`prueba_estado_pc`, `prueba_bateria_pc`, `prueba_calorias_pc`).
- `Doxyfile` y `doc_c/`: configuración y documentación generada con Doxygen (`doc_c/html/index.html`).

## Compilación

En Visual Studio Code con la extensión Raspberry Pi Pico, compilar con **Compile Project** (o desde consola con CMake y Ninja). En `build/` se generan `Prac6.uf2` y los programas de prueba.

Pruebas unitarias en el PC (desde la carpeta `Prac6/`):

```bash
gcc -Wall -Wextra -std=c11 -Iinclude pruebas/prueba_estado_pc.c src/estado.c -o prueba_estado
gcc -Wall -Wextra -std=c11 -Iinclude pruebas/prueba_bateria_pc.c src/bateria.c -o prueba_bateria
gcc -Wall -Wextra -std=c11 -Iinclude pruebas/prueba_calorias_pc.c src/calorias.c -o prueba_calorias
```

Documentación Doxygen:

```bash
doxygen Doxyfile
```

> **Seguridad eléctrica:** no conectar el cable USB de la Raspberry Pi Pico mientras las tres pilas AA estén conectadas a `VSYS`.
