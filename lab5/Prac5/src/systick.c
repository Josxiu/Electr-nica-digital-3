/**
 * @file systick.c
 * @brief Implementación del driver de SysTick por acceso directo a registros.
 */
#include "systick.h"

#include <stddef.h>
#include "hardware/structs/systick.h"   /* systick_hw: CSR, RVR, CVR, CALIB */
#include "hardware/regs/m0plus.h"       /* máscaras M0PLUS_SYST_* y SHPR3   */
#include "hardware/regs/addressmap.h"   /* PPB_BASE                         */

/** Registro SHPR3 del bloque de control del sistema (prioridad de SysTick). */
#define SHPR3 (*(volatile uint32_t *)(PPB_BASE + M0PLUS_SHPR3_OFFSET))

static volatile systick_callback_t callback = NULL;
static volatile uint32_t conteo_irq = 0;

bool systick_configurar(uint32_t ticks)
{
    if (ticks == 0u || ticks > SYSTICK_PERIODO_MAX) {
        return false;
    }
    systick_hw->csr = 0u;                                    /* detener antes de cambiar RVR  */
    systick_hw->rvr = (ticks - 1u) & M0PLUS_SYST_RVR_RELOAD_BITS;
    systick_hw->cvr = 0u;                                    /* CVR = 0 y borra COUNTFLAG     */
    return true;
}

void systick_registrar_callback(systick_callback_t cb)
{
    callback = cb;
}

void systick_iniciar(bool con_interrupcion)
{
    uint32_t csr = M0PLUS_SYST_CSR_CLKSOURCE_BITS            /* reloj del procesador (clk_sys) */
                 | M0PLUS_SYST_CSR_ENABLE_BITS;
    if (con_interrupcion) {
        csr |= M0PLUS_SYST_CSR_TICKINT_BITS;
    }
    conteo_irq = 0u;
    systick_hw->cvr = 0u;                                    /* el primer periodo sale completo */
    systick_hw->csr = csr;
}

void systick_detener(void)
{
    systick_hw->csr = 0u;
}

void systick_fijar_prioridad(uint8_t prioridad)
{
    if (prioridad > 3u) {
        prioridad = 3u;
    }
    /* Solo los 2 bits altos del byte PRI_15 existen en el M0+ (bits 31:30). */
    SHPR3 = (SHPR3 & ~M0PLUS_SHPR3_PRI_15_BITS)
          | ((uint32_t)prioridad << M0PLUS_SHPR3_PRI_15_LSB);
}

bool systick_hubo_vuelta(void)
{
    return (systick_hw->csr & M0PLUS_SYST_CSR_COUNTFLAG_BITS) != 0u;
}

uint32_t systick_leer_actual(void)
{
    return systick_hw->cvr & M0PLUS_SYST_CVR_CURRENT_BITS;
}

uint32_t systick_periodo(void)
{
    return (systick_hw->rvr & M0PLUS_SYST_RVR_RELOAD_BITS) + 1u;
}

uint32_t systick_conteo_interrupciones(void)
{
    return conteo_irq;
}

/**
 * @brief Manejador de la excepción SysTick.
 *
 * El nombre isr_systick es el que la tabla de vectores del SDK (crt0.S) deja
 * como símbolo débil; al definirlo aquí, el enlazador usa esta función. La
 * excepción no necesita borrar ninguna bandera: el NVIC la da por atendida al
 * entrar.
 */
void isr_systick(void)
{
    conteo_irq++;
    systick_callback_t cb = callback;
    if (cb != NULL) {
        cb();
    }
}
