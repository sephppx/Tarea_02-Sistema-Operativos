#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cmatch.h"

/* quita espacios al inicio y al final de una cadena (in-place) */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

/* valores por defecto por si el archivo no trae alguna variable */
static void set_defaults(Config *cfg) {
    cfg->n_players     = 10;
    cfg->k_boards      = 2;
    cfg->k_elo         = 32;
    cfg->max_elo_diff  = 100;
    cfg->turn_delay_ms = 50;
    cfg->reenter_prob  = 0.8;
    strcpy(cfg->snapshot_path, "dump.bin");
}

/*
 * Lee el archivo de configuracion con formato de variables de entorno (.env).
 * Cada linea es NOMBRE=VALOR. Se ignoran lineas vacias y comentarios (#).
 * Devuelve 0 si todo ok, -1 si no se pudo abrir el archivo.
 */
int config_load(const char *path, Config *cfg) {
    set_defaults(cfg);

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "No se pudo abrir el archivo de configuracion: %s\n", path);
        return -1;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (*s == '\0' || *s == '#') continue;   /* linea vacia o comentario */

        char *eq = strchr(s, '=');
        if (!eq) continue;                        /* linea sin '=' la ignoramos */
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if      (strcmp(key, "N_PLAYERS")           == 0) cfg->n_players     = atoi(val);
        else if (strcmp(key, "K_BOARDS")            == 0) cfg->k_boards      = atoi(val);
        else if (strcmp(key, "K_ELO")               == 0) cfg->k_elo         = atoi(val);
        else if (strcmp(key, "MAX_ELO_DIFF")        == 0) cfg->max_elo_diff  = atoi(val);
        else if (strcmp(key, "TURN_DELAY_MS")       == 0) cfg->turn_delay_ms = atoi(val);
        else if (strcmp(key, "REENTER_PROBABILITY") == 0) cfg->reenter_prob  = atof(val);
        else if (strcmp(key, "SNAPSHOT_PATH")       == 0) {
            strncpy(cfg->snapshot_path, val, sizeof(cfg->snapshot_path) - 1);
            cfg->snapshot_path[sizeof(cfg->snapshot_path) - 1] = '\0';
        }
    }

    fclose(f);

    /* unas validaciones minimas para no reventar despues */
    if (cfg->n_players < 2)    cfg->n_players = 2;
    if (cfg->k_boards < 1)     cfg->k_boards = 1;
    if (cfg->reenter_prob < 0) cfg->reenter_prob = 0;
    if (cfg->reenter_prob > 1) cfg->reenter_prob = 1;

    return 0;
}

void config_print(const Config *cfg) {
    printf("=== Configuracion cargada ===\n");
    printf("  N_PLAYERS           = %d\n", cfg->n_players);
    printf("  K_BOARDS            = %d\n", cfg->k_boards);
    printf("  K_ELO               = %d\n", cfg->k_elo);
    printf("  MAX_ELO_DIFF        = %d\n", cfg->max_elo_diff);
    printf("  TURN_DELAY_MS       = %d\n", cfg->turn_delay_ms);
    printf("  REENTER_PROBABILITY = %.2f\n", cfg->reenter_prob);
    printf("  SNAPSHOT_PATH       = %s\n", cfg->snapshot_path);
    printf("=============================\n");
}
