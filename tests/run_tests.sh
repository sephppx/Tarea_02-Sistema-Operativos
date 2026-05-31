#!/usr/bin/env bash
#
# Bateria de pruebas para cmatch. Pensada para correr en Linux (lo que usan los
# ayudantes), aunque tambien funciona en macOS. Verifica que la implementacion
# cumpla con lo pedido en el enunciado:
#
#   1. Compila con los flags exactos (gcc -Wall -Wextra -std=c17) sin warnings.
#   2. La logica de ELO es correcta (test unitario).
#   3. Se forman partidas y nunca hay mas de K en simultaneo.
#   4. El emparejamiento respeta el rango de ELO (|ELO_A - ELO_B| <= MAX_ELO_DIFF).
#   5. SIGINT produce un cierre ordenado, sin threads colgados.
#   6. El estado se guarda en un snapshot y se puede restaurar.
#
# Uso:  ./tests/run_tests.sh        (o bien: make test)

set -u

# --- ubicaciones ---
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TMP_DIR="$SCRIPT_DIR/.tmp"
BIN="$ROOT_DIR/cmatch"

mkdir -p "$TMP_DIR"

PASS=0
FAIL=0

pass() { printf "  \033[32m[PASA]\033[0m %s\n" "$1"; PASS=$((PASS+1)); }
fail() { printf "  \033[31m[FALLA]\033[0m %s\n" "$1"; FAIL=$((FAIL+1)); }
title() { printf "\n\033[1m== %s ==\033[0m\n" "$1"; }

# escribe un archivo de configuracion en $TMP_DIR/$1 con los parametros dados
write_cfg() {
    local name="$1" n="$2" k="$3" maxdiff="$4" delay="$5" reenter="$6"
    cat > "$TMP_DIR/$name" <<EOF
N_PLAYERS=$n
K_BOARDS=$k
K_ELO=32
MAX_ELO_DIFF=$maxdiff
TURN_DELAY_MS=$delay
REENTER_PROBABILITY=$reenter
SNAPSHOT_PATH=$TMP_DIR/snap_${name}.bin
EOF
}

# -------------------------------------------------------------------
# Test 1: compilacion limpia con los flags exigidos por el enunciado
# -------------------------------------------------------------------
title "Test 1: compilacion sin warnings (gcc -Wall -Wextra -std=c17)"
( cd "$ROOT_DIR" && make clean >/dev/null 2>&1 )
BUILD_LOG="$TMP_DIR/build.log"
if ( cd "$ROOT_DIR" && make ) > "$BUILD_LOG" 2>&1; then
    if grep -qi "warning" "$BUILD_LOG"; then
        fail "compilo pero hay warnings (ver $BUILD_LOG)"
    else
        pass "compila sin errores ni warnings"
    fi
else
    fail "no compila (ver $BUILD_LOG)"
    echo "No se puede continuar sin el ejecutable."
    exit 1
fi

# -------------------------------------------------------------------
# Test 2: test unitario de la formula de ELO
# -------------------------------------------------------------------
title "Test 2: logica de ELO (test unitario)"
if gcc -Wall -Wextra -std=c17 "$SCRIPT_DIR/test_elo.c" "$ROOT_DIR/src/elo.c" \
       -o "$TMP_DIR/test_elo" -lpthread -lm 2>"$TMP_DIR/elo_build.log"; then
    if "$TMP_DIR/test_elo"; then
        pass "todas las comprobaciones de ELO pasan"
    else
        fail "alguna comprobacion de ELO falla"
    fi
else
    fail "no compila el test de ELO (ver $TMP_DIR/elo_build.log)"
fi

# -------------------------------------------------------------------
# Test 3: se forman partidas y nunca hay mas de K simultaneas
# -------------------------------------------------------------------
title "Test 3: nunca hay mas de K partidas simultaneas"
K=10
write_cfg "cfgK.env" 60 "$K" 400 100 0.9
OUT3="$TMP_DIR/out3.txt"
(
    sleep 0.4; printf 'matches\n'
    sleep 0.4; printf 'matches\n'
    sleep 0.4; printf 'matches\n'
    printf 'quit\n'
) | "$BIN" "$TMP_DIR/cfgK.env" > "$OUT3" 2>&1

MAX_SEEN=$(grep -oE 'partidas en curso: [0-9]+' "$OUT3" | grep -oE '[0-9]+$' | sort -nr | head -n1)
TOTAL_SAMPLES=$(grep -c 'partidas en curso:' "$OUT3")
if [ -z "$MAX_SEEN" ]; then
    fail "no se pudo leer la cantidad de partidas en curso"
elif [ "$MAX_SEEN" -gt "$K" ]; then
    fail "se observaron $MAX_SEEN partidas (mas que K=$K)"
elif [ "$MAX_SEEN" -lt 1 ]; then
    fail "nunca se formaron partidas"
else
    pass "maximo observado: $MAX_SEEN partidas en $TOTAL_SAMPLES muestras (tope K=$K respetado)"
fi

# -------------------------------------------------------------------
# Test 4: el emparejamiento respeta el rango de ELO
# -------------------------------------------------------------------
title "Test 4: el emparejamiento respeta |ELO_A - ELO_B| <= MAX_ELO_DIFF"
N=30
MAXDIFF=100
# retardo alto: asi las partidas no terminan entre que listamos los matches y
# leemos las estadisticas, y el ELO de cada par se mantiene constante.
write_cfg "cfgElo.env" "$N" 12 "$MAXDIFF" 600 0.5
OUT4="$TMP_DIR/out4.txt"
{
    sleep 0.5
    printf 'matches\n'
    i=0
    while [ "$i" -lt "$N" ]; do printf 'stats %d\n' "$i"; i=$((i+1)); done
    printf 'quit\n'
} | "$BIN" "$TMP_DIR/cfgElo.env" > "$OUT4" 2>&1

# pares de jugadores que estaban emparejados
grep -oE 'jugador [0-9]+ \(X\) vs jugador [0-9]+' "$OUT4" \
    | sed -E 's/jugador ([0-9]+) \(X\) vs jugador ([0-9]+)/\1 \2/' > "$TMP_DIR/pairs.txt"
# ELO de cada jugador
grep -oE 'jugador [0-9]+ \| ELO=[0-9.]+' "$OUT4" \
    | sed -E 's/jugador ([0-9]+) \| ELO=([0-9.]+)/\1 \2/' > "$TMP_DIR/elos.txt"

NPAIRS=$(wc -l < "$TMP_DIR/pairs.txt" | tr -d ' ')
if [ "$NPAIRS" -lt 1 ]; then
    fail "no se formaron partidas para verificar el rango de ELO"
else
    # awk: carga los ELO y comprueba cada par; imprime los que violan el rango
    if awk -v maxd="$MAXDIFF" '
        NR==FNR { elo[$1]=$2; next }
        {
            d = elo[$1] - elo[$2];
            if (d < 0) d = -d;
            if (d > maxd + 1e-6) { bad++; printf("      par (%s,%s) con diff=%.1f\n", $1, $2, d); }
        }
        END { exit (bad>0) ? 1 : 0 }
    ' "$TMP_DIR/elos.txt" "$TMP_DIR/pairs.txt"; then
        pass "los $NPAIRS pares emparejados cumplen |ELO_A - ELO_B| <= $MAXDIFF"
    else
        fail "hay pares que superan el rango de ELO permitido"
    fi
fi

# -------------------------------------------------------------------
# Test 5: SIGINT produce un cierre ordenado sin threads colgados
# -------------------------------------------------------------------
title "Test 5: cierre ordenado con SIGINT (sin threads colgados)"
write_cfg "cfgSig.env" 30 8 200 20 0.7
OUT5="$TMP_DIR/out5.txt"
# 'sleep 20' mantiene abierto stdin para que el programa no termine por EOF y
# podamos enviarle SIGINT como lo haria un usuario con Ctrl+C.
"$BIN" "$TMP_DIR/cfgSig.env" < <(sleep 20) > "$OUT5" 2>&1 &
CM_PID=$!
sleep 1
kill -INT "$CM_PID" 2>/dev/null

# espero a que termine, con un limite de ~10s
ENDED=0
for _ in $(seq 1 50); do
    if ! kill -0 "$CM_PID" 2>/dev/null; then ENDED=1; break; fi
    sleep 0.2
done

if [ "$ENDED" -ne 1 ]; then
    kill -9 "$CM_PID" 2>/dev/null
    fail "el programa quedo colgado tras SIGINT"
else
    wait "$CM_PID"; RC=$?
    if [ "$RC" -eq 0 ] && grep -q "no quedan threads colgados" "$OUT5"; then
        pass "termino ordenadamente tras SIGINT (codigo de salida 0)"
    else
        fail "termino tras SIGINT pero de forma anormal (rc=$RC)"
    fi
fi

# -------------------------------------------------------------------
# Test 6: snapshot (guardado y restauracion del estado)
# -------------------------------------------------------------------
title "Test 6: guardado y restauracion del estado (snapshot)"
write_cfg "cfgSnap.env" 20 6 200 20 0.6
SNAP="$TMP_DIR/snap_cfgSnap.env.bin"
rm -f "$SNAP"

# primera corrida: termina por fin de entrada (EOF) y debe guardar el snapshot
"$BIN" "$TMP_DIR/cfgSnap.env" < /dev/null > "$TMP_DIR/out6a.txt" 2>&1
if [ -s "$SNAP" ]; then
    pass "se genero el snapshot al cerrar"
else
    fail "no se genero el snapshot"
fi

# segunda corrida: debe restaurar desde el snapshot
"$BIN" "$TMP_DIR/cfgSnap.env" --resume < /dev/null > "$TMP_DIR/out6b.txt" 2>&1
if grep -q "Estado restaurado desde" "$TMP_DIR/out6b.txt"; then
    pass "el estado se restaura correctamente con --resume"
else
    fail "no se restauro el estado con --resume"
fi

# -------------------------------------------------------------------
# Resumen
# -------------------------------------------------------------------
title "Resumen"
printf "  Pruebas superadas: %d\n" "$PASS"
printf "  Pruebas falladas:  %d\n" "$FAIL"

# limpieza de temporales (se deja el snapshot/logs solo si algo fallo)
if [ "$FAIL" -eq 0 ]; then
    rm -rf "$TMP_DIR"
    printf "\n\033[32mTodas las pruebas pasaron.\033[0m\n"
    exit 0
else
    printf "\n\033[31mHubo pruebas con fallas (revisa %s).\033[0m\n" "$TMP_DIR"
    exit 1
fi
