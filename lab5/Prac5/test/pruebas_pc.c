/**
 * @file test/pruebas_pc.c
 * @brief Pruebas de componente de medicion y reporte que corren en el PC.
 *
 * Estos dos módulos no tocan hardware, así que se pueden probar con
 * timestamps inventados, sin Pico ni generador. Compilar y correr:
 * @code
 *   gcc -std=c11 -Wall -Wextra -I include test/pruebas_pc.c \
 *       src/medicion.c src/reporte.c -o pruebas_pc
 *   ./pruebas_pc
 * @endcode
 * Cada caso imprime OK o FALLA. El programa termina con código 1 si alguno falla.
 */
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "medicion.h"
#include "reporte.h"

static int fallas = 0;

static void verificar(int condicion, const char *nombre)
{
    printf("%-55s %s\n", nombre, condicion ? "OK" : "FALLA");
    if (!condicion) {
        fallas++;
    }
}

/**
 * Genera una señal ideal desde t0 durante dur_us: periodo T, tiempo en alto tH.
 * Devuelve el instante siguiente a la última subida generada.
 */
static uint32_t generar(medicion_t *m, uint32_t t0, uint32_t T, uint32_t tH, uint32_t dur_us)
{
    uint32_t t = t0;
    while (t - t0 < dur_us) {
        medicion_registrar_flanco(m, true, t);
        medicion_registrar_flanco(m, false, t + tH);
        t += T;
    }
    return t;
}

static void medir(uint32_t T, uint32_t tH, medicion_resultado_t *r)
{
    medicion_t m;
    medicion_ventana_t v;
    medicion_iniciar(&m, 0u);
    uint32_t t = generar(&m, 1000u, T, tH, 100000u);
    medicion_cerrar_ventana(&m, t, &v);
    medicion_calcular(&v, r);
}

int main(void)
{
    medicion_resultado_t r;
    char linea[REPORTE_MAX_BYTES + 1];

    /* --- medicion ---------------------------------------------------- */
    medir(1000u, 250u, &r);       /* 1 kHz, 25 % */
    verificar(r.estado == MEDICION_OK && r.frecuencia_mhz == 1000000u
              && r.duty_decimas == 250u, "1 kHz 25 %");

    medir(50000u, 2500u, &r);     /* 20 Hz, 5 % */
    verificar(r.estado == MEDICION_OK && r.frecuencia_mhz == 20000u
              && r.duty_decimas == 50u, "20 Hz 5 % (limite inferior del perfil)");

    medir(100u, 95u, &r);         /* 10 kHz, 95 % */
    verificar(r.estado == MEDICION_OK && r.frecuencia_mhz == 10000000u
              && r.duty_decimas == 950u, "10 kHz 95 % (limite superior del perfil)");

    medir(3000u, 1500u, &r);      /* 333,333 Hz, 50 % */
    verificar(r.estado == MEDICION_OK && r.frecuencia_mhz == 333333u
              && r.duty_decimas == 500u, "333.333 Hz 50 % (redondeo)");

    {   /* Sin flancos: ESPERANDO antes de 500 ms, SIN_TRANSICIONES después */
        medicion_t m;
        medicion_ventana_t v;
        medicion_iniciar(&m, 0u);
        medicion_cerrar_ventana(&m, 100000u, &v);
        medicion_calcular(&v, &r);
        verificar(r.estado == MEDICION_ESPERANDO, "arranque sin flancos -> ESPERANDO");
        medicion_cerrar_ventana(&m, 600000u, &v);
        medicion_calcular(&v, &r);
        verificar(r.estado == MEDICION_SIN_TRANSICIONES, "600 ms sin flancos -> SIN_TRANSICIONES");
    }

    {   /* Señal que se detiene y vuelve: no debe mezclar el periodo viejo */
        medicion_t m;
        medicion_ventana_t v;
        medicion_iniciar(&m, 0u);
        uint32_t t = generar(&m, 0u, 1000u, 500u, 100000u);
        medicion_cerrar_ventana(&m, t, &v);
        medicion_cerrar_ventana(&m, t + 700000u, &v);   /* 700 ms quieta */
        medicion_calcular(&v, &r);
        verificar(r.estado == MEDICION_SIN_TRANSICIONES, "entrada quieta 700 ms -> SIN_TRANSICIONES");
        uint32_t t2 = t + 800000u;
        uint32_t fin = generar(&m, t2, 2000u, 500u, 100000u);   /* vuelve a 500 Hz 25 % */
        medicion_cerrar_ventana(&m, fin, &v);
        medicion_calcular(&v, &r);
        verificar(r.estado == MEDICION_OK && r.frecuencia_mhz == 500000u
                  && r.duty_decimas == 250u, "la senal vuelve -> mide solo lo nuevo");
    }

    {   /* Flanco perdido: dos subidas seguidas no forman periodo */
        medicion_t m;
        medicion_ventana_t v;
        medicion_iniciar(&m, 0u);
        medicion_registrar_flanco(&m, true, 0u);
        medicion_registrar_flanco(&m, false, 500u);
        medicion_registrar_flanco(&m, true, 1000u);    /* periodo bueno */
        medicion_registrar_flanco(&m, true, 2000u);    /* falta la bajada */
        medicion_registrar_flanco(&m, false, 2500u);
        medicion_registrar_flanco(&m, true, 3000u);    /* periodo bueno */
        medicion_cerrar_ventana(&m, 3000u, &v);
        verificar(v.n_periodos == 2u && m.eventos_perdidos == 1u,
                  "subida repetida -> se descarta ese periodo");
    }

    {   /* Timer que da la vuelta (cerca de 2^32 us) */
        medicion_t m;
        medicion_ventana_t v;
        uint32_t t0 = 0xFFFF0000u;
        medicion_iniciar(&m, t0);
        uint32_t t = generar(&m, t0, 1000u, 300u, 100000u);
        medicion_cerrar_ventana(&m, t, &v);
        medicion_calcular(&v, &r);
        verificar(r.estado == MEDICION_OK && r.frecuencia_mhz == 1000000u
                  && r.duty_decimas == 300u, "desbordamiento del timer de 32 bits");
    }

    {   /* Tramo ciego de polling: se descarta solo si cabe un pulso entero */
        medicion_t m;
        medicion_ventana_t v;
        medicion_iniciar(&m, 0u);
        verificar(medicion_revisar_tramo_ciego(&m, 30u) == false,
                  "tramo ciego sin periodo en curso -> no corta");
        uint32_t t = generar(&m, 0u, 100u, 5u, 100000u);      /* 10 kHz, 5 % */
        medicion_cerrar_ventana(&m, t, &v);                   /* referencia: 5 us */
        verificar(m.pulso_min_referencia_us == 5u, "pulso minimo de la ventana = 5 us");
        medicion_registrar_flanco(&m, true, t);               /* periodo en curso */
        verificar(medicion_revisar_tramo_ciego(&m, 3u) == false,
                  "tramo de 3 us < pulso de 5 us -> no corta");
        verificar(medicion_revisar_tramo_ciego(&m, 30u) == true && m.cortes_tramo_ciego == 1u,
                  "tramo de 30 us >= pulso de 5 us -> corta el periodo");

        medicion_iniciar(&m, 0u);
        t = generar(&m, 0u, 50000u, 2500u, 200000u);          /* 20 Hz, 5 % */
        medicion_cerrar_ventana(&m, t, &v);
        medicion_registrar_flanco(&m, true, t);
        verificar(medicion_revisar_tramo_ciego(&m, 50u) == false,
                  "20 Hz: tramo de 50 us no alcanza un pulso de 2,5 ms");
    }

    /* --- reporte ----------------------------------------------------- */
    {
        reporte_t rep;
        reporte_iniciar(&rep);

        medicion_resultado_t esp = { MEDICION_ESPERANDO, 0u, 0u };
        reporte_formatear(&rep, &esp, 1000000u, linea, sizeof linea);
        verificar(strcmp(linea, "0,0,NA,NA,ESPERANDO\n") == 0, "primer reporte: n=0, dt=0");

        medicion_resultado_t ok = { MEDICION_OK, 1000000u, 250u };
        reporte_formatear(&rep, &ok, 1100000u, linea, sizeof linea);
        verificar(strcmp(linea, "1,100,1000.000,25.0,OK\n") == 0, "ejemplo de la guia (1 kHz, 25 %)");

        reporte_omitir(&rep);   /* n=2 se omite */
        medicion_resultado_t sin = { MEDICION_SIN_TRANSICIONES, 0u, 0u };
        reporte_formatear(&rep, &sin, 1301000u, linea, sizeof linea);
        verificar(strcmp(linea, "3,201,NA,NA,SIN_TRANSICIONES\n") == 0,
                  "reporte omitido deja hueco en n");

        reporte_t rep2;
        reporte_iniciar(&rep2);
        rep2.n = 4294967295u;
        medicion_resultado_t peor = { MEDICION_OK, 4294967295u, 1000u };
        size_t n = reporte_formatear(&rep2, &peor, 0u, linea, sizeof linea);
        verificar(n > 0u && n <= REPORTE_MAX_BYTES, "linea mas larga posible <= 64 bytes");
        printf("    (peor caso: %u bytes: %s", (unsigned)n, linea);
    }

    printf("\n%s (%d fallas)\n", fallas == 0 ? "TODAS LAS PRUEBAS PASARON" : "HAY FALLAS", fallas);
    return fallas == 0 ? 0 : 1;
}
