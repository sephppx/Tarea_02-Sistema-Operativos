#include <stdio.h>
#include <string.h>

#include "cmatch.h"

/*
 * Guardado y restauracion del estado de la simulacion.
 *
 * El archivo es binario y guarda lo necesario para reanudar tal como quedo: una
 * cabecera con un numero magico y la cantidad de jugadores, los contadores
 * globales, y por cada jugador su ELO y sus estadisticas. Como el snapshot se
 * escribe/lee cuando ya no hay otros threads tocando la tabla, no necesita
 * sincronizacion adicional.
 */

#define SNAP_MAGIC 0x434D4154u   /* "CMAT" */

typedef struct {
    unsigned int magic;
    int          n_players;
    int          game_counter;
    long long    wait_counter;
} SnapHeader;

typedef struct {
    int    id;
    double elo;
    int    wins;
    int    losses;
    int    draws;
} SnapPlayer;

int snapshot_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "No se pudo abrir %s para guardar el estado\n", path);
        return -1;
    }

    SnapHeader h;
    h.magic        = SNAP_MAGIC;
    h.n_players    = g_cfg.n_players;
    h.game_counter = g_game_counter;
    h.wait_counter = g_wait_counter;

    if (fwrite(&h, sizeof(h), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    for (int i = 0; i < g_cfg.n_players; i++) {
        SnapPlayer sp;
        sp.id     = g_players[i].id;
        sp.elo    = g_players[i].elo;
        sp.wins   = g_players[i].wins;
        sp.losses = g_players[i].losses;
        sp.draws  = g_players[i].draws;
        if (fwrite(&sp, sizeof(sp), 1, f) != 1) {
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    printf("Estado guardado en %s\n", path);
    return 0;
}

/*
 * Carga el estado desde el archivo. Se asume que g_players ya esta reservado con
 * el N de la configuracion actual. Devuelve 0 si se restauro, -1 si no.
 */
int snapshot_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "No existe un snapshot en %s para restaurar\n", path);
        return -1;
    }

    SnapHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1 || h.magic != SNAP_MAGIC) {
        fprintf(stderr, "El archivo %s no es un snapshot valido\n", path);
        fclose(f);
        return -1;
    }
    if (h.n_players != g_cfg.n_players) {
        fprintf(stderr, "El snapshot tiene %d jugadores y la config %d; deben coincidir\n",
                h.n_players, g_cfg.n_players);
        fclose(f);
        return -1;
    }

    g_game_counter = h.game_counter;
    g_wait_counter = h.wait_counter;

    for (int i = 0; i < g_cfg.n_players; i++) {
        SnapPlayer sp;
        if (fread(&sp, sizeof(sp), 1, f) != 1) {
            fclose(f);
            return -1;
        }
        g_players[i].id     = sp.id;
        g_players[i].elo    = sp.elo;
        g_players[i].wins   = sp.wins;
        g_players[i].losses = sp.losses;
        g_players[i].draws  = sp.draws;
    }

    fclose(f);
    printf("Estado restaurado desde %s\n", path);
    return 0;
}
