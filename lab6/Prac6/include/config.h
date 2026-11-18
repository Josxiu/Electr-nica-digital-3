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

/* Contador de pasos (valores iniciales, se ajustan con datos de caminata) */
#define PASOS_TAU_MEDIA_S   0.5f    /* promedio que quita la gravedad (s) */
#define PASOS_TAU_FILTRO_S  0.03f   /* suavizado de la senal (s), corta hacia 5 Hz */
#define PASOS_UMBRAL_G      0.12f   /* un paso: la senal sube de este valor (g) */
#define PASOS_SEP_MIN_MS    280u    /* tiempo minimo entre dos pasos (ms) */
#define PASOS_SEP_MAX_MS    2000u   /* sin pasos por mas que esto, se acaba la marcha (ms) */
#define PASOS_CONFIRMAR     4u      /* pasos seguidos para dar la marcha por iniciada */
#define PASOS_DT_MAX_S      0.1f    /* una pausa mayor entre muestras reinicia los filtros (s) */

#endif