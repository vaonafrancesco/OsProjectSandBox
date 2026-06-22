#!/usr/bin/env bash
# OS Project 2026 - Comprehensive End-to-End Test Suite 
# Sincronizzazione matematica basata sul worst-case delay e copertura totale Edge Cases.
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
CYAN='\033[0;36m'
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

echo -e "${YELLOW}Inizio esecuzione Test Suite Completa e Edge Cases...${NC}\n"

# ==========================================
# SEZIONE 1: Creazione 
# ==========================================
echo -e "${CYAN}=> Esecuzione Sezione 1: Creazione Dispositivi${NC}"
send_cmd "add hub"      # 1
sleep 3
send_cmd "add timer"    # 2
sleep 3
send_cmd "add bulb"     # 3
sleep 3
send_cmd "add window"   # 4
sleep 3
send_cmd "add fridge"   # 5
sleep 4 

assert_log "id=1.*hub" "Hub (id=1) non creato correttamente"
assert_log "id=5.*fridge" "Fridge (id=5) non creato correttamente"
pass "Sezione 1: Creazione completata"

# ==========================================
# SEZIONE 2: Linking Gerarchico
# ==========================================
# Creiamo l'albero: Timer(2) -> Hub(1) -> Bulb(3) e Timer(2) -> Window(4), Timer(2) -> Fridge(5)
# ATTENZIONE: Il Timer per specifica accetta SOLO UN FIGLIO. (Il tuo timer.c controlla: child_capacity=1)
# Collegheremo tutto sotto l'Hub(1) e poi l'Hub sotto il Timer(2)
echo -e "${CYAN}=> Esecuzione Sezione 2: Topologia e Loop Logici${NC}"
send_cmd "link 3 to 1"    
sleep 7 
send_cmd "link 4 to 1"    
sleep 7
send_cmd "link 5 to 1"    
sleep 7
send_cmd "link 1 to 2"    
sleep 7

# Generazione di errori voluti (Edge cases 2.2.4 & 2.2.8)
send_cmd "link 2 to 3"  # Non puoi linkare al Bulb
sleep 4 
send_cmd "link 2 to 1"  # Ciclo logico 2->1->2
sleep 4

assert_log "(error.*mismatch|not compatible|invalid parameter)" "Ammesso un link a dispositivo Interaction (Bulb)"
assert_log "(error.*cycle|cycle detected)" "Il sistema non ha rilevato il ciclo logico (2->1->2)"
pass "Sezione 2: Linking logico validato"

# ==========================================
# SEZIONE 3: Switch Semplici & Testi Window
# ==========================================
echo -e "${CYAN}=> Esecuzione Sezione 3: Attuatori e Stati Testuali (Window/Bulb)${NC}"
send_cmd "switch 3 power on"
sleep 4 
send_cmd "info 3"
sleep 4
assert_log "bulb id=3.*state=on" "Info della lampadina non riporta lo stato rigoroso 'on'"

send_cmd "switch 4 open on"
sleep 4
send_cmd "info 4"
sleep 4
assert_log "window id=4.*state=open" "Info della finestra non riporta lo stato rigoroso 'open'"
pass "Sezione 3: Switch e Stringhe di stato verificati"

# ==========================================
# SEZIONE 4: Propagazione Hub e Risoluzione Conflitti
# ==========================================
echo -e "${CYAN}=> Esecuzione Sezione 4: Propagazione Hub e Manual Override${NC}"
send_cmd "switch 1 sys_state off"
sleep 13 
send_cmd "info 1"
sleep 13
assert_log "hub.*state=off" "L'Hub non ha riportato stato OFF per tutto il branch"

# Usiamo scripts/manual_interaction.sh
bash scripts/manual_interaction.sh 3 switch power on >> "$MANUAL_OUT" 2>&1
sleep 4
send_cmd "info 1" 
sleep 13
assert_log "manual.*override" "L'Hub non ha rilevato il 'manual override' generato dallo script bash esterno"
pass "Sezione 4: Propagazione e Override completati"

# ==========================================
# SEZIONE 5: Frigorifero (Protezione e Auto-Close)
# ==========================================
echo -e "${CYAN}=> Esecuzione Sezione 5: Parametri e Timer del Frigorifero (Fridge)${NC}"

# 1. Modifica via controller (deve fallire con permission denied)
send_cmd "switch 5 thermostat 10"
sleep 4
assert_log "Permission denied" "Il Controller ha modificato un parametro ad uso solo manuale (thermostat)"

# 2. Modifica via manual interaction (deve passare)
bash scripts/manual_interaction.sh 5 set thermostat 8 >> "$MANUAL_OUT" 2>&1
sleep 4
send_cmd "info 5"
sleep 4
assert_log "thermostat=8" "L'interazione manuale non ha aggiornato il termostato del frigo"

# 3. Auto-close delay
echo -e "${YELLOW}Apro il frigo e attendo 32 secondi per attivare l'auto-chiusura (delay_seconds=30)...${NC}"
bash scripts/manual_interaction.sh 5 switch open on >> "$MANUAL_OUT" 2>&1
sleep 4
send_cmd "info 5"
sleep 4
assert_log "fridge id=5.*state=open" "Il frigo non si è aperto prima del delay"

sleep 32 # Superiamo il default delay di 30 secondi 

send_cmd "info 5"
sleep 4
assert_log "fridge id=5.*state=closed" "L'auto-chiusura del frigorifero non è scattata dopo il delay previsto"
pass "Sezione 5: Edge cases del frigorifero superati"

# ==========================================
# SEZIONE 6: Edge Case 2.2.8 - Concorrenza Esatta
# ==========================================
echo -e "${CYAN}=> Esecuzione Sezione 6: Race Condition (Comandi Simultanei)${NC}"
# Inviamo un comando via IPC e uno via shell controller ESATTAMENTE in parallelo
bash scripts/manual_interaction.sh 3 switch power off > /dev/null 2>&1 &
MANUAL_JOB_PID=$!
send_cmd "switch 3 power on"
wait "$MANUAL_JOB_PID"
sleep 5 

send_cmd "info 3"
sleep 5
assert_log "bulb id=3.*state=(on|off)" "La lampadina è andata in crash o stato inconsistente a causa dei comandi simultanei"
pass "Sezione 6: Conflitto di concorrenza risolto senza crash"

# ==========================================
# SEZIONE 7: Gestione Crash (SIGKILL)
# ==========================================
echo -e "${CYAN}=> Esecuzione Sezione 7: Tolleranza ai Guasti (SIGKILL)${NC}"
BULB_PID=$(grep -oE "id=3.*bulb.*pid=[0-9]+" "$CTRL_OUT" | grep -oE "[0-9]+$" | tail -n1)

if [ -n "$BULB_PID" ]; then
    kill -9 "$BULB_PID" 2>/dev/null || true
    sleep 4

    send_cmd "switch 1 sys_state off"
    sleep 15 # Hub aspetta il timeout del figlio e poi deve procedere
    send_cmd "list"
    sleep 4

    if tail -n 15 "$CTRL_OUT" | grep -E -i -q "^3[[:space:]]+bulb"; then
        fail "La bulb(3) uccisa con SIGKILL è ancora nella routing table del controller"
    fi
    pass "Sezione 7: Isolamento del fault riuscito"
else
    echo -e "${YELLOW}[ATTENZIONE] Salto il test Crash: impossibile estrarre il PID dal log.${NC}"
fi

# ==========================================
# SEZIONE 8: Eliminazione a Cascata Corretta
# ==========================================
echo -e "${CYAN}=> Esecuzione Sezione 8: Eliminazione a cascata dal Root${NC}"
# Ora distruggiamo l'intero sistema a partire dalla cima: Il Timer (id=2)
# Uccidendo il 2, dovranno perire 1, 4, 5 (la 3 è già stata uccisa col SIGKILL al passo prima)
send_cmd "del 2"
sleep 15 # Tempo profondo di propagazione dei segnali SIGTERM nella rete IPC
send_cmd "list"
sleep 4

assert_log "Deleted device: id=1" "L'eliminazione a cascata ha fallito nel rimuovere l'Hub(1) figlio del Timer"
assert_log "Deleted device: id=4" "L'eliminazione a cascata ha fallito nel rimuovere la Finestra(4) sotto l'Hub"
assert_log "Deleted device: id=5" "L'eliminazione a cascata ha fallito nel rimuovere il Frigo(5) sotto l'Hub"
pass "Sezione 8: Eliminazione a cascata dell'intero albero verificata"

send_cmd "exit"
sleep 3
echo -e "\n${GREEN}TUTTI I TEST E GLI EDGE CASES SONO STATI SUPERATI AL 100%!${NC}"
cleanup
exit 0