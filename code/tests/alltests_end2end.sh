#!/usr/bin/env bash
# OS Project 2026 - Comprehensive End-to-End Test Suite
set -u

# ==========================================
# SETUP AMBIENTE E PATH RELATIVI
# ==========================================
# CORREZIONE: Ora risolve automaticamente la cartella `code/` 
# andando un livello sopra la cartella dove si trova questo script.
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

OUT_DIR="runtime/test_outputs"
mkdir -p "$OUT_DIR"

TEST_NAME="alltests_end2end"
CTRL_OUT="$OUT_DIR/${TEST_NAME}.controller.out"
CTRL_IN="$OUT_DIR/${TEST_NAME}.fifo"
MANUAL_OUT="$OUT_DIR/${TEST_NAME}.manual.out"

# Colori per un output leggibile
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Evita chiusure silenti in caso di SIGPIPE (se il controller muore)
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
    if ! grep -E -q "$pattern" "$CTRL_OUT"; then
        fail "$error_msg"
    fi
}

assert_not_log() {
    local pattern="$1"
    local error_msg="$2"
    if grep -E -q "$pattern" "$CTRL_OUT"; then
        fail "$error_msg"
    fi
}

# ==========================================
# CONTROLLI PRELIMINARI
# ==========================================
if [ ! -x "./bin/domotics_controller" ]; then
    echo -e "${RED}Errore: Eseguibile non trovato in ./bin/domotics_controller${NC}"
    echo "Assicurati di trovarti nella cartella giusta e di aver eseguito 'make build'."
    exit 1
fi

# ==========================================
# INIZIALIZZAZIONE CONTROLLER
# ==========================================
bash scripts/cleanup_ipc.sh >/dev/null 2>&1 || true
rm -f "$CTRL_IN"
: > "$CTRL_OUT"
: > "$MANUAL_OUT"

mkfifo "$CTRL_IN" || fail "Impossibile creare la FIFO del controller"

./bin/domotics_controller < "$CTRL_IN" > "$CTRL_OUT" 2>&1 &
CTRL_PID=$!
exec {WRITER_FD}> "$CTRL_IN" || fail "Impossibile aprire la FIFO in scrittura"

sleep 1 # Tempo di avvio del controller

echo -e "${YELLOW}Inizio esecuzione Test Suite Completa...${NC}\n"

# ==========================================
# SEZIONE 1: Creazione e Validazione Tipi
# ==========================================
echo "=> Esecuzione Sezione 1: Creazione Dispositivi"
send_cmd "add hub"
sleep 1
send_cmd "add timer"
sleep 1
send_cmd "add bulb"
sleep 1
send_cmd "add window"
sleep 1
send_cmd "add fridge"
sleep 2

assert_log "Added device: id=1 type=hub" "Hub (id=1) non creato correttamente"
assert_log "Added device: id=2 type=timer" "Timer (id=2) non creato correttamente"
assert_log "Added device: id=3 type=bulb" "Bulb (id=3) non creato correttamente"
assert_log "Added device: id=4 type=window" "Window (id=4) non creato correttamente"
assert_log "Added device: id=5 type=fridge" "Fridge (id=5) non creato correttamente"
pass "Sezione 1: Creazione completata"


# ==========================================
# SEZIONE 2: Linking, Cicli e Gerarchie Invalide
# ==========================================
echo "=> Esecuzione Sezione 2: Linking Logic e Edge Cases"
send_cmd "link 3 to 1"    # Bulb(3) -> Hub(1) (Valido)
sleep 2
send_cmd "link 4 to 2"    # Window(4) -> Timer(2) (Valido)
sleep 2

# Edge cases richiesti dalle specifiche
send_cmd "link 1 to 3"    # Hub(1) -> Bulb(3) (Invalido: Bulb non è Control Device)
sleep 1
send_cmd "link 2 to 2"    # Timer(2) -> Timer(2) (Invalido: Self-link)
sleep 1
send_cmd "link 1 to 2"    # Hub(1) -> Timer(2) (Valido)
sleep 2
send_cmd "link 2 to 1"    # Timer(2) -> Hub(1) (Invalido: Creerebbe ciclo 1->2->1)
sleep 1

assert_log "Linked device 3 to 1" "Link base valido non funzionante (3->1)"
assert_log "Error: The selected devices are not compatible" "Il sistema ha permesso di usare un Interaction Device (Bulb) come genitore"
assert_log "Error: Self link not allowed" "Il sistema ha permesso un Self-Link (2->2)"
assert_log "Error: Cycle detected" "Il sistema non ha rilevato il ciclo logico (1->2->1)"
pass "Sezione 2: Linking e validazione completati"


# ==========================================
# SEZIONE 3: Switch Semplici (Interaction Devices)
# ==========================================
echo "=> Esecuzione Sezione 3: Attuatori e Switch"
send_cmd "switch 3 power on"
sleep 4 # Delay 1-3s max
send_cmd "info 3"
sleep 4

assert_log "bulb 3 switched on" "Fallito lo switch ON della lampadina"
assert_log "bulb id=3 .* state=on" "Info della lampadina non riporta lo stato ON"

send_cmd "switch 4 open on"
sleep 4
assert_log "window 4 switched open" "Fallito lo switch della finestra"
pass "Sezione 3: Switch singoli completati"


# ==========================================
# SEZIONE 4: Propagazione Hub e Risoluzione Conflitti
# ==========================================
echo "=> Esecuzione Sezione 4: Propagazione Hub"
send_cmd "switch 1 sys_state on"
sleep 8 # Propagazione profonda richiede tempo: Hub -> figli (1-3s) + esecuzione (1-3s)

send_cmd "info 1"
sleep 4
assert_log "hub id=1 .* state=on" "L'Hub non ha riportato stato ON dopo propagazione"
pass "Sezione 4: Propagazione Hub completata"


# ==========================================
# SEZIONE 5: Manual Override 
# ==========================================
echo "=> Esecuzione Sezione 5: Override Esterno"
# Usiamo il manual_client per spegnere la Bulb(3) bypassando il controller
./bin/manual_client 3 switch power off > "$MANUAL_OUT" 2>&1
sleep 4

send_cmd "info 1" # Il padre Hub(1) deve rilevare l'inconsistenza
sleep 6

assert_log "hub id=1 .* state=manual_override" "L'Hub non ha rilevato il 'manual override' dopo l'azione esterna sulla Bulb"

# Il controller sovrascrive tutto azzerando l'override
send_cmd "switch 1 sys_state off"
sleep 8
send_cmd "info 1"
sleep 6
assert_log "hub id=1 .* state=off" "L'Hub non ha azzerato il manual override al comando successivo"
pass "Sezione 5: Manual Override e recovery completati"


# ==========================================
# SEZIONE 6: Edge Cases del Timer
# ==========================================
echo "=> Esecuzione Sezione 6: Edge cases Timer"
# I parametri del timer passano per la manual interaction
./bin/manual_client 2 set begin 25:99 > /dev/null 2>&1
sleep 3
send_cmd "info 2"
sleep 4

assert_not_log "begin=25:99" "Il Timer ha accettato un orario non valido (25:99)"
pass "Sezione 6: Validazione orari Timer completata"


# ==========================================
# SEZIONE 7: Gestione Crash e SIGKILL
# ==========================================
echo "=> Esecuzione Sezione 7: Tolleranza ai Crash (SIGKILL)"
BULB_PID=$(awk '/^3[[:space:]]+bulb[[:space:]]+[0-9]+[[:space:]]+[0-9]+/ {print $3}' "$CTRL_OUT" | tail -n1)

if [ -z "$BULB_PID" ]; then
    fail "Impossibile recuperare il PID della Bulb(3) per il test di crash"
fi

# Uccidiamo il processo fisicamente
kill -9 "$BULB_PID"
sleep 4

send_cmd "switch 1 sys_state on"
sleep 8

# L'hub dovrebbe gestire il fallimento senza andare in blocco
send_cmd "list"
sleep 2

assert_not_log "^3[[:space:]]+bulb" "Il controller non ha rimosso la bulb dalla tabella di routing dopo il SIGKILL"
pass "Sezione 7: SIGCHLD e tolleranza ai crash completata"


# ==========================================
# SEZIONE 8: Eliminazione a cascata
# ==========================================
echo "=> Esecuzione Sezione 8: Eliminazione a cascata"
# Hub(1) controlla Timer(2) che controlla Window(4)
send_cmd "del 1"
sleep 6 # Deve propagare i segnali di DEL a tutta la gerarchia
send_cmd "list"
sleep 2

assert_not_log "^2[[:space:]]+timer" "L'eliminazione a cascata ha fallito nel rimuovere il figlio Timer(2)"
assert_not_log "^4[[:space:]]+window" "L'eliminazione a cascata ha fallito nel rimuovere il nipote Window(4)"
pass "Sezione 8: Eliminazione a cascata completata"

# Spegnimento
send_cmd "exit"
sleep 2

echo -e "\n${GREEN}TUTTI I TEST SUPERATI CON SUCCESSO!${NC}"
cleanup
exit 0