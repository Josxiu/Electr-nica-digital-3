/**
 * @file pantalla.c
 * @brief Implementación del driver OLED SSD1306 con arquitectura híbrida Polling + IRQ.
 * @details Gestiona la máquina de estados de transmisión I2C a nivel de registros (`i2c_hw_t`),
 *          la gestión de doble framebuffer en formato `MONO_VLSB` y el renderizado de
 *          primitivas gráficas y fuente ASCII 8x8.
 */

#include "pantalla.h"

#include <string.h>
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/i2c.h"

/** @brief Número de páginas verticales de 8 bits en la memoria GDDRAM del SSD1306. */
#define PANTALLA_PAGES      (PANTALLA_ALTO / 8)

/** @brief Tamaño total del framebuffer monocromático en bytes (1024 bytes para 128x64). */
#define PANTALLA_FB_SIZE    (PANTALLA_ANCHO * PANTALLA_PAGES)

/**
 * @enum pantalla_tx_state_t
 * @brief Estados internos de la máquina de estados manejada por la ISR de I2C.
 */
typedef enum {
    PANTALLA_STATE_IDLE = 0,   /**< Bus libre; sin transferencia activa de pantalla. */
    PANTALLA_STATE_CMD_WINDOW, /**< Fase 1: enviando comandos de ventana de columnas y páginas. */
    PANTALLA_STATE_DATA_FB     /**< Fase 2: enviando byte de control 0x40 + 1024 bytes de píxeles. */
} pantalla_tx_state_t;

/** @brief Puntero a la instancia hardware I2C activa. */
static i2c_inst_t *pantalla_i2c = NULL;

/** @brief Dirección I2C detectada de la pantalla OLED. */
static uint8_t pantalla_addr = PANTALLA_ADDR_DEFAULT;

/** @brief Buffer frontal donde las primitivas gráficas escriben píxeles. */
static uint8_t draw_fb[PANTALLA_FB_SIZE];

/** @brief Buffer trasero de transmisión (`tx_buf[0] = 0x40` + `1024` bytes de datos). */
static uint8_t tx_buf[1 + PANTALLA_FB_SIZE];

/** @brief Secuencia de comandos SSD1306 para fijar la ventana completa de escritura GDDRAM. */
static const uint8_t cmd_window[] = {
    0x00,                        // Byte de control: flujo de comandos
    0x21, 0, PANTALLA_ANCHO - 1, // Configurar dirección de columna (0..127)
    0x22, 0, PANTALLA_PAGES - 1  // Configurar dirección de página (0..7)
};

/** @brief Estado actual de la transferencia asíncrona (compartido con la ISR). */
static volatile pantalla_tx_state_t pantalla_state = PANTALLA_STATE_IDLE;

/** @brief Bandera de error activada si ocurre `TX_ABRT` durante la ISR. */
static volatile bool pantalla_error = false;

/** @brief Puntero al arreglo en curso que está siendo transmitido por la ISR. */
static const uint8_t *volatile current_ptr = NULL;

/** @brief Longitud total en bytes de la transferencia en curso. */
static volatile size_t current_len = 0;

/** @brief Índice del siguiente byte a cargar en la FIFO de transmisión I2C. */
static volatile size_t tx_idx = 0;

/**
 * @brief Tabla de mapas de bits para caracteres ASCII imprimibles (códigos 32 al 126).
 * @details Cada carácter ocupa 8 columnas de 8 bits, con el bit menos significativo (LSB)
 *          representando el píxel superior (`y = 0`).
 */
static const uint8_t FONT_8X8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' ' (32)
    {0x00,0x00,0x5F,0x00,0x00,0x00,0x00,0x00}, // '!'
    {0x00,0x07,0x00,0x07,0x00,0x00,0x00,0x00}, // '"'
    {0x14,0x7F,0x14,0x7F,0x14,0x00,0x00,0x00}, // '#'
    {0x24,0x2A,0x7F,0x2A,0x12,0x00,0x00,0x00}, // '$'
    {0x23,0x13,0x08,0x64,0x62,0x00,0x00,0x00}, // '%'
    {0x36,0x49,0x55,0x22,0x50,0x00,0x00,0x00}, // '&'
    {0x00,0x05,0x03,0x00,0x00,0x00,0x00,0x00}, // '''
    {0x00,0x1C,0x22,0x41,0x00,0x00,0x00,0x00}, // '('
    {0x00,0x41,0x22,0x1C,0x00,0x00,0x00,0x00}, // ')'
    {0x08,0x2A,0x1C,0x2A,0x08,0x00,0x00,0x00}, // '*'
    {0x08,0x08,0x3E,0x08,0x08,0x00,0x00,0x00}, // '+'
    {0x00,0x50,0x30,0x00,0x00,0x00,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x00}, // '-'
    {0x00,0x60,0x60,0x00,0x00,0x00,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02,0x00,0x00,0x00}, // '/'
    {0x00,0x3E,0x51,0x49,0x45,0x3E,0x00,0x00}, // '0' (48)
    {0x00,0x00,0x42,0x7F,0x40,0x00,0x00,0x00}, // '1'
    {0x00,0x42,0x61,0x51,0x49,0x46,0x00,0x00}, // '2'
    {0x00,0x21,0x41,0x45,0x4B,0x31,0x00,0x00}, // '3'
    {0x00,0x18,0x14,0x12,0x7F,0x10,0x00,0x00}, // '4'
    {0x00,0x27,0x45,0x45,0x45,0x39,0x00,0x00}, // '5'
    {0x00,0x3C,0x4A,0x49,0x49,0x30,0x00,0x00}, // '6'
    {0x00,0x01,0x71,0x09,0x05,0x03,0x00,0x00}, // '7'
    {0x00,0x36,0x49,0x49,0x49,0x36,0x00,0x00}, // '8'
    {0x00,0x06,0x49,0x49,0x29,0x1E,0x00,0x00}, // '9' (57)
    {0x00,0x36,0x36,0x00,0x00,0x00,0x00,0x00}, // ':'
    {0x00,0x56,0x36,0x00,0x00,0x00,0x00,0x00}, // ';'
    {0x00,0x08,0x14,0x22,0x41,0x00,0x00,0x00}, // '<'
    {0x14,0x14,0x14,0x14,0x14,0x00,0x00,0x00}, // '='
    {0x41,0x22,0x14,0x08,0x00,0x00,0x00,0x00}, // '>'
    {0x02,0x01,0x51,0x09,0x06,0x00,0x00,0x00}, // '?'
    {0x32,0x49,0x79,0x41,0x3E,0x00,0x00,0x00}, // '@'
    {0x00,0x7E,0x11,0x11,0x11,0x7E,0x00,0x00}, // 'A' (65)
    {0x00,0x7F,0x49,0x49,0x49,0x36,0x00,0x00}, // 'B'
    {0x00,0x3E,0x41,0x41,0x41,0x22,0x00,0x00}, // 'C'
    {0x00,0x7F,0x41,0x41,0x22,0x1C,0x00,0x00}, // 'D'
    {0x00,0x7F,0x49,0x49,0x49,0x41,0x00,0x00}, // 'E'
    {0x00,0x7F,0x09,0x09,0x09,0x01,0x00,0x00}, // 'F'
    {0x00,0x3E,0x41,0x49,0x49,0x7A,0x00,0x00}, // 'G'
    {0x00,0x7F,0x08,0x08,0x08,0x7F,0x00,0x00}, // 'H'
    {0x00,0x00,0x41,0x7F,0x41,0x00,0x00,0x00}, // 'I'
    {0x00,0x20,0x40,0x41,0x3F,0x01,0x00,0x00}, // 'J'
    {0x00,0x7F,0x08,0x14,0x22,0x41,0x00,0x00}, // 'K'
    {0x00,0x7F,0x40,0x40,0x40,0x40,0x00,0x00}, // 'L'
    {0x00,0x7F,0x02,0x0C,0x02,0x7F,0x00,0x00}, // 'M'
    {0x00,0x7F,0x04,0x08,0x10,0x7F,0x00,0x00}, // 'N'
    {0x00,0x3E,0x41,0x41,0x41,0x3E,0x00,0x00}, // 'O'
    {0x00,0x7F,0x09,0x09,0x09,0x06,0x00,0x00}, // 'P'
    {0x00,0x3E,0x41,0x51,0x21,0x5E,0x00,0x00}, // 'Q'
    {0x00,0x7F,0x09,0x19,0x29,0x46,0x00,0x00}, // 'R'
    {0x00,0x46,0x49,0x49,0x49,0x31,0x00,0x00}, // 'S'
    {0x00,0x01,0x01,0x7F,0x01,0x01,0x00,0x00}, // 'T'
    {0x00,0x3F,0x40,0x40,0x40,0x3F,0x00,0x00}, // 'U'
    {0x00,0x1F,0x20,0x40,0x20,0x1F,0x00,0x00}, // 'V'
    {0x00,0x3F,0x40,0x38,0x40,0x3F,0x00,0x00}, // 'W'
    {0x00,0x63,0x14,0x08,0x14,0x63,0x00,0x00}, // 'X'
    {0x00,0x07,0x08,0x70,0x08,0x07,0x00,0x00}, // 'Y'
    {0x00,0x61,0x51,0x49,0x45,0x43,0x00,0x00}, // 'Z' (90)
    {0x00,0x00,0x7F,0x41,0x41,0x00,0x00,0x00}, // '['
    {0x02,0x04,0x08,0x10,0x20,0x00,0x00,0x00}, // '\'
    {0x00,0x41,0x41,0x7F,0x00,0x00,0x00,0x00}, // ']'
    {0x04,0x02,0x01,0x02,0x04,0x00,0x00,0x00}, // '^'
    {0x40,0x40,0x40,0x40,0x40,0x00,0x00,0x00}, // '_'
    {0x00,0x01,0x02,0x04,0x00,0x00,0x00,0x00}, // '`'
    {0x00,0x20,0x54,0x54,0x54,0x78,0x00,0x00}, // 'a' (97)
    {0x00,0x7F,0x48,0x44,0x44,0x38,0x00,0x00}, // 'b'
    {0x00,0x38,0x44,0x44,0x44,0x20,0x00,0x00}, // 'c'
    {0x00,0x38,0x44,0x44,0x48,0x7F,0x00,0x00}, // 'd'
    {0x00,0x38,0x54,0x54,0x54,0x18,0x00,0x00}, // 'e'
    {0x00,0x08,0x7E,0x09,0x01,0x02,0x00,0x00}, // 'f'
    {0x00,0x0C,0x52,0x52,0x52,0x3E,0x00,0x00}, // 'g'
    {0x00,0x7F,0x08,0x04,0x04,0x78,0x00,0x00}, // 'h'
    {0x00,0x00,0x44,0x7D,0x40,0x00,0x00,0x00}, // 'i'
    {0x00,0x20,0x40,0x44,0x3D,0x00,0x00,0x00}, // 'j'
    {0x00,0x7F,0x10,0x28,0x44,0x00,0x00,0x00}, // 'k'
    {0x00,0x00,0x41,0x7F,0x40,0x00,0x00,0x00}, // 'l'
    {0x00,0x7C,0x04,0x18,0x04,0x78,0x00,0x00}, // 'm'
    {0x00,0x7C,0x08,0x04,0x04,0x78,0x00,0x00}, // 'n'
    {0x00,0x38,0x44,0x44,0x44,0x38,0x00,0x00}, // 'o'
    {0x00,0x7C,0x14,0x14,0x14,0x08,0x00,0x00}, // 'p'
    {0x00,0x08,0x14,0x14,0x18,0x7C,0x00,0x00}, // 'q'
    {0x00,0x7C,0x08,0x04,0x04,0x08,0x00,0x00}, // 'r'
    {0x00,0x48,0x54,0x54,0x54,0x20,0x00,0x00}, // 's'
    {0x00,0x04,0x3F,0x44,0x40,0x20,0x00,0x00}, // 't'
    {0x00,0x3C,0x40,0x40,0x20,0x7C,0x00,0x00}, // 'u'
    {0x00,0x1C,0x20,0x40,0x20,0x1C,0x00,0x00}, // 'v'
    {0x00,0x3C,0x40,0x30,0x40,0x3C,0x00,0x00}, // 'w'
    {0x00,0x44,0x28,0x10,0x28,0x44,0x00,0x00}, // 'x'
    {0x00,0x0C,0x50,0x50,0x50,0x3C,0x00,0x00}, // 'y'
    {0x00,0x44,0x64,0x54,0x4C,0x44,0x00,0x00}, // 'z' (122)
    {0x00,0x08,0x36,0x41,0x00,0x00,0x00,0x00}, // '{'
    {0x00,0x00,0x7F,0x00,0x00,0x00,0x00,0x00}, // '|'
    {0x00,0x41,0x36,0x08,0x00,0x00,0x00,0x00}, // '}'
    {0x08,0x04,0x08,0x10,0x08,0x00,0x00,0x00}  // '~' (126)
};

/**
 * @brief Rutina de Servicio de Interrupción (ISR) del controlador I2C.
 * @details Atiende tres eventos de hardware:
 *          1. `TX_ABRT`: Limpia el aborto, deshabilita interrupciones y marca error.
 *          2. `TX_EMPTY`: Rellena la FIFO de hardware hasta llenarla o alcanzar `current_len`,
 *             añadiendo el bit `STOP` en el último byte.
 *          3. `STOP_DET`: Transiciona de la fase de comandos de ventana (`PANTALLA_STATE_CMD_WINDOW`)
 *             a la fase de datos (`PANTALLA_STATE_DATA_FB`), o finaliza dejando el estado en `IDLE`.
 */
static void pantalla_i2c_isr(void) {
    i2c_hw_t *hw = i2c_get_hw(pantalla_i2c);
    uint32_t stat = hw->intr_stat;

    // 1. Error o NACK en el bus
    if (stat & I2C_IC_INTR_STAT_R_TX_ABRT_BITS) {
        (void)hw->clr_tx_abrt;
        hw->intr_mask = 0;
        pantalla_error = true;
        pantalla_state = PANTALLA_STATE_IDLE;
        return;
    }

    // 2. FIFO de transmisión con espacio disponible (TX_EMPTY)
    if (stat & I2C_IC_INTR_STAT_R_TX_EMPTY_BITS) {
        while ((tx_idx < current_len) && (hw->status & I2C_IC_STATUS_TFNF_BITS)) {
            uint32_t data_cmd = current_ptr[tx_idx++];
            if (tx_idx == current_len) {
                data_cmd |= I2C_IC_DATA_CMD_STOP_BITS; // Generar STOP en el último byte
            }
            hw->data_cmd = data_cmd;
        }

        // Si ya se cargaron todos los bytes en la FIFO, deshabilitar TX_EMPTY
        // y esperar la confirmación física de STOP_DET
        if (tx_idx >= current_len) {
            hw->intr_mask &= ~I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
        }
    }

    // 3. Condición STOP completada en el bus
    if (stat & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
        (void)hw->clr_stop_det;

        if (pantalla_state == PANTALLA_STATE_CMD_WINDOW) {
            // Fase 1 completada: pasar a Fase 2 (enviar 0x40 + 1024 bytes de framebuffer)
            pantalla_state = PANTALLA_STATE_DATA_FB;
            current_ptr = tx_buf;
            current_len = sizeof(tx_buf);
            tx_idx = 0;
            hw->intr_mask |= I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
        } else if (pantalla_state == PANTALLA_STATE_DATA_FB) {
            // Fase 2 completada: transmisión finalizada
            hw->intr_mask = 0;
            pantalla_state = PANTALLA_STATE_IDLE;
        }
    }
}

/**
 * @brief Envía una lista de comandos de configuración al SSD1306 mediante polling bloqueante.
 *
 * @param[in] cmds Puntero al arreglo de bytes de comando.
 * @param[in] len  Cantidad de comandos a transmitir.
 */
static void pantalla_enviar_comandos_polling(const uint8_t *cmds, size_t len) {
    uint8_t pkt[2];
    pkt[0] = 0x00;
    for (size_t i = 0; i < len; i++) {
        pkt[1] = cmds[i];
        i2c_write_blocking(pantalla_i2c, pantalla_addr, pkt, 2, false);
    }
}

/**
 * @brief Escanea el bus I2C por polling para detectar la dirección de la pantalla OLED.
 * @details Verifica primero las direcciones estándar `0x3C` y `0x3D`, y en caso de no
 *          hallar respuesta realiza un barrido completo entre `0x08` y `0x77`.
 *
 * @retval true  Dirección detectada y almacenada en `pantalla_addr`.
 * @retval false Ningún esclavo I2C respondió en el bus.
 */
static bool pantalla_detectar_direccion_polling(void) {
    uint8_t dummy;
    const uint8_t candidatas[] = {0x3C, 0x3D};
    for (size_t i = 0; i < sizeof(candidatas); i++) {
        if (i2c_read_blocking(pantalla_i2c, candidatas[i], &dummy, 1, false) >= 0) {
            pantalla_addr = candidatas[i];
            return true;
        }
    }
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_read_blocking(pantalla_i2c, addr, &dummy, 1, false) >= 0) {
            pantalla_addr = addr;
            return true;
        }
    }
    return false;
}

bool pantalla_iniciar(i2c_inst_t *i2c, uint sda_pin, uint scl_pin, uint32_t freq_hz) {
    pantalla_i2c = i2c;
    tx_buf[0] = 0x40; // Byte de control de datos GDDRAM

    i2c_init(pantalla_i2c, freq_hz);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);

    // 1. Detección e inicialización por POLLING
    if (!pantalla_detectar_direccion_polling()) {
        return false;
    }

    static const uint8_t init_cmds[] = {
        0xAE,             // Pantalla apagada
        0xD5, 0x80,       // Reloj
        0xA8, 0x3F,       // 64 filas
        0xD3, 0x00,       // Sin desplazamiento
        0x40,             // Empezar en fila 0
        0x8D, 0x14,       // Bomba de carga interna encendida
        0x20, 0x00,       // Direccionamiento horizontal
        0xA1, 0xC8,       // Orientación normal
        0xDA, 0x12,       // Configuración de pines COM
        0x81, 0xCF,       // Contraste
        0xD9, 0xF1,       // Precarga
        0xDB, 0x40,       // Nivel VCOMH
        0xA4,             // Mostrar contenido de la RAM
        0xA6,             // Sin invertir colores
        0xAF              // Pantalla encendida
    };

    pantalla_enviar_comandos_polling(init_cmds, sizeof(init_cmds));

    // 2. Configurar controlador de INTERRUPCIONES para el refresco de pantalla
    i2c_hw_t *hw = i2c_get_hw(pantalla_i2c);
    hw->intr_mask = 0;
    (void)hw->clr_intr;
    hw->tx_tl = 4; // Disparar TX_EMPTY cuando queden <= 4 bytes en la FIFO (de 16)

    uint irq_num = I2C0_IRQ + i2c_get_index(pantalla_i2c);
    irq_set_exclusive_handler(irq_num, pantalla_i2c_isr);
    irq_set_enabled(irq_num, true);

    pantalla_limpiar(0);
    pantalla_mostrar();
    pantalla_esperar_lista();
    return true;
}

bool pantalla_ocupada(void) {
    return pantalla_state != PANTALLA_STATE_IDLE;
}

bool pantalla_hubo_error(void) {
    return pantalla_error;
}

void pantalla_esperar_lista(void) {
    while (pantalla_state != PANTALLA_STATE_IDLE) {
        tight_loop_contents();
    }
}

void pantalla_limpiar(uint8_t color) {
    memset(draw_fb, color ? 0xFF : 0x00, PANTALLA_FB_SIZE);
}

void pantalla_pixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= PANTALLA_ANCHO || y < 0 || y >= PANTALLA_ALTO) return;
    uint16_t idx = (uint16_t)x + ((uint16_t)(y >> 3) * PANTALLA_ANCHO);
    uint8_t mask = (uint8_t)(1u << (y & 7));
    if (color) {
        draw_fb[idx] |= mask;
    } else {
        draw_fb[idx] &= (uint8_t)~mask;
    }
}

void pantalla_hline(int x, int y, int w, uint8_t color) {
    for (int i = 0; i < w; i++) {
        pantalla_pixel(x + i, y, color);
    }
}

void pantalla_vline(int x, int y, int h, uint8_t color) {
    for (int i = 0; i < h; i++) {
        pantalla_pixel(x, y + i, color);
    }
}

void pantalla_rect(int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    pantalla_hline(x, y, w, color);
    pantalla_hline(x, y + h - 1, w, color);
    pantalla_vline(x, y, h, color);
    pantalla_vline(x + w - 1, y, h, color);
}

void pantalla_fill_rect(int x, int y, int w, int h, uint8_t color) {
    for (int dy = 0; dy < h; dy++) {
        pantalla_hline(x, y + dy, w, color);
    }
}

void pantalla_texto(const char *txt, int x, int y, uint8_t escala) {
    if (!txt || escala == 0) return;
    while (*txt) {
        uint8_t c = (uint8_t)(*txt++);
        if (c < 32 || c > 126) c = '?';
        const uint8_t *glyph = FONT_8X8[c - 32];

        for (int px = 0; px < 8; px++) {
            uint8_t col = glyph[px];
            for (int py = 0; py < 8; py++) {
                if (col & (1u << py)) {
                    if (escala == 1) {
                        pantalla_pixel(x + px, y + py, 1);
                    } else {
                        pantalla_fill_rect(x + px * escala, y + py * escala, escala, escala, 1);
                    }
                }
            }
        }
        x += 8 * escala;
    }
}

void pantalla_dibujar_bateria(int x, int y, uint8_t pct) {
    if (pct > 100) pct = 100;
    pantalla_rect(x, y, 20, 10, 1);
    pantalla_fill_rect(x + 20, y + 3, 2, 4, 1);
    int relleno = (16 * pct) / 100;
    if (relleno > 0) {
        pantalla_fill_rect(x + 2, y + 2, relleno, 6, 1);
    }
}

bool pantalla_mostrar(void) {
    if (pantalla_state != PANTALLA_STATE_IDLE) {
        return false; // Aún enviando el frame anterior
    }

    // Copiar el framebuffer listo al buffer de transmisión
    memcpy(&tx_buf[1], draw_fb, PANTALLA_FB_SIZE);

    i2c_hw_t *hw = i2c_get_hw(pantalla_i2c);

    // Asegurar dirección destino en IC_TAR (útil si se comparte el bus con otros sensores)
    hw->enable = 0;
    hw->tar = pantalla_addr;
    hw->enable = 1;

    pantalla_error = false;
    pantalla_state = PANTALLA_STATE_CMD_WINDOW;
    current_ptr = cmd_window;
    current_len = sizeof(cmd_window);
    tx_idx = 0;

    (void)hw->clr_intr;
    // Habilitar interrupciones: al estar vacía la FIFO, TX_EMPTY dispara la ISR de inmediato
    hw->intr_mask = I2C_IC_INTR_MASK_M_TX_EMPTY_BITS |
                    I2C_IC_INTR_MASK_M_STOP_DET_BITS |
                    I2C_IC_INTR_MASK_M_TX_ABRT_BITS;

    return true;
}
