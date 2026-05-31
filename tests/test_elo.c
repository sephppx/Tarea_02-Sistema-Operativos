/*
 * Test unitario de la logica de ELO (elo.c).
 *
 * Compila junto con ../src/elo.c y comprueba la expectativa logistica y la
 * actualizacion del puntaje tras una partida (victoria, empate). Como elo.c usa
 * algunas variables globales declaradas en cmatch.h, las definimos aca para que
 * enlace.
 *
 * Devuelve 0 si todas las comprobaciones pasan, distinto de 0 si alguna falla.
 */
#include <stdio.h>
#include <math.h>
#include <pthread.h>

#include "../src/cmatch.h"

/* definiciones minimas de los globals que usa elo.c */
Config          g_cfg;
Player         *g_players      = NULL;
pthread_mutex_t g_mutex        = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  g_cv           = PTHREAD_COND_INITIALIZER;
int             g_free_boards  = 0;
long long       g_wait_counter = 0;
int             g_game_counter = 0;
volatile int    g_running      = 1;

static int fails = 0;

#define CHECK(cond, msg) do {                       \
    if (cond) { printf("  [OK]    %s\n", msg); }      \
    else      { printf("  [FALLA] %s\n", msg); fails++; } \
} while (0)

static int approx(double a, double b) {
    return fabs(a - b) < 1e-6;
}

int main(void) {
    g_cfg.k_elo = 32;
    printf("== Test unitario de ELO ==\n");

    /* 1. Con ELO igual, la expectativa debe ser 0.5 */
    CHECK(approx(elo_expected(1500, 1500), 0.5),
          "expectativa con ELO igual es 0.5");

    /* 2. La expectativa es simetrica: E(A,B) + E(B,A) = 1 */
    CHECK(approx(elo_expected(1700, 1500) + elo_expected(1500, 1700), 1.0),
          "E(A,B) + E(B,A) = 1");

    /* 3. El de mayor ELO tiene mayor expectativa de ganar */
    CHECK(elo_expected(1700, 1500) > 0.5 && elo_expected(1500, 1700) < 0.5,
          "el de mayor ELO tiene expectativa > 0.5");

    /* 4. Victoria con ELO igual: el ganador suma K/2 y el perdedor resta K/2 */
    {
        Player a = {0}, b = {0};
        a.id = 0; a.elo = 1500;
        b.id = 1; b.elo = 1500;
        Match m = {0};
        m.players[0] = &a;
        m.players[1] = &b;
        m.status = M_P0_WIN;
        elo_apply_result(&m);
        CHECK(approx(a.elo, 1516.0), "ganador (ELO igual) sube a 1516");
        CHECK(approx(b.elo, 1484.0), "perdedor (ELO igual) baja a 1484");
        CHECK(a.wins == 1 && b.losses == 1, "se cuenta 1 ganada y 1 perdida");
    }

    /* 5. Empate con ELO igual: nadie cambia su ELO y suma un empate a cada uno */
    {
        Player a = {0}, b = {0};
        a.elo = 1500; b.elo = 1500;
        Match m = {0};
        m.players[0] = &a;
        m.players[1] = &b;
        m.status = M_DRAW;
        elo_apply_result(&m);
        CHECK(approx(a.elo, 1500.0) && approx(b.elo, 1500.0),
              "empate con ELO igual no cambia el puntaje");
        CHECK(a.draws == 1 && b.draws == 1, "se cuenta un empate a cada jugador");
    }

    /* 6. El ELO se conserva: lo que sube uno es lo que baja el otro */
    {
        Player a = {0}, b = {0};
        a.elo = 1600; b.elo = 1400;
        double total_antes = a.elo + b.elo;
        Match m = {0};
        m.players[0] = &a;
        m.players[1] = &b;
        m.status = M_P1_WIN;
        elo_apply_result(&m);
        CHECK(approx(a.elo + b.elo, total_antes),
              "el ELO total se conserva tras la partida");
    }

    printf("== Resultado: %s (%d fallas) ==\n", fails == 0 ? "TODO OK" : "HAY FALLAS", fails);
    return fails == 0 ? 0 : 1;
}
