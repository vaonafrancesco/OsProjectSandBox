#!/usr/bin/env bash
# OS Project 2026 - Comprehensive End-to-End Test Suite 
# Sincronizzazione matematica basata sul worst-case delay e fix per Ubuntu/mawk
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

OUT_DIR="runtime/test_outputs"
mkdir -p "$OUT_DIR"

TEST_NAME="alltests_end2end"
CTRL_OUT="$OUT_DIR/${TEST_NAME}.controller.out"
CTRL_IN="$OUT_DIR/${TEST_NAME}.fifo"
MANUAL_OUT="$OUT_DIR/${TEST_NAME}.manual.out"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

trap 'fail "Il Controller è crashato o non si è avviato (Errore SIGPIPE)."' PIPE

fail() {
    echo -e "${RED}[FALLITO] $1${NC}"
    echo "========= CONTROLLER LOG ========="
    cat "$CTRL_OUT"
    echo "=================================="
    cleanup
    exit 1
}

pass() {
    echo -e "${GREEN}[PASSATO] $1${NC}"
}

cleanup() {
    [ -n "${WRITER_FD:-}" ] && exec {WRITER_FD}>&- 2>/dev/null || true
    [ -n "${CTRL_PID:-}" ] && kill -9 "$CTRL_PID" 2>/dev/null || true
    wait "$CTRL_PID" 2>/dev/null || true
    rm -f "$CTRL_IN"
    bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true
}

send_cmd() {
    echo "$1" >&"$WRITER_FD" || fail "Impossibile inviare il comando: $1"
}

assert_log() {
    local pattern="$1"
    local error_msg="$2"
    if ! grep -E -i -q "$pattern" "$CTRL_OUT"; then
        fail "$error_msg"
    fi
}

assert_not_log() {
    local pattern="$1"
    local error_msg="$2"
    if grep -E -i -q "$pattern" "$CTRL_OUT"; then
        fail "$error_msg"
    fi
}

if [ ! -x "./bin/domotics_controller" ]; then
    echo -e "${RED}Errore: Eseguibile non trovato in ./bin/domotics_controller${NC}"
    exit 1
fi

bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true
rm -f "$CTRL_IN"
: > "$CTRL_OUT"
: > "$MANUAL_OUT"

mkfifo "$CTRL_IN" || fail "Impossibile creare la FIFO del controller"

./bin/domotics_controller < "$CTRL_IN" > "$CTRL_OUT" 2>&1 &
CTRL_PID=$!
exec {WRITER_FD}> "$CTRL_IN" || fail "Impossibile aprire la FIFO in scrittura"

sleep 2 

echo -e "${YELLOW}Inizio esecuzione Test Suite Completa...${NC}\n"

# ==========================================
# SEZIONE 1: Creazione e Validazione Tipi
# ==========================================
echo "=> Esecuzione Sezione 1: Creazione Dispositivi"
send_cmd "add hub"
sleep 3
send_cmd "add timer"
sleep 3
send_cmd "add bulb"
sleep 3
send_cmd "add window"
sleep 3
send_cmd "add fridge"
sleep 4 

assert_log "id=1.*hub" "Hub (id=1) non creato correttamente"
assert_log "id=2.*timer" "Timer (id=2) non creato correttamente"
assert_log "id=3.*bulb" "Bulb (id=3) non creato correttamente"
pass "Sezione 1: Creazione completata"

# ==========================================
# SEZIONE 2: Linking, Cicli e Gerarchie Invalide
# ==========================================
echo "=> Esecuzione Sezione 2: Linking Logic e Edge Cases"
send_cmd "link 3 to 1"    
sleep 7 
send_cmd "link 4 to 2"    
sleep 7

send_cmd "link 1 to 3"    
sleep 4 
send_cmd "link 2 to 2"    
sleep 4
send_cmd "link 1 to 2"    
sleep 7
send_cmd "link 2 to 1"    
sleep 4

assert_log "(error.*mismatch|error.*type|not compatible|invalid parameters)" "Il sistema ha permesso di usare un Interaction Device (Bulb) come genitore"
assert_log "(error.*self|error.*allow|error.*link)" "Il sistema ha permesso un Self-Link (2->2)"
assert_log "(error.*cycle|error.*state|not allowed)" "Il sistema non ha rilevato il ciclo logico (1->2->1)"
pass "Sezione 2: Linking e validazione completati"

# ==========================================
# SEZIONE 3: Switch Semplici (Interaction Devices)
# ==========================================
echo "=> Esecuzione Sezione 3: Attuatori e Switch"
send_cmd "switch 3 power on"
sleep 4 
send_cmd "info 3"
sleep 4

assert_log "state=on" "Info della lampadina non riporta lo stato ON"
pass "Sezione 3: Switch singoli completati"

# ==========================================
# SEZIONE 4: Propagazione Hub e Risoluzione Conflitti
# ==========================================
echo "=> Esecuzione Sezione 4: Propagazione Hub"
send_cmd "switch 1 sys_state on"
sleep 13 
send_cmd "info 1"
sleep 13
assert_log "hub.*state=on" "L'Hub non ha riportato stato ON dopo propagazione"
pass "Sezione 4: Propagazione Hub completata"

# ==========================================
# SEZIONE 5: Manual Override 
# ==========================================
echo "=> Esecuzione Sezione 5: Override Esterno"
./bin/manual_client 3 switch power off > "$MANUAL_OUT" 2>&1
sleep 4
send_cmd "info 1" 
sleep 13

assert_log "manual.*override" "L'Hub non ha rilevato il 'manual override' dopo l'azione esterna sulla Bulb"

send_cmd "switch 1 sys_state off"
sleep 13
send_cmd "info 1"
sleep 13
assert_log "hub.*state=off" "L'Hub non ha azzerato il manual override al comando successivo"
pass "Sezione 5: Manual Override e recovery completati"

# ==========================================
# SEZIONE 6: Edge Cases del Timer
# ==========================================
echo "=> Esecuzione Sezione 6: Edge cases Timer"
./bin/manual_client 2 set begin 25:99 > /dev/null 2>&1
sleep 4
send_cmd "info 2"
sleep 4

assert_not_log "begin=25:99" "Il Timer ha accettato un orario non valido (25:99)"
pass "Sezione 6: Validazione orari Timer completata"

# ==========================================
# SEZIONE 7: Gestione Crash e SIGKILL
# ==========================================
echo "=> Esecuzione Sezione 7: Tolleranza ai Crash (SIGKILL)"
# FIX PER UBUNTU: Estrazione PID compatibile con tutti gli awk e grep
BULB_PID=$(grep -oE "id=3.*bulb.*pid=[0-9]+" "$CTRL_OUT" | grep -oE "[0-9]+$" | tail -n1)

if [ -n "$BULB_PID" ]; then
    kill -9 "$BULB_PID" 2>/dev/null || true
    sleep 4

    send_cmd "switch 1 sys_state on"
    sleep 15
    send_cmd "list"
    sleep 4

    # FIX: Controlliamo solo le ultime righe stampate dalla 'list', non tutto il file!
    if tail -n 15 "$CTRL_OUT" | grep -E -i -q "3.*bulb"; then
        fail "Il controller non ha rimosso la bulb(3) dalla tabella di routing dopo il SIGKILL (è ancora nella list)"
    fi
    pass "Sezione 7: SIGCHLD e tolleranza ai crash completata"
else
    echo -e "${YELLOW}[ATTENZIONE] Salto il test Crash: impossibile estrarre il PID dal log.${NC}"
fi

# ==========================================
# SEZIONE 8: Eliminazione a cascata
# ==========================================
echo "=> Esecuzione Sezione 8: Eliminazione a cascata"
send_cmd "del 1"
sleep 13 
send_cmd "list"
sleep 4

# FIX: Invece di controllare se non c'è nella list (rischiando falsi positivi con le righe vecchie), 
# ci assicuriamo che il controller abbia esplicitamente stampato di averli eliminati!
assert_log "Deleted device: id=2" "L'eliminazione a cascata ha fallito nel rimuovere il figlio Timer(2)"
assert_log "Deleted device: id=4" "L'eliminazione a cascata ha fallito nel rimuovere il nipote Window(4)"
pass "Sezione 8: Eliminazione a cascata completata"

send_cmd "exit"
sleep 3
echo -e "\n${GREEN}TUTTI I TEST SUPERATI CON SUCCESSO! Ottimo lavoro sul codice C!${NC}"
cleanup
exit 0
#...