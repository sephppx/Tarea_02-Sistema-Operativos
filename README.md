# cmatch — Tarea 2 de Sistemas Operativos

Este es nuestro programa para la Tarea 2 del ramo (threads y sincronización).
La idea es simular un sistema de *matchmaking* para torneos de gato
(tic-tac-toe): hay `N` jugadores (cada uno es un thread) que buscan rival para
jugar, y compiten por `K` tableros disponibles (cada tablero también es un
thread). Todo se sincroniza usando solamente la librería de POSIX Threads
(`pthread`).

Acá dejamos el paso a paso de cómo compilarlo y usarlo.

## Lo que se necesita

- `gcc` (nosotros lo probamos con C17) y `make`.
- Linux o macOS. La sincronización usa `pthread`.

## Paso 1: compilar

Desde la carpeta del proyecto, ejecutamos:

```bash
make
```

Esto crea el ejecutable `cmatch`. Se compila con `gcc -Wall -Wextra -std=c17` y se
enlaza con `-lpthread` (y `-lm`, porque el cálculo del ELO usa `pow`).

Si queremos borrar lo compilado:

```bash
make clean
```

## Paso 2: revisar la configuración

El programa lee sus parámetros desde un archivo con formato `.env`. Por defecto
usa `config.env`, que ya viene en el repositorio. Se puede abrir y editar:

```
N_PLAYERS=100            # cuántos jugadores (threads) hay
K_BOARDS=20              # máximo de partidas a la vez (threads de tablero)
K_ELO=32                 # factor de ajuste del ELO
MAX_ELO_DIFF=50          # diferencia de ELO máxima para emparejar a dos jugadores
TURN_DELAY_MS=50         # milisegundos de espera entre cada jugada del gato
REENTER_PROBABILITY=0.8  # probabilidad de que un jugador vuelva a la cola al terminar
SNAPSHOT_PATH=dump.bin   # archivo donde se guarda/recupera el estado
```

Si queremos podemos hacer otro archivo (por ejemplo `mi_config.env`) y pasárselo
al programa en el paso siguiente.

## Paso 3: ejecutar

Para correrlo con la configuración por defecto:

```bash
./cmatch
```

Y si queremos usar otro archivo de configuración:

```bash
./cmatch mi_config.env
```

Apenas parte, el programa muestra la configuración que cargó y deja la simulación
corriendo. Los jugadores empiezan a buscar rival y a jugar solos.

## Paso 4: mirar lo que está pasando (consola de monitoreo)

Mientras el programa corre, queda esperando comandos. Estos son los que se pueden
escribir:

| Comando         | Qué hace |
|-----------------|----------|
| `matches`       | muestra cuántas partidas hay en curso y quiénes juegan |
| `stats <id>`    | muestra las estadísticas de un jugador (ganadas, perdidas, empatadas, ELO) |
| `match <id>`    | muestra el estado de una partida: jugadores, tablero y las jugadas |
| `help`          | vuelve a mostrar la ayuda |
| `quit`          | termina el programa de forma ordenada |

Por ejemplo, si escribimos `matches` y después `match 0`, podemos ver el tablero
de la partida número 0 y de quién es el turno.

## Paso 5: terminar el programa

Hay dos formas de terminar, y las dos hacen lo mismo (un cierre ordenado):

- escribir `quit` en la consola, o
- apretar `Ctrl+C` (esto manda la señal `SIGINT`).

Cuando esto pasa: no se empiezan partidas nuevas, las que están jugándose
terminan normalmente, se espera a que no quede ningún thread colgado y, al final,
el estado se guarda en el archivo indicado en `SNAPSHOT_PATH` (por defecto
`dump.bin`).

## Paso 6: retomar una simulación guardada

Como al terminar se guarda el estado, podemos volver a levantar la simulación
desde donde quedó (con los mismos ELO y estadísticas) usando `--resume`:

```bash
./cmatch --resume
```

## Ejemplo completo para probar rápido

```bash
make
./cmatch
# en la consola del programa escribimos, por ejemplo:
cmatch> matches
cmatch> stats 3
cmatch> match 0
cmatch> quit
# al salir se crea dump.bin; para retomar la simulación:
./cmatch --resume
```

## Cómo probamos que funciona

Dejamos una batería de pruebas que se corre con:

```bash
make test
```

Esa batería revisa que el programa cumpla lo que pide el enunciado: que compile
sin warnings, que el cálculo del ELO sea correcto, que nunca haya más de `K`
partidas a la vez, que el emparejamiento respete la diferencia máxima de ELO, que
`Ctrl+C` cierre todo de forma ordenada y que el guardado/recuperación del estado
funcione.

Para revisar que no haya condiciones de carrera, también lo compilamos con
ThreadSanitizer:

```bash
gcc -Wall -Wextra -std=c17 -fsanitize=thread -g src/*.c -o cmatch_tsan -lpthread -lm
./cmatch_tsan config.env
```

## Cómo está organizado el código

| Archivo            | Para qué es |
|--------------------|-------------|
| `src/cmatch.h`     | estructuras, estados y declaraciones que usa todo el programa |
| `src/config.c`     | lee el archivo `.env` |
| `src/elo.c`        | calcula la expectativa y actualiza el ELO |
| `src/game.c`       | la lógica del gato y el thread del tablero (hace de árbitro) |
| `src/player.c`     | el matchmaking y el thread de cada jugador |
| `src/monitor.c`    | las funciones de monitoreo y el registro de partidas activas |
| `src/snapshot.c`   | guarda y recupera el estado |
| `src/main.c`       | el arranque, el manejo de `Ctrl+C`, la consola y el cierre ordenado |
| `tests/`           | las pruebas (`run_tests.sh` y `test_elo.c`) |
| `informe/`         | el informe en LaTeX y su PDF |
# Tarea_02-Sistema-Operativos
