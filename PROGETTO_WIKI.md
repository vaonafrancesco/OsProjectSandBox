# 🏠 OS Project 2026 — Sistema di Domotica Emulato
> **Documento di riferimento interno del team.**
> Se hai staccato dal progetto e vuoi rientrare velocemente, leggi questo file dall'inizio alla fine.

---

## Indice
1. [Cos'è il progetto](#1-cosè-il-progetto)
2. [Regole fondamentali imposte dal prof](#2-regole-fondamentali-imposte-dal-prof)
3. [Architettura: due livelli distinti](#3-architettura-due-livelli-distinti)
4. [I tipi di dispositivo](#4-i-tipi-di-dispositivo)
5. [Come comunica tutto: IPC con Named Pipes (FIFO)](#5-come-comunica-tutto-ipc-con-named-pipes-fifo)
6. [Il Protocollo applicativo (formato messaggi)](#6-il-protocollo-applicativo-formato-messaggi)
7. [Il Controller: il cuore del sistema](#7-il-controller-il-cuore-del-sistema)
8. [Ciclo di vita di un dispositivo](#8-ciclo-di-vita-di-un-dispositivo)
9. [Linking: la gerarchia logica](#9-linking-la-gerarchia-logica)
10. [Manual Interaction (override esterno)](#10-manual-interaction-override-esterno)
11. [Concorrenza e consistenza dello stato](#11-concorrenza-e-consistenza-dello-stato)
12. [Gestione errori e crash](#12-gestione-errori-e-crash)
13. [Struttura delle cartelle e build system](#13-struttura-delle-cartelle-e-build-system)
14. [Consegna](#14-consegna)
15. [Glossario rapido](#15-glossario-rapido)

---

## 1. Cos'è il progetto

Dobbiamo realizzare un sistema domotico **emulato**: ogni dispositivo della casa (lampadina, frigo, hub, timer…) è un **processo Unix distinto**. I processi comunicano tra loro tramite IPC, si organizzano in una gerarchia logica, e sono tutti coordinati da un processo principale chiamato **Controller**.

Non è una simulazione grafica: è un sistema a riga di comando in cui il Controller espone una **shell interattiva** e ogni dispositivo vive come processo figlio.

Il progetto vale **10 punti** sul totale del corso, così distribuiti:
- 20% codice (correttezza, qualità, aderenza alle spec)
- 10% report PDF (max 5 pagine)
- 70% presentazione orale all'esame

---

## 2. Regole fondamentali imposte dal prof

Queste sono **non negoziabili**. Deviare porta a penalizzazioni.

| Regola | Dettaglio |
|--------|-----------|
| **Linguaggi** | Solo C e Bash. Niente C++, niente Python, niente altro. |
| **Un processo per dispositivo** | Ogni device DEVE essere un processo OS distinto. |
| **Gerarchia OS piatta** | Tutti i device sono figli diretti del Controller. Un Hub non è padre OS di una Bulb. |
| **Gerarchia logica via IPC** | Chi controlla chi si decide a runtime con il comando `link`, non alla fork. |
| **Delay simulato** | Ogni device aspetta 1-3 secondi prima di rispondere, per testare la concorrenza. |
| **Build system** | Makefile o `build.sh` con target `build`, `clean`, `run`. |
| **Nome archivio consegna** | `Cognome1_Cognome2_Cognome3_Cognome4.tar.gz` |

---

## 3. Architettura: due livelli distinti

Questo è il punto più importante da capire. Esistono **due gerarchie parallele e indipendenti**.

### 3.1 Livello OS (processo Unix) — **sempre piatta**

```
Controller (PID principale)
├── Bulb_1   (figlio diretto)
├── Hub_2    (figlio diretto)
├── Fridge_3 (figlio diretto)
└── Timer_4  (figlio diretto)
```

Il Controller fa `fork()` per ogni device. **Nessun device fa fork di un altro device.** Questo semplifica enormemente la gestione dei segnali (es. `SIGCHLD`) e la pulizia delle risorse.

### 3.2 Livello Applicativo (logico) — **dinamica, definita via `link`**

```
Controller
└── Hub_2          ← logicamente controlla:
    ├── Bulb_1
    └── Timer_4    ← logicamente controlla:
        └── Fridge_3
```

Questa gerarchia non ha esistenza a livello OS: è solo una **tabella di routing** interna a ogni processo. Hub_2 sa di avere come figli logici Bulb_1 e Timer_4, e Timer_4 sa di avere come figlio logico Fridge_3. Tutto passa per messaggi IPC.

**Regola chiave:** un Control Device (Controller, Hub, Timer) che riceve un comando lo propaga via IPC a tutti i suoi figli logici. Non mantiene cache dello stato dei figli: deve interrogarli ogni volta.

---

## 4. I tipi di dispositivo

### 4.1 Control Devices (propagano comandi ai figli logici)

#### Controller
- **Unico**, non può essere eliminato, è il padre OS di tutti.
- **State:** on/off
- **Switch:** `main` (on/off per spegnere tutto)
- **Registry:** `num` (numero di device figli diretti)

#### Hub
- Connette più device in parallelo.
- **State:** rispecchia lo stato dei figli — ma solo se sono tutti consistenti. Se i figli hanno stati diversi (es. per un override manuale), risponde `"manual override"` invece di on/off.
- Qualunque nuovo comando inviato all'Hub sovrascrive tutti i figli e azzera gli override.

#### Timer
- Controlla **un solo** device figlio secondo uno schedule.
- **Registry:** `begin` (HH:MM), `end` (HH:MM)
- **State/Switch:** come Hub (mirroring del figlio, con stessa logica di override).
- Casi limite da gestire: `begin > end`, orari nel passato.

### 4.2 Interaction Devices (attuatori/sensori finali, nessun figlio logico)

#### Bulb
- **State:** on/off
- **Switch:** `power` (on/off)
- **Registry:** `time` (tempo totale di accensione)

#### Window
- **State:** open/closed
- **Switch:** `open` e `close` — tornano automaticamente a `off` dopo essere stati attivati (impulso, non latch)
- **Registry:** `time` (tempo totale trascorso aperta)

#### Fridge
- **State:** open/closed
- **Switch:** `open`/`close`
- **Registry:**
  - `time` — tempo trascorso aperto
  - `delay` — secondi dopo cui si richiude automaticamente
  - `perc` — percentuale di riempimento (0-100%), modificabile **solo manualmente**
  - `temp` — temperatura interna corrente
  - `thermostat` — temperatura target, modificabile **solo manualmente**

---

## 5. Come comunica tutto: IPC con Named Pipes (FIFO)

### Scelta tecnica
Usiamo **solo Named Pipes (FIFO)**, niente socket, niente shared memory. Motivo: le FIFO sono accessibili facilmente anche da Bash con semplici redirect (`echo "..." > /tmp/domotica_3.fifo`), il che permette di implementare la manual interaction come script Bash.

### Dove vivono le pipe
Tutte in `/tmp/`. Naming convention:

| Pipe | Chi la legge | Usata per |
|------|-------------|-----------|
| `/tmp/domotica_ctrl.fifo` | Controller | Riceve notifiche/risposte da tutti i device figli |
| `/tmp/domotica_<ID>.fifo` | Device con quell'ID | Riceve comandi dal Controller o da interazione manuale |

Ogni processo, appena nasce (fork+exec), **crea la propria FIFO** e si mette in ascolto su di essa.

### Flusso tipico di un comando

```
[Utente digita "switch 3 power on"]
        ↓
Controller scrive su /tmp/domotica_3.fifo
        ↓
Device 3 riceve, dorme 1-3s (delay simulato), aggiorna stato
        ↓
Device 3 scrive risposta su /tmp/domotica_ctrl.fifo
        ↓
Controller legge la risposta nel suo loop select()
```

---

## 6. Il Protocollo applicativo (formato messaggi)

Il protocollo è **testuale** (stringhe ASCII), per compatibilità con Bash. Ogni messaggio termina con `\n`.

### Formato
```
SENDER_TYPE:SENDER_ID|COMMAND|TARGET_ID|PAYLOAD\n
```

| Campo | Valori | Significato |
|-------|--------|-------------|
| `SENDER_TYPE` | `CTRL`, `DEV`, `EXT` | Chi manda (Controller / Device / Bash esterno) |
| `SENDER_ID` | intero | ID del mittente (0 = Controller) |
| `COMMAND` | `SWITCH`, `LINK`, `INFO`, `DEL`, `STATUS` | Tipo di operazione |
| `TARGET_ID` | intero | ID del destinatario logico |
| `PAYLOAD` | stringa | Dati specifici del comando |

### Esempi

```bash
# Controller chiede info al device 3
CTRL:0|INFO|3|ALL\n

# Device 3 risponde (dopo delay)
DEV:3|STATUS|0|on,temp:20\n

# Bash fa override manuale su device 5
EXT:0|SWITCH|5|power,on\n

# Controller ordina link: device 1 diventa figlio logico di device 2
CTRL:0|LINK|1|parent:2\n
```

### Codici di errore (da definire sia in C che in Bash con lo stesso valore numerico)

```c
// Esempi da implementare in protocol.h
#define ERR_DEVICE_NOT_FOUND    1
#define ERR_INVALID_COMMAND     2
#define ERR_LINK_FAILED         3
#define ERR_DEVICE_TYPE_MISMATCH 4
#define ERR_CYCLE_DETECTED      5
```
In Bash si usano variabili con lo stesso valore (`ERR_DEVICE_NOT_FOUND=1`).

---

## 7. Il Controller: il cuore del sistema

### Shell interattiva
Il Controller espone una shell che accetta questi comandi:

| Comando | Cosa fa |
|---------|---------|
| `list` | Elenca tutti i device con ID e stato riassunto |
| `add <device>` | Crea un nuovo processo figlio (es. `add bulb`) e stampa i dettagli |
| `del <id>` | Termina il processo. Se è un Control Device, propaga la terminazione ai figli logici via IPC |
| `link <id1> to <id2>` | Imposta id1 come figlio logico di id2 (id2 deve essere Controller/Hub/Timer) |
| `switch <id> <label> <pos>` | Es. `switch 3 power on` |
| `info <id>` | Mostra tutti i dettagli del device |

### Il loop con `select()`

**Il Controller non usa thread.** Usa un singolo loop basato su `select()` che monitora contemporaneamente:
1. `STDIN` — input dell'utente dalla shell
2. `/tmp/domotica_ctrl.fifo` — messaggi in arrivo dai device

```c
// Schema semplificato del loop principale
fd_set read_fds;
int ctrl_pipe_fd = open("/tmp/domotica_ctrl.fifo", O_RDONLY | O_NONBLOCK);

while (1) {
    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    FD_SET(ctrl_pipe_fd, &read_fds);

    select(ctrl_pipe_fd + 1, &read_fds, NULL, NULL, NULL);

    if (FD_ISSET(STDIN_FILENO, &read_fds))   handle_user_input();
    if (FD_ISSET(ctrl_pipe_fd, &read_fds))   handle_device_response();
}
```

**Perché non bloccarsi:** i device rispondono con 1-3 secondi di delay. Se il Controller si bloccasse ad aspettare la risposta, non potrebbe accettare altri comandi nel frattempo. Con `select()` torna subito disponibile e processa la risposta solo quando arriva.

### Tabella di routing interna del Controller

Il Controller mantiene una struttura (array o lista) che mappa ogni ID di device a:
- tipo del device
- PID del processo
- ID del padre logico (se assegnato)
- lista degli ID dei figli logici (se è un Control Device)

Questa struttura si aggiorna ad ogni `add`, `del` e `link`.

---

## 8. Ciclo di vita di un dispositivo

```
add bulb
   │
   ├─ Controller assegna un ID univoco (es. 5)
   ├─ Controller esegue fork()
   │      └─ figlio: exec("./bulb", "5", ...)   ← il device sa il suo ID dall'argv
   │
   └─ Il processo bulb_5:
         1. Crea /tmp/domotica_5.fifo
         2. Apre /tmp/domotica_ctrl.fifo in scrittura
         3. Entra nel suo loop principale (legge dalla sua FIFO)
         4. Per ogni messaggio ricevuto: sleep(rand%3+1), poi esegue e risponde

del 5
   │
   ├─ Se device 5 ha figli logici: Controller manda DEL a ciascuno prima
   ├─ Controller manda DEL a device 5 sulla sua FIFO
   ├─ Device 5 riceve, fa cleanup (rimuove /tmp/domotica_5.fifo), exit(0)
   └─ Controller rimuove ID 5 dalla tabella di routing
```

---

## 9. Linking: la gerarchia logica

Il comando `link <id1> to <id2>` **non uccide né rilancia processi**. Aggiorna solo le tabelle di routing interne:

1. Controller manda messaggio IPC a `id1`: "il tuo nuovo padre logico è `id2`"
2. Controller manda messaggio IPC a `id2`: "hai un nuovo figlio logico: `id1`"
3. Entrambi i device aggiornano le proprie strutture dati interne

**Casi di fallimento immediato:**
- `id2` è un Interaction Device (Bulb, Window, Fridge) → solo Control Device possono essere padri
- `id1 == id2` → un device non può essere figlio di se stesso
- Il link crea un ciclo nella gerarchia logica (es. A→B→A) → da rilevare e rifiutare

---

## 10. Manual Interaction (override esterno)

### Cos'è
Simula un'azione fisica umana sul device (es. premere il pulsante di una lampadina). Bypassa completamente il Controller e scrive direttamente sulla FIFO del device target.

### Come si usa
```bash
./manual_interaction.sh <id> <command> [parameters]

# Esempi:
./manual_interaction.sh 3 switch power on
./manual_interaction.sh 7 switch open
./manual_interaction.sh 5 set thermostat 18
```

### Implementazione
Lo script Bash legge il registro dei device (es. un file `/tmp/domotica_registry.txt` mantenuto dal Controller, che mappa ID → path della FIFO) e scrive direttamente il messaggio formattato sulla FIFO del device. Il device risponde sulla FIFO del Controller (o direttamente su stdout se è un override puro).

### Effetto sullo stato del sistema
Se un device viene messo in uno stato diverso da quello ordinato dal suo padre logico, il padre lo riconosce come **"manual override"** alla prossima query. Il primo nuovo comando del padre azzera l'override e risincronizza lo stato.

---

## 11. Concorrenza e consistenza dello stato

### Scenari critici da gestire

**Scenario 1: override manuale mentre il Controller sta inviando un comando**
- Il Controller non aspetta la risposta prima di tornare nel loop.
- Se arriva un override mentre il device sta processando un comando del Controller, vince **l'ultimo scritto sulla pipe** (le pipe sono FIFO, ordine garantito).
- Il device non deve mai trovarsi in uno stato indefinito: ogni comando lo porta a uno stato valido.

**Scenario 2: Hub che propaga a un figlio crashato**
- Mentre l'Hub manda comandi ai figli, uno non risponde (pipe rotta / processo morto).
- L'Hub deve rilevare l'errore (es. `write()` su pipe rotta → `SIGPIPE` o errore), segnalarlo al Controller, e continuare con gli altri figli senza bloccarsi.

**Scenario 3: Device crash inatteso**
- Il Controller ha installato un handler per `SIGCHLD`.
- All'arrivo del segnale usa `waitpid(-1, &status, WNOHANG)` per capire quale PID è morto.
- Rimuove quell'ID dalla tabella di routing e avvisa l'utente.

### La regola del delay (obbligatoria per il testing)
```c
// Ogni device, PRIMA di scrivere qualsiasi risposta:
sleep((rand() % 3) + 1);
```
Questo è lì apposta per far emergere bug di concorrenza nel Controller.

---

## 12. Gestione errori e crash

**Ogni funzione che può fallire deve restituire un codice di errore.** Niente ignorare i return value.

| Cosa verificare | Come |
|-----------------|------|
| `fork()` | Controllare che non ritorni -1 |
| `open()` su FIFO | Gestire il fallimento (device non esistente?) |
| `write()` su FIFO | Gestire `SIGPIPE` e `EPIPE` (destinatario morto) |
| `read()` da FIFO | Gestire EOF (scrittore ha chiuso la pipe) |
| Parsing dei messaggi | Formato non valido → ignorare o loggare, mai crashare |
| `waitpid()` nel handler `SIGCHLD` | Usare sempre `WNOHANG` nel loop |

**Nota su Bash:** usare sempre `$?` dopo i comandi critici. I codici di errore C devono avere lo stesso valore numerico nelle variabili Bash.

---

## 13. Struttura delle cartelle e build system

```
.
├── Makefile              # (o build.sh in alternativa)
├── build.sh
├── report.pdf            # Max 5 pagine
└── code/
    ├── src/
    │   ├── controller.c  # Loop select(), shell interattiva, gestione SIGCHLD
    │   ├── devices.c     # Logica comune a tutti i device (loop di ascolto, delay)
    │   ├── ipc_utils.c   # Funzioni helper: apri FIFO, scrivi msg, leggi msg, parsing
    │   └── models/
    │       ├── bulb.c
    │       ├── window.c
    │       ├── fridge.c
    │       ├── hub.c
    │       └── timer.c
    ├── include/
    │   ├── protocol.h    # Costanti protocollo, codici errore, formato messaggi
    │   └── common.h      # Strutture dati condivise (device_entry, routing_table, ecc.)
    └── manual_interaction.sh
```

### Target Makefile

```makefile
build:
    gcc -o build/controller src/controller.c src/ipc_utils.c ...
    gcc -o build/bulb src/models/bulb.c src/devices.c src/ipc_utils.c ...
    # ... un eseguibile per ogni tipo di device

clean:
    rm -f build/*
    rm -f /tmp/domotica_*.fifo     # CRITICO: pulisce pipe residue

run:
    ./build/controller $(ARGS)
```

---

## 14. Consegna

- **Nome file:** `Cognome1_Cognome2_Cognome3_Cognome4.tar.gz`
- **Contenuto obbligatorio:**
  - `report.pdf` (max 5 pagine: descrizione progetto, scelte di design, aspetti rilevanti)
  - cartella `code/` con tutto il sorgente
  - Makefile o `build.sh` funzionante su Ubuntu 24.04

- **Scadenze:** a scelta tra le sessioni: Giugno, Luglio, Settembre 2026 — Gennaio, Febbraio 2027.
  La sessione in cui si consegna è quella in cui si sostiene l'esame orale.

- **Attenzione:** il bottone "Submit" su Moodle **blocca definitivamente** la consegna. Non premerlo finché non siete sicuri.

---

## 15. Glossario rapido

| Termine | Significato nel progetto |
|---------|--------------------------|
| **Controller** | Processo principale; padre OS di tutti; espone la shell |
| **Control Device** | Hub o Timer; può avere figli logici e propaga comandi |
| **Interaction Device** | Bulb, Window, Fridge; foglie della gerarchia logica |
| **Gerarchia OS** | Relazione padre-figlio a livello di processi Unix (sempre piatta) |
| **Gerarchia logica** | Chi controlla chi; definita via `link`; mantenuta nelle routing table |
| **Routing table** | Struttura dati interna a ogni processo che mappa ID → pipe/stato |
| **FIFO** | Named Pipe in `/tmp/`; unico mezzo di comunicazione IPC |
| **Manual override** | Comando arrivato via `manual_interaction.sh`, bypassando il Controller |
| **select()** | Syscall usata dal Controller per monitorare stdin + pipe senza bloccarsi |
| **SIGCHLD** | Segnale inviato al Controller quando un figlio OS muore |
| **WNOHANG** | Flag di `waitpid()` per non bloccarsi se nessun figlio è morto ancora |
| **Delay simulato** | `sleep(rand()%3+1)` eseguito da ogni device prima di rispondere |
