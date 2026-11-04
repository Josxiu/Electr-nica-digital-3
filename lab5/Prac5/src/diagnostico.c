/**
 * @file diagnostico.c
 * @brief Implementación de la consola de diagnóstico por USB.
 */
#include "diagnostico.h"

#include <inttypes.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "config.h"

#if CONSOLA_USB

void diagnostico_iniciar(void)
{
    stdio_init_all();   /* solo USB: el stdio por UART está apagado en CMake */
}

bool diagnostico_pedido(void)
{
    return getchar_timeout_us(0) == 'e';
}

void diagnostico_imprimir(const char *version, const diagnostico_t *d)
{
    printf("[%s] programados=%" PRIu32 " omitidos=%" PRIu32
           " descartados=%" PRIu32 " flancos_perdidos=%" PRIu32
           " cortes_polling=%" PRIu32 " desbordes_cola=%" PRIu32
           " irq_gpio=%" PRIu32 " irq_uart=%" PRIu32 "\n",
           version,
           d->reportes_programados, d->reportes_omitidos, d->reportes_descartados,
           d->flancos_perdidos, d->cortes_polling, d->desbordes_cola,
           d->irq_gpio, d->irq_uart);
}

void diagnostico_error(const char *version, const char *mensaje)
{
    printf("[%s] ERROR: %s\n", version, mensaje);
}

#else  /* sin consola USB: funciones vacías */

void diagnostico_iniciar(void) {}
bool diagnostico_pedido(void) { return false; }
void diagnostico_imprimir(const char *version, const diagnostico_t *d) { (void)version; (void)d; }
void diagnostico_error(const char *version, const char *mensaje) { (void)version; (void)mensaje; }

#endif
