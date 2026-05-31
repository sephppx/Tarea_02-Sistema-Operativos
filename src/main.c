#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include "cmatch.h"

/* ----- estado global (declarado extern en cmatch.h) ----- */
Config          g_cfg;
Player         *g_players      = NULL;
pthread_mutex_t g_mutex        = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  g_cv           = PTHREAD_COND_INITIALIZER;
int             g_free_boards  = 0;
long long       g_wait_counter = 0;
int             g_game_counter = 0;
volatile int    g_running      = 1;

/* bandera que activa el manejador de SIGINT */
static volatile sig_atomic_t g_sigint = 0;

/* contador de tableros (threads de tablero) activos, para no dejar threads colgados */
static int             active_boards = 0;
static pthread_mutex_t boards_mtx    = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  boards_cv     = PTHREAD_COND_INITIALIZER;

/* ----- utilidades expuestas en el header ----- */
void sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void boards_inc(void) {
    pthread_mutex_lock(&boards_mtx);
    active_boards++;
    pthread_mutex_unlock(&boards_mtx);
}

void boards_dec(void) {
    pthread_mutex_lock(&boards_mtx);
    active_boards--;
    if (active_boards == 0) pthread_cond_broadcast(&boards_cv);
    pthread_mutex_unlock(&boards_mtx);
}

void boards_wait_empty(void) {
    pthread_mutex_lock(&boards_mtx);
    while (active_boards > 0) pthread_cond_wait(&boards_cv, &boards_mtx);
    pthread_mutex_unlock(&boards_mtx);
}

/* manejador de SIGINT: solo deja una marca (es lo unico seguro de hacer aca) */
static void on_sigint(int sig) {
    (void)sig;
    g_sigint = 1;
}

/* pide el termino ordenado: no se inician partidas nuevas y se despierta a
 * todos los jugadores que estan esperando en la cola */
static void request_shutdown(void) {
    pthread_mutex_lock(&g_mutex);
    if (g_running) {
        g_running = 0;
        printf("\n>> Termino solicitado: no se inician partidas nuevas, "
               "esperando a que terminen las que estan en curso...\n");
    }
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mutex);
}

/* deja a cada jugador con un ELO inicial variado para que el matchmaking por
 * cercania de ELO tenga sentido */
static void init_players(void) {
    g_players = calloc(g_cfg.n_players, sizeof(Player));
    if (!g_players) {
        perror("calloc players");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < g_cfg.n_players; i++) {
        g_players[i].id    = i;
        g_players[i].elo   = 800.0 + (rand() % 801);   /* ELO inicial entre 800 y 1600 */
        g_players[i].state = ST_WAITING;
    }
}

static void print_help(void) {
    printf("\nComandos disponibles:\n");
    printf("  stats <id>     -> estadisticas de un jugador\n");
    printf("  matches        -> partidas en curso\n");
    printf("  match <id>     -> estado de una partida\n");
    printf("  help           -> muestra esta ayuda\n");
    printf("  quit           -> termina la simulacion (igual que Ctrl+C)\n\n");
}

/* lee comandos por stdin para usar la interfaz de monitoreo */
static void console_loop(void) {
    char line[128];
    print_help();

    while (g_running) {
        if (g_sigint) {                 /* llego SIGINT mientras esperabamos */
            request_shutdown();
            break;
        }
        printf("cmatch> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (errno == EINTR) {       /* fgets interrumpido por SIGINT */
                errno = 0;
                if (g_sigint) { request_shutdown(); break; }
                continue;
            }
            /* EOF (Ctrl+D o fin de pipe): tambien terminamos ordenadamente */
            request_shutdown();
            break;
        }

        char cmd[32];
        int arg;
        int n = sscanf(line, "%31s %d", cmd, &arg);
        if (n <= 0) continue;

        if (strcmp(cmd, "stats") == 0 && n == 2) {
            player_stats(arg);
        } else if (strcmp(cmd, "matches") == 0) {
            current_matches();
        } else if (strcmp(cmd, "match") == 0 && n == 2) {
            match_status(arg);
        } else if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            request_shutdown();
            break;
        } else {
            printf("Comando no reconocido. Escribe 'help'.\n");
        }
    }
}

/* resumen final de los jugadores */
static void print_summary(void) {
    printf("\n=== Resumen final ===\n");
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < g_cfg.n_players; i++) {
        Player *p = &g_players[i];
        printf("  jugador %3d | ELO=%.1f | G=%d P=%d E=%d\n",
               p->id, p->elo, p->wins, p->losses, p->draws);
    }
    pthread_mutex_unlock(&g_mutex);
    printf("=====================\n");
}

int main(int argc, char **argv) {
    const char *config_path = "config.env";
    int resume = 0;

    /* argumentos: [archivo_config] [--resume] */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--resume") == 0 || strcmp(argv[i], "-r") == 0) {
            resume = 1;
        } else {
            config_path = argv[i];
        }
    }

    if (config_load(config_path, &g_cfg) != 0) {
        return EXIT_FAILURE;
    }
    config_print(&g_cfg);

    srand((unsigned int)time(NULL));

    init_players();

    if (resume) {
        if (snapshot_load(g_cfg.snapshot_path) != 0) {
            fprintf(stderr, "Se continuara con un estado nuevo.\n");
        }
    }

    g_free_boards = g_cfg.k_boards;   /* al inicio todos los tableros estan libres */

    /* SIGINT sin SA_RESTART para que interrumpa el fgets de la consola */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    /* lanzo un thread por jugador */
    pthread_t *threads = calloc(g_cfg.n_players, sizeof(pthread_t));
    if (!threads) {
        perror("calloc threads");
        return EXIT_FAILURE;
    }
    for (int i = 0; i < g_cfg.n_players; i++) {
        if (pthread_create(&threads[i], NULL, player_thread, &g_players[i]) != 0) {
            perror("pthread_create player");
            return EXIT_FAILURE;
        }
    }

    printf("\nSimulacion en marcha con %d jugadores y %d tableros.\n",
           g_cfg.n_players, g_cfg.k_boards);
    printf("Usa Ctrl+C o 'quit' para terminar de forma ordenada.\n");

    /* hilo principal: atiende la consola de monitoreo */
    console_loop();

    /* termino ordenado: espero a todos los jugadores y a los tableros */
    for (int i = 0; i < g_cfg.n_players; i++) {
        pthread_join(threads[i], NULL);
    }
    boards_wait_empty();

    printf(">> Todas las partidas terminaron y no quedan threads colgados.\n");

    /* guardo el estado para poder reanudar mas adelante */
    snapshot_save(g_cfg.snapshot_path);
    print_summary();

    /* limpieza */
    free(threads);
    free(g_players);
    pthread_mutex_destroy(&g_mutex);
    pthread_cond_destroy(&g_cv);

    return EXIT_SUCCESS;
}
