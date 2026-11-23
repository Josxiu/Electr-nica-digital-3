/**
 * @file mpu6050.c
 * @brief Driver del MPU-6050 / MPU-6500. Ver mpu6050.h.
 */
#include "mpu6050.h"
#include "config.h"

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

/* Registros (mapa de registros del MPU-6050; el 6500 usa los mismos) */
#define REG_SMPLRT_DIV      0x19
#define REG_CONFIG          0x1A
#define REG_GYRO_CONFIG     0x1B
#define REG_ACCEL_CONFIG    0x1C
#define REG_ACCEL_CONFIG2   0x1D    /* solo existe en el MPU-6500 */
#define REG_INT_PIN_CFG     0x37
#define REG_INT_ENABLE      0x38
#define REG_ACCEL_XOUT_H    0x3B    /* aquí empiezan los 14 bytes de datos */
#define REG_PWR_MGMT_1      0x6B
#define REG_WHO_AM_I        0x75

#define ID_MPU6050          0x68
#define ID_MPU6500          0x70

#define SENS_ACEL           16384.0f    /* LSB por g con +-2 g */
#define SENS_GIRO           65.5f       /* LSB por grado/s con +-500 */

/* ax, ay, az, temperatura, gx, gy, gz: 2 bytes cada uno, el alto primero */
#define N_BYTES             14

static uint8_t crudo[N_BYTES];              /* la llena la ISR del I2C */
static volatile bool ocupado = false;       /* hay una lectura en curso */
static volatile bool lectura_nueva = false;
static volatile uint32_t fallos = 0;
static volatile uint32_t perdidas = 0;      /* lecturas que llegaron sin que el main tomara la anterior */
static volatile uint32_t t_pedido_us = 0;
static uint32_t t_lectura_us = 0;           /* instante de la lectura guardada en crudo[] */

static void iniciar_bus(void) {
    i2c_init(BUS_I2C, BUS_I2C_HZ);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);

    /* La dirección del esclavo solo se puede cambiar con el bloque apagado */
    i2c_hw_t *hw = i2c_get_hw(BUS_I2C);
    hw->enable = 0;
    hw->tar = MPU_DIR;
    hw->enable = 1;
    /* Solo interesan el fin de la transferencia y el aborto (sin respuesta) */
    hw->intr_mask = I2C_IC_INTR_MASK_M_STOP_DET_BITS | I2C_IC_INTR_MASK_M_TX_ABRT_BITS;
}

/* Escritura y lectura de un registro, bloqueantes: solo para configurar. */
static bool escribir_reg(uint8_t reg, uint8_t valor) {
    const uint8_t buf[2] = { reg, valor };
    return i2c_write_timeout_us(BUS_I2C, MPU_DIR, buf, 2, false, BUS_TIMEOUT_US) == 2;
}

static bool leer_reg(uint8_t reg, uint8_t *valor) {
    if (i2c_write_timeout_us(BUS_I2C, MPU_DIR, &reg, 1, true, BUS_TIMEOUT_US) != 1) {
        return false;
    }
    return i2c_read_timeout_us(BUS_I2C, MPU_DIR, valor, 1, false, BUS_TIMEOUT_US) == 1;
}

/**
 * ISR del I2C. Llega al terminar la transferencia (STOP) o si el chip no
 * contestó. Saca los bytes de la FIFO de recepción; no espera nada.
 */
static void isr_i2c(void) {
    i2c_hw_t *hw = i2c_get_hw(BUS_I2C);
    const uint32_t causa = hw->intr_stat;
    (void)hw->clr_tx_abrt;              /* las banderas se borran al leer estos registros */
    (void)hw->clr_stop_det;
    if (!ocupado) {
        return;                         /* el STOP que sigue a un aborto ya atendido */
    }

    const bool aborto = (causa & I2C_IC_INTR_STAT_R_TX_ABRT_BITS) != 0;
    if (!aborto && hw->rxflr == N_BYTES) {
        if (lectura_nueva) {
            perdidas++;                 /* la anterior no se alcanzo a tomar: se pisa */
        }
        for (int i = 0; i < N_BYTES; i++) {
            crudo[i] = (uint8_t)hw->data_cmd;
        }
        t_lectura_us = t_pedido_us;
        lectura_nueva = true;
        fallos = 0;
    } else {
        while (hw->rxflr > 0) {         /* lectura incompleta: se bota */
            (void)hw->data_cmd;
        }
        fallos++;
    }
    ocupado = false;
}

/** ISR del pin INT: el chip tiene una muestra nueva. */
static void isr_int(void) {
    if (gpio_get_irq_event_mask(MPU_PIN_INT) & GPIO_IRQ_EDGE_RISE) {
        gpio_acknowledge_irq(MPU_PIN_INT, GPIO_IRQ_EDGE_RISE);
        mpu6050_pedir_lectura();
    }
}

bool mpu6050_iniciar(void) {
    irq_set_enabled(BUS_I2C_IRQ, false);    /* la configuración va por polling */
    iniciar_bus();

    /* Reinicio del chip. Si la Pico se reinició a mitad de una transferencia
     * el chip puede tener SDA agarrada: se libera el bus y se intenta otra vez. */
    if (!escribir_reg(REG_PWR_MGMT_1, 0x80)) {
        mpu6050_recuperar_bus();
        if (!escribir_reg(REG_PWR_MGMT_1, 0x80)) {
            return false;
        }
    }
    sleep_ms(100);

    /* Despertar, con el reloj del giroscopio X (más estable que el interno) */
    if (!escribir_reg(REG_PWR_MGMT_1, 0x01)) {
        return false;
    }
    sleep_ms(10);

    uint8_t id;
    if (!leer_reg(REG_WHO_AM_I, &id) || (id != ID_MPU6050 && id != ID_MPU6500)) {
        return false;
    }

    const uint8_t divisor = (uint8_t)(1000 / MPU_FRECUENCIA_HZ - 1);   /* f = 1 kHz / (1 + divisor) */
    bool ok = escribir_reg(REG_CONFIG, 0x03)            /* pasa-bajos de ~44 Hz */
           && escribir_reg(REG_SMPLRT_DIV, divisor)
           && escribir_reg(REG_GYRO_CONFIG, 0x08)       /* +-500 grados/s */
           && escribir_reg(REG_ACCEL_CONFIG, 0x00)      /* +-2 g */
           && escribir_reg(REG_INT_PIN_CFG, 0x00)       /* INT activo en alto, pulso de 50 us */
           && escribir_reg(REG_INT_ENABLE, 0x00);
    if (ok && id == ID_MPU6500) {
        ok = escribir_reg(REG_ACCEL_CONFIG2, 0x03);     /* el 6500 filtra el acelerómetro aparte */
    }
    if (!ok) {
        return false;
    }

    ocupado = false;
    lectura_nueva = false;
    fallos = 0;
    (void)i2c_get_hw(BUS_I2C)->clr_intr;    /* lo que dejó pendiente la configuración */
    irq_set_exclusive_handler(BUS_I2C_IRQ, isr_i2c);
    irq_set_priority(BUS_I2C_IRQ, PRIORIDAD_IRQ_I2C);
    irq_set_enabled(BUS_I2C_IRQ, true);
    return true;
}

bool mpu6050_muestrear_con_int(void) {
    gpio_init(MPU_PIN_INT);
    gpio_set_dir(MPU_PIN_INT, GPIO_IN);
    gpio_pull_down(MPU_PIN_INT);            /* nivel definido si el cable se suelta */

    irq_set_enabled(BUS_I2C_IRQ, false);
    const bool ok = escribir_reg(REG_INT_ENABLE, 0x01);     /* DATA_RDY_EN */
    (void)i2c_get_hw(BUS_I2C)->clr_intr;
    irq_set_enabled(BUS_I2C_IRQ, true);
    if (!ok) {
        return false;
    }

    gpio_add_raw_irq_handler(MPU_PIN_INT, isr_int);
    gpio_set_irq_enabled(MPU_PIN_INT, GPIO_IRQ_EDGE_RISE, true);
    irq_set_priority(IO_IRQ_BANK0, PRIORIDAD_IRQ_GPIO);
    irq_set_enabled(IO_IRQ_BANK0, true);
    return true;
}

void mpu6050_pedir_lectura(void) {
    if (ocupado) {
        fallos++;
        return;
    }
    ocupado = true;
    t_pedido_us = time_us_32();

    /* Los 15 comandos caben en la FIFO de transmisión (16), así que se dejan
     * todos de una vez: el registro inicial, un START repetido y 14 lecturas,
     * la última con STOP. El hardware hace el resto. */
    i2c_hw_t *hw = i2c_get_hw(BUS_I2C);
    hw->data_cmd = REG_ACCEL_XOUT_H;
    for (int i = 0; i < N_BYTES; i++) {
        uint32_t cmd = I2C_IC_DATA_CMD_CMD_BITS;            /* 1 = leer */
        if (i == 0) {
            cmd |= I2C_IC_DATA_CMD_RESTART_BITS;
        }
        if (i == N_BYTES - 1) {
            cmd |= I2C_IC_DATA_CMD_STOP_BITS;
        }
        hw->data_cmd = cmd;
    }
}

bool mpu6050_hay_lectura(void) {
    return lectura_nueva;
}

static int16_t unir(uint8_t alto, uint8_t bajo) {
    return (int16_t)((alto << 8) | bajo);
}

bool mpu6050_tomar_lectura(mpu6050_datos_t *d) {
    if (!lectura_nueva) {
        return false;
    }
    /* Copia con las interrupciones apagadas: la ISR no puede cambiar el
     * arreglo a la mitad */
    uint8_t b[N_BYTES];
    const uint32_t estado = save_and_disable_interrupts();
    for (int i = 0; i < N_BYTES; i++) {
        b[i] = crudo[i];
    }
    d->t_us = t_lectura_us;
    lectura_nueva = false;
    restore_interrupts(estado);

    d->ax = unir(b[0], b[1]) / SENS_ACEL;
    d->ay = unir(b[2], b[3]) / SENS_ACEL;
    d->az = unir(b[4], b[5]) / SENS_ACEL;
    /* b[6] y b[7] son la temperatura, no se usa */
    d->gx = unir(b[8], b[9]) / SENS_GIRO;
    d->gy = unir(b[10], b[11]) / SENS_GIRO;
    d->gz = unir(b[12], b[13]) / SENS_GIRO;
    return true;
}

uint32_t mpu6050_fallos(void) {
    return fallos;
}

uint32_t mpu6050_perdidas(void) {
    return perdidas;
}

void mpu6050_recuperar_bus(void) {
    const bool irq_activa = irq_is_enabled(BUS_I2C_IRQ);
    irq_set_enabled(BUS_I2C_IRQ, false);
    i2c_deinit(BUS_I2C);

    gpio_init(PIN_I2C_SCL);
    gpio_set_dir(PIN_I2C_SCL, GPIO_OUT);
    gpio_put(PIN_I2C_SCL, 1);
    gpio_init(PIN_I2C_SDA);
    gpio_set_dir(PIN_I2C_SDA, GPIO_IN);
    gpio_pull_up(PIN_I2C_SDA);
    sleep_us(10);

    /* El chip termina el byte que tenía a medias y suelta SDA */
    for (int i = 0; i < 9 && !gpio_get(PIN_I2C_SDA); i++) {
        gpio_put(PIN_I2C_SCL, 0);
        sleep_us(5);
        gpio_put(PIN_I2C_SCL, 1);
        sleep_us(5);
    }

    /* STOP: SDA sube con SCL en alto */
    gpio_put(PIN_I2C_SCL, 0);
    gpio_set_dir(PIN_I2C_SDA, GPIO_OUT);
    gpio_put(PIN_I2C_SDA, 0);
    sleep_us(5);
    gpio_put(PIN_I2C_SCL, 1);
    sleep_us(5);
    gpio_put(PIN_I2C_SDA, 1);
    sleep_us(5);

    iniciar_bus();
    ocupado = false;
    fallos = 0;
    irq_set_enabled(BUS_I2C_IRQ, irq_activa);
}
