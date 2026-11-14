#ifndef CONFIG_H
#define CONFIG_H

/* Bus I2C de la IMU */
#define BUS_I2C             i2c1
#define BUS_I2C_IRQ         I2C1_IRQ
#define PIN_I2C_SDA         26u
#define PIN_I2C_SCL         27u
#define BUS_I2C_HZ          100000u
#define BUS_TIMEOUT_US      5000u

/* IMU */
#define MPU_DIR             0x68u
#define MPU_PIN_INT         22u
#define MPU_FRECUENCIA_HZ   200u

/* Prioridades de interrupción (0x00 la más alta) */
#define PRIORIDAD_IRQ_GPIO  0x00u
#define PRIORIDAD_IRQ_I2C   0x80u

#endif