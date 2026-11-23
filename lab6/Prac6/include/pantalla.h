/**
 * @file pantalla.h
 * @brief Driver híbrido (Polling + Interrupciones) para pantalla OLED SSD1306 (128x64) por I2C.
 * @details Proporciona inicialización síncrona mediante polling y actualización asíncrona
 *          del framebuffer mediante interrupciones de hardware I2C del Pico SDK, empleando
 *          doble buffer para evitar parpadeos o corrupción visual durante la transmisión.
 * @defgroup pantalla_driver Driver de Pantalla OLED SSD1306
 * @{
 */

#ifndef PANTALLA_H
#define PANTALLA_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

/** @brief Resolución horizontal de la pantalla OLED en píxeles. */
#define PANTALLA_ANCHO        128

/** @brief Resolución vertical de la pantalla OLED en píxeles. */
#define PANTALLA_ALTO         64

/** @brief Dirección I2C por defecto del controlador SSD1306 (7 bits). */
#define PANTALLA_ADDR_DEFAULT 0x3C

/**
 * @brief Inicializa el periférico I2C, detecta la pantalla y configura el controlador SSD1306.
 * @details Ejecuta el escaneo de dirección y el envío de comandos de arranque de forma
 *          síncrona (polling). Posteriormente configura la FIFO y habilita la rutina
 *          de interrupción (`I2C0_IRQ` o `I2C1_IRQ`) para transferencias de pantalla.
 *
 * @param[in] i2c     Instancia del controlador I2C a utilizar (`i2c0` o `i2c1`).
 * @param[in] sda_pin Número de pin GPIO asignado a la línea SDA.
 * @param[in] scl_pin Número de pin GPIO asignado a la línea SCL.
 * @param[in] freq_hz Frecuencia del reloj I2C en Hz (ej. `400000` para Fast Mode).
 *
 * @retval true  Pantalla detectada e inicializada correctamente.
 * @retval false No se encontró ningún dispositivo I2C respondiendo en el bus.
 */
bool pantalla_iniciar(i2c_inst_t *i2c, uint sda_pin, uint scl_pin, uint32_t freq_hz);

/**
 * @brief Indica si hay una transferencia de framebuffer en curso por interrupciones.
 *
 * @retval true  El controlador I2C está transmitiendo datos hacia la pantalla.
 * @retval false El driver está inactivo y listo para iniciar un nuevo envío.
 */
bool pantalla_ocupada(void);

/**
 * @brief Consulta si ocurrió un error de transmisión (NACK o aborto) en el último envío.
 *
 * @retval true  Se detectó condición `TX_ABRT` durante la última transferencia por IRQ.
 * @retval false La última transferencia finalizó sin errores en el bus.
 */
bool pantalla_hubo_error(void);

/**
 * @brief Rellena todo el framebuffer de dibujo con un color uniforme.
 *
 * @param[in] color `0` para apagar todos los píxeles (negro), distinto de `0` para encenderlos.
 */
void pantalla_limpiar(uint8_t color);

/**
 * @brief Dibuja o borra un píxel individual en el framebuffer de dibujo.
 * @details Si las coordenadas están fuera de los límites `[0..127, 0..63]`, la llamada se ignora.
 *
 * @param[in] x     Coordenada horizontal (columna, `0` a `PANTALLA_ANCHO - 1`).
 * @param[in] y     Coordenada vertical (fila, `0` a `PANTALLA_ALTO - 1`).
 * @param[in] color `1` para encender el píxel, `0` para apagarlo.
 */
void pantalla_pixel(int x, int y, uint8_t color);

/**
 * @brief Traza una línea horizontal en el framebuffer de dibujo.
 *
 * @param[in] x     Coordenada X del punto inicial.
 * @param[in] y     Coordenada Y de la línea.
 * @param[in] w     Longitud de la línea en píxeles hacia la derecha.
 * @param[in] color `1` para encender píxeles, `0` para apagarlos.
 */
void pantalla_hline(int x, int y, int w, uint8_t color);

/**
 * @brief Traza una línea vertical en el framebuffer de dibujo.
 *
 * @param[in] x     Coordenada X de la línea.
 * @param[in] y     Coordenada Y del punto inicial.
 * @param[in] h     Longitud de la línea en píxeles hacia abajo.
 * @param[in] color `1` para encender píxeles, `0` para apagarlos.
 */
void pantalla_vline(int x, int y, int h, uint8_t color);

/**
 * @brief Dibuja el contorno de un rectángulo en el framebuffer de dibujo.
 *
 * @param[in] x     Coordenada X de la esquina superior izquierda.
 * @param[in] y     Coordenada Y de la esquina superior izquierda.
 * @param[in] w     Ancho del rectángulo en píxeles.
 * @param[in] h     Alto del rectángulo en píxeles.
 * @param[in] color `1` para encender píxeles, `0` para apagarlos.
 */
void pantalla_rect(int x, int y, int w, int h, uint8_t color);

/**
 * @brief Dibuja un rectángulo relleno en el framebuffer de dibujo.
 *
 * @param[in] x     Coordenada X de la esquina superior izquierda.
 * @param[in] y     Coordenada Y de la esquina superior izquierda.
 * @param[in] w     Ancho del rectángulo en píxeles.
 * @param[in] h     Alto del rectángulo en píxeles.
 * @param[in] color `1` para encender píxeles, `0` para apagarlos.
 */
void pantalla_fill_rect(int x, int y, int w, int h, uint8_t color);

/**
 * @brief Renderiza una cadena de texto ASCII usando la fuente bitmap interna de 8x8.
 * @details Los caracteres fuera del rango imprimible ASCII (`32..126`) se sustituyen por `'?'`.
 *
 * @param[in] txt    Puntero a la cadena de texto terminada en carácter nulo (`'\0'`).
 * @param[in] x      Coordenada X de inicio del primer carácter.
 * @param[in] y      Coordenada Y superior del texto.
 * @param[in] escala Factor de ampliación entero (`1` = 8x8 px, `2` = 16x16 px, `3` = 24x24 px, etc.).
 */
void pantalla_texto(const char *txt, int x, int y, uint8_t escala);

/**
 * @brief Dibuja un icono gráfico de batería con barra de carga proporcional.
 *
 * @param[in] x   Coordenada X de la esquina superior izquierda del icono (22x10 px total).
 * @param[in] y   Coordenada Y de la esquina superior izquierda del icono.
 * @param[in] pct Nivel de carga de la batería en porcentaje (`0` a `100`). Valores superiores se saturan a `100`.
 */
void pantalla_dibujar_bateria(int x, int y, uint8_t pct);

/**
 * @brief Inicia de forma no bloqueante el envío del framebuffer actual a la pantalla vía IRQ.
 * @details Copia el buffer de dibujo al buffer de transmisión y habilita las interrupciones
 *          `TX_EMPTY`, `STOP_DET` y `TX_ABRT` para transferir ventana de direccionamiento
 *          y los 1024 bytes de píxeles en segundo plano.
 *
 * @retval true  Transferencia iniciada correctamente.
 * @retval false La pantalla sigue ocupada transmitiendo el cuadro anterior; petición descartada.
 */
bool pantalla_mostrar(void);

/**
 * @brief Apaga la pantalla (modo dormir y bomba de carga apagada), sin bloquear.
 * @details Envía los comandos por interrupciones; la RAM de la pantalla se conserva.
 * @retval true  Envío iniciado.
 * @retval false La pantalla sigue ocupada; hay que intentarlo de nuevo.
 */
bool pantalla_apagar(void);

/**
 * @brief Enciende la pantalla después de pantalla_apagar(), sin bloquear.
 * @retval true  Envío iniciado.
 * @retval false La pantalla sigue ocupada; hay que intentarlo de nuevo.
 */
bool pantalla_encender(void);

#endif // PANTALLA_H

/** @} */ // end of pantalla_driver