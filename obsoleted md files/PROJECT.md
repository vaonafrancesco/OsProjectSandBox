# 🏠 Progetto Operating Systems 2026 — Sistema di Domotica Emulato

> **⚠️ Documento ufficiale del gruppo.** Nessuno deve deviare da queste specifiche senza averne prima discusso con tutti. Qualsiasi modifica a un file `.h` condiviso può rompere compilazione, linking e IPC di tutti i moduli.

---

## Indice

1. [Contesto e Obiettivi](#1-contesto-e-obiettivi)
2. [Architettura del Sistema](#2-architettura-del-sistema)
3. [Dispositivi — Specifiche Dettagliate](#3-dispositivi--specifiche-dettagliate)
4. [Contratto IPC](#4-contratto-ipc)
5. [Concorrenza e Gestione Asincrona](#5-concorrenza-e-gestione-asincrona)
6. [Gestione Errori ed Edge Case](#6-gestione-errori-ed-edge-case)
7. [Interfacce Utente](#7-interfacce-utente)
8. [Build System](#8-build-system)
9. [Struttura del Repository](#9-struttura-del-repository)
10. [Suddivisione dei Compiti](#10-suddivisione-dei-compiti)
11. [Regole di Integrazione](#11-regole-di-integrazione)
12. [Consegna](#12-consegna)

---

## 1. Contesto e Obiettivi

Il progetto richiede l'implementazione di un sistema domotico emulato in cui **ogni dispositivo è un processo OS distinto**. La comunicazione e la sincronizzazione avvengono tramite primitive Unix (IPC, segnali, `select`).

### Obiettivi tecnici principali

- Gestione avanzata dei processi: `fork`, `exec`, `waitpid`.
- IPC robusta tramite Named Pipes (FIFO).
- Concorrenza tramite I/O multiplexing (`select`) — no thread nel Controller.
- Interoperabilità tra C e script Bash.
- Simulazione di interventi manuali esterni che bypassano il Controller.

### Peso nella valutazione

| Componente | Peso |
|---|---|
| Codice (correttezza, qualità, aderenza alle specifiche) | 20% |
| Report (`report.pdf`, max 5 pagine) | 10% |
| Presentazione orale all'esame | 70% |

---

## 2. Architettura del Sistema

### 2.1 Gerarchia OS (piatta)

A livello di Sistema Operativo, la struttura è **sempre piatta**:

- Il **Controller** è il processo radice e padre OS di tutti gli altri processi.
- Ogni dispositivo aggiunto (`add`) viene creato dal Controller con `fork` + `exec` come figlio diretto.
- **Non esiste parentela OS tra dispositivi**: un Hub non è padre OS di una Bulb. Questo semplifica la gestione dei segnali e la pulizia del sistema.

```
Controller (PID: padre di tutti)
├── Bulb    (PID: figlio diretto)
├── Hub     (PID: figlio diretto)
├── Fridge  (PID: figlio diretto)
└── Timer   (PID: figlio diretto)
```

### 2.2 Gerarchia Logica (applicativa, dinamica)

La gerarchia "chi controlla chi" è **puramente logica** e indipendente dall'OS:

- Viene definita con il comando `link`.
- È mantenuta tramite **tabelle di routing interne** a ogni processo (chi è il mio padre logico, chi sono i miei figli logici).
- Un comando inviato a un Control Device viene propagato via IPC a tutti i suoi figli logici.

**Esempio:**
```
Controller  (logico)
└── Hub 2   (logico: figlio del Controller)
    ├── Bulb 3
    └── Timer 4
        └── Fridge 5
```

---

## 3. Dispositivi — Specifiche Dettagliate

### 3.1 Struttura base comune a tutti i dispositivi

Ogni dispositivo ha:
- **State**: rappresentazione dello stato corrente (es. on/off, open/closed).
- **Switches**: controlli a due posizioni (on/off) per modificare lo stato.
- **Registry**: insieme di parametri e attributi specifici del dispositivo.

---

### 3.2 Control Devices (propagano comandi ai figli logici)

In modalità **write**, il comando viene propagato via IPC a tutti i figli logici.  
In modalità **read**, lo stato viene recuperato attivamente interrogando i figli via IPC (nessun caching).

#### Controller
- **State:** on/off
- **Switches:** `main` (on/off per alimentare l'intero sistema)
- **Registry:** `num` (numero di device figli direttamente connessi)
- **Vincoli:** unico, non può essere eliminato, padre OS di tutti gli altri processi.

#### Hub
- **State:** rispecchia lo stato dei figli. Lo stato è considerato **consistente** solo dopo che un comando `switch` è stato propagato con successo a tutti i figli (tutti condividono lo stesso stato).
- **Switches:** eredita quelli dei figli.
- **Registry:** eredita i registry dei figli.
- **Override:** se i figli hanno stati discordanti (es. a causa di un intervento manuale), interrogare l'Hub deve restituire `"manual override"` invece di uno stato definitivo.

#### Timer
- **State/Switches:** rispecchia lo stato del singolo device connesso (stessa logica di consistenza dell'Hub).
- **Registry:**
  - `begin`: orario di attivazione (formato `HH:MM`)
  - `end`: orario di disattivazione (formato `HH:MM`)
- **Edge case da gestire:** `begin > end`, orari nel passato, formato non valido.

**Regola Override comune a tutti i Control Device:** qualsiasi nuovo comando inviato a un Control Device sovrascrive immediatamente lo stato di tutti i suoi figli, annullando qualsiasi override manuale precedente.

---

### 3.3 Interaction Devices (attuatori/sensori finali)

#### Bulb
- **State:** on/off
- **Switches:** `power` (on/off)
- **Registry:** `time` (tempo totale di utilizzo/accensione)

#### Window
- **State:** open/closed
- **Switches:** `open` e `close` (on/off). **Nota:** tornano immediatamente a `off` dopo essere stati attivati (impulso).
- **Registry:** `time` (tempo totale rimasto aperta)

#### Fridge
- **State:** open/closed
- **Switches:** `open` / `close` (on/off)
- **Registry:**
  - `time`: tempo rimasto aperto
  - `delay`: tempo dopo cui si chiude automaticamente
  - `perc`: percentuale di riempimento (0–100%) — modificabile solo manualmente
  - `temp`: temperatura interna corrente
  - `thermostat`: temperatura target — modificabile solo manualmente

---

## 4. Contratto IPC

> ⚠️ **Questo è il punto di integrazione più critico del progetto.** Il formato delle FIFO e dei messaggi deve essere concordato e congelato prima che chiunque inizi a scrivere codice `.c`.

### 4.1 Pattern: Una FIFO per Nodo

Ogni processo possiede e ascolta su **una singola Named Pipe** esclusiva. Non esistono code separate per input e output.

**Convenzione di naming:**
```
/tmp/domotica_fifo_<ID>
```

Il Controller ha sempre `ID = 0`:
```
/tmp/domotica_fifo_0   ← FIFO del Controller
/tmp/domotica_fifo_3   ← FIFO del device con ID 3
```

Le FIFO risiedono in `/tmp/` per garantire accesso rapido e pulizia semplice.

Se il processo A (`ID 2`) vuole inviare un messaggio al processo B (`ID 3`): apre `/tmp/domotica_fifo_3` in scrittura e invia il messaggio. B lo legge dalla propria FIFO.

### 4.2 Registry dei Device Attivi

Il Controller mantiene un file di testo `/tmp/domotica_registry` che mappa ogni ID attivo al tipo del device. Viene aggiornato ad ogni `add` e `del`.

**Formato:**
```
# /tmp/domotica_registry
# ID   TIPO
0      controller
2      hub
3      bulb
5      fridge
```

N.B. DA VERIFICARE: "Poiché il file di registry è una risorsa condivisa tra il Controller (scrittore) e lo script di interazione manuale (lettore), l'accesso deve essere obbligatoriamente sincronizzato per evitare race condition. È necessario implementare un meccanismo di file locking (utilizzando flock nello script Bash e flock() o fcntl() nel codice C). In alternativa, il Controller deve garantire l'atomicità della scrittura creando un file temporaneo e rinominandolo (rename) solo a scrittura ultimata, evitando che lo script legga dati parziali o corrotti."

**A cosa serve:**
1. `manual_interaction.sh` legge questo file per validare l'ID prima di scrivere sulla FIFO.
2. Evita il blocco indefinito: su Linux, aprire una FIFO in scrittura quando il lettore non esiste **blocca indefinitamente**. Se l'ID non è nel registry, lo script termina con errore invece di bloccarsi.

### 4.3 Formato dei Messaggi

Per garantire compatibilità con Bash, i payload binari (`struct` C) sono **vietati**. Tutti i messaggi sono stringhe terminate da `\n`:

```
SENDER_ID|COMMAND|TARGET_ID|PAYLOAD\n
```

| Campo | Valori | Descrizione |
|---|---|---|
| `SENDER_ID` | intero oppure `EXT` | ID del mittente (0 = Controller, riservato). `EXT` per script Bash esterni |
| `COMMAND` | `SWITCH`, `LINK`, `INFO`, `DEL`, `STATUS` | Tipo di operazione |
| `TARGET_ID` | intero | ID del destinatario logico |
| `PAYLOAD` | stringa | Dati specifici del comando (es. `power,on` oppure `12:00,14:00`) |

**Note:**
- L'ID `0` è riservato al Controller: nessun device può averlo. Il device ricevente può sempre distinguere mittente Controller (ID = 0), altro device (ID > 0) o script esterno (`EXT`).
- I messaggi con `SENDER_ID = EXT` **non prevedono risposta**: `manual_interaction.sh` scrive e chiude senza attendere nulla sulla FIFO del Controller.

### 4.4 Esempi di messaggi

```bash
# Il Controller chiede lo stato al device 5
0|INFO|5|ALL\n

# L'Hub 2 ordina al device 3 di accendersi
2|SWITCH|3|power,on\n

# Script esterno forza lo spegnimento bypassando il Controller (nessuna risposta attesa)
EXT|SWITCH|3|power,off\n

# Risposta del device 3 al Controller (dopo il delay casuale)
3|STATUS|0|on,time:120\n
```

### 4.5 Protezione contro blocco FIFO in `manual_interaction.sh`

```bash
timeout 2 bash -c "echo \"EXT|SWITCH|${ID}|${PAYLOAD}\" > /tmp/domotica_fifo_${ID}"
if [ $? -ne 0 ]; then
    echo "Errore: device ${ID} non risponde o non esiste." >&2
    exit 1
fi
```

---

## 5. Concorrenza e Gestione Asincrona

### 5.1 I/O Multiplexing con `select()` nel Controller

Il Controller **non deve usare thread**. Utilizza un ciclo principale basato su `select()` per monitorare simultaneamente:

1. `STDIN`: comandi digitati dall'utente nella shell interattiva.
2. `/tmp/domotica_fifo_0`: messaggi in arrivo dai device figli.

### 5.2 Comportamento non bloccante

Poiché i device rispondono con un ritardo di 1–3 secondi, il Controller **non si blocca** dopo aver inviato un comando. Torna immediatamente nel loop della `select` e processa la risposta solo quando il descrittore della FIFO del Controller diventa pronto in lettura.

### 5.3 Simulazione del Delay nei Device

Prima di scrivere qualsiasi risposta sulla FIFO verso il mittente, ogni device esegue:

```c
sleep((rand() % 3) + 1);
```

Questo è obbligatorio per testare la robustezza della concorrenza del Controller.

### 5.4 Manual Override e Consistenza

Un intervento manuale può arrivare mentre il Controller sta propagando un comando. Regola: **l'ultimo comando ricevuto vince**. Un device non deve mai trovarsi in uno stato indefinito. La consistenza deve essere garantita sempre.

---

## 6. Gestione Errori ed Edge Case

### 6.1 Codici di Errore

Tutte le operazioni devono usare return code per indicare successo o fallimento. I codici devono essere definiti e usati consistentemente da C e Bash (non serve condivisione diretta, ma i valori devono coincidere):

```c
// In C (protocol.h)
#define OK                    0
#define DEVICE_NOT_FOUND      1
#define INVALID_COMMAND       2
#define LINK_FAILED           3
#define DEVICE_TYPE_MISMATCH  4
#define IPC_ERROR             5
#define CYCLE_DETECTED        6
```

```bash
# In Bash (stessi valori numerici)
OK=0
DEVICE_NOT_FOUND=1
INVALID_COMMAND=2
# ...
```

### 6.2 Rilevamento Crash dei Device

Il Controller installa un handler per `SIGCHLD`:

```c
signal(SIGCHLD, sigchld_handler);
// oppure sigaction(SIGCHLD, ...)
```

All'arrivo del segnale, usa `waitpid(..., WNOHANG)` per identificare quale processo è terminato. Deve poi:
- Rimuovere l'ID dalle tabelle di routing interne.
- Aggiornare `/tmp/domotica_registry`.
- Rimuovere la FIFO residua del device morto.
- Segnalare l'anomalia all'utente nella shell.

N.B. DA VERIFICARE SE E' VERO: "Oltre al crash dei figli, il sistema deve gestire la terminazione imprevista del Controller. I processi device non devono rimanere orfani in esecuzione: ogni dispositivo deve monitorare lo stato del padre (Controller). Se un dispositivo rileva un errore di tipo EPIPE (Broken Pipe) tentando di rispondere sulla FIFO del Controller, o se rileva tramite logica interna che il PID del padre non è più attivo, deve procedere immediatamente a una terminazione pulita delle proprie risorse e alla chiusura del processo."

### 6.3 Edge Case Obbligatori da Gestire

| Scenario | Comportamento atteso |
|---|---|
| Manual override mentre il Controller invia un comando allo stesso device | L'ultimo comando ricevuto vince; stato sempre consistente |
| Crash imprevisto di un device (es. `SIGKILL`) | Controller rileva via `SIGCHLD`, pulisce routing e registry |
| `link` di un Interaction Device come padre | Comando fallisce immediatamente con errore esplicito |
| `link` di un device a se stesso | Comando fallisce con errore |
| Ciclo nella gerarchia logica (A → B → A) | Rilevamento e blocco del ciclo, errore esplicito |
| `del` di un Hub con figli | Eliminazione a cascata di tutti i figli logici via IPC |
| `switch` su un Hub con un figlio crashato | Hub segnala il fallimento senza bloccarsi (timeout o errore IPC) |
| Orari Timer non validi (`begin > end`, formato errato, orari nel passato) | Errore esplicito, comando rifiutato |

---

## 7. Interfacce Utente

### 7.1 Shell Interattiva del Controller

Il processo Controller apre una shell interattiva con i seguenti comandi:

| Comando | Descrizione |
|---|---|
| `list` | Elenca tutti i device, il loro ID univoco e un sommario delle caratteristiche |
| `add <device>` | Crea un nuovo processo device (`fork` + `exec`) e stampa i dettagli. Es: `add bulb` |
| `del <id>` | Termina logicamente e fisicamente il processo. Se è un Control Device, propaga la terminazione a cascata via IPC a tutti i figli logici |
| `link <id1> to <id2>` | Aggiorna il routing IPC in modo che `id1` sia controllato logicamente da `id2` (solo Controller, Hub o Timer possono essere padre) |
| `switch <id> <label> <pos>` | Imposta lo switch `label` del device `id` alla posizione `pos`. Es: `switch 3 power on` |
| `info <id>` | Visualizza i dettagli completi del device |

**Come funziona `link` senza uccidere processi:**
1. Invia un messaggio IPC a `id1` comunicandogli il nuovo padre logico (`id2`).
2. Invia un messaggio IPC a `id2` comunicandogli il nuovo figlio logico (`id1`).
3. Entrambi i processi aggiornano le proprie routing table interne.

### 7.2 Interazione Manuale Esterna

Un eseguibile separato permette di simulare azioni fisiche su un device, **bypassando il Controller**:

```bash
./scripts/manual_interaction.sh <id> <command> [parameters]
```

- Può essere implementato come script Bash o programma C.
- Scrive direttamente sulla FIFO del device target.
- **Non riceve risposta** (EXT non attende).
- Deve leggere `/tmp/domotica_registry` per validare l'ID prima di aprire la FIFO.
- Deve proteggersi dal blocco su FIFO senza lettore (vedi §4.5).

---

## 8. Build System

Il progetto deve supportare **o Makefile o `build.sh`** (da scegliere uno dei due secondo le specifiche(controllare sul pdf originale del prof che cosa dice )).

### Target/Comandi

| Target | Comportamento |
|---|---|
| `make build` / `./build.sh build` | Compila tutti i sorgenti C, genera gli eseguibili |
| `make clean` / `./build.sh clean` | Rimuove binari, **esegue `rm -f /tmp/domotica_*.fifo`** per pulire pipe residue, rimuove `/tmp/domotica_registry` |
| `make run` / `./build.sh run` | Avvia il Controller. Supporta variabile `ARGS` per argomenti opzionali |

**Nota critica su `clean`:** la pulizia delle FIFO in `/tmp/` è essenziale. Pipe orfane da run precedenti possono causare comportamenti inspiegabili.

### Flag di compilazione consigliati

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11

NOTA OPERATIVA DA VERIFICARE: "Il Makefile (o lo script build.sh) deve essere posizionato nella directory radice dell'archivio consegnato. Tuttavia, deve essere configurato per operare esclusivamente sui file contenuti nella cartella code/. La compilazione deve essere strutturata in modo da non 'sporcare' la root: i file oggetto (.o) e i binari finali devono essere generati all'interno di una sottodirectory dedicata (es. code/build/ o code/bin/), garantendo l'ordine richiesto dalle specifiche di consegna."
```

---

## 9. Struttura del Repository

```
.
├── Makefile
├── build.sh
├── report.pdf                     # Relazione finale (max 5 pagine)
│
├── scripts/
│   ├── manual_interaction.sh      # Interazione esterna manuale (Bash)
│   └── cleanup_ipc.sh             # Script di emergenza: kill processi e pulizia /tmp/
│
├── code/
│   ├── include/                   # ← File .h condivisi: il "contratto" del progetto
│   │   ├── protocol.h             # Costanti ID, delimitatori, codici di errore
│   │   ├── ipc.h                  # Prototipi funzioni di read/write sicure su FIFO
│   │   ├── device.h               # Strutture dati astratte dei dispositivi
│   │   └── routing.h              # Strutture per la gestione della gerarchia logica
│   │
│   └── src/
│       ├── controller/            # Shell, fork(), exec(), I/O multiplexing
│       │   ├── main.c
│       │   ├── controller.c
│       │   ├── repl.c             # Read-Eval-Print Loop della shell
│       │   ├── parser.c           # Parsing dei comandi utente
│       │   └── commands.c         # Esecuzione comandi (add, del, link, switch, info...)
│       │
│       ├── ipc/                   # Infrastruttura FIFO e loop eventi
│       │   ├── fifo.c             # Apertura, scrittura, chiusura sicura delle FIFO
│       │   ├── message.c          # Formattazione e parsing dei messaggi
│       │   ├── request_reply.c    # Logica richiesta/risposta
│       │   ├── ipc_common.c       # Utility IPC condivise
│       │   └── event_loop.c       # Loop select() del Controller
│       │
│       ├── core/                  # Routing, gerarchia logica, serializzazione
│       │   ├── routing.c          # Tabelle di routing, aggiornamento padre/figlio
│       │   ├── hierarchy.c        # Rilevamento cicli, validazione link
│       │   └── serialization.c    # Serializzazione/deserializzazione messaggi
│       │
│       ├── devices/               # Logica interna di ogni dispositivo
│       │   ├── bulb.c
│       │   ├── fridge.c
│       │   ├── window.c
│       │   ├── hub.c
│       │   └── timer.c
│       │
│       └── utils/
│           ├── random_delay.c     # sleep(rand()%3+1) prima di ogni risposta IPC
│           └── cleanup.c          # Handler SIGCHLD e pulizia risorse
│
└── tests/                         # Script automatizzati per corner case
```

---

## 10. Suddivisione dei Compiti

### Membro 1 — Controller e I/O Multiplexing - Tommaso

**File di competenza:** `src/controller/`, `src/ipc/event_loop.c`, `src/utils/cleanup.c`

**Obiettivi:**

- Implementare la shell interattiva con tutti i comandi: `add`, `list`, `del`, `link`, `switch`, `info`.
- Gestire `fork()` + `exec()` in `commands.c` per istanziare i processi device.
- Costruire il loop `select()` in `event_loop.c` per monitorare simultaneamente stdin e la FIFO del Controller.
- Scrivere l'handler `SIGCHLD` in `cleanup.c` per rilevare crash dei figli, pulire le routing table e aggiornare il registry.
- Gestire la creazione e aggiornamento di `/tmp/domotica_registry` ad ogni `add` e `del`.

**Dipendenze:** usa le interfacce di `ipc.h`, `device.h`, `routing.h` definite con il gruppo.

---

### Membro 2 — Infrastruttura IPC e Routing - Poli

**File di competenza:** `src/ipc/` (escluso `event_loop.c`), `src/core/`

**Obiettivi:**

- Garantire apertura, scrittura e chiusura sicura delle FIFO in `fifo.c` (includendo protezione da blocchi indefiniti).
- Implementare formattazione, parsing e serializzazione dei messaggi in `message.c` e `serialization.c`.
- Implementare la logica del comando `link` in `routing.c` e `hierarchy.c`:
  - Aggiornamento delle routing table e dei file descriptor.
  - Rilevamento e blocco di cicli nella gerarchia logica (`A → B → A`).
  - Rifiuto di link verso Interaction Device come padri.
- Garantire codici di errore strutturati (definiti in `protocol.h`) per tutte le operazioni IPC.

**Dipendenze:** definisce e mantiene le interfacce condivise `ipc.h` e `routing.h`.

---

### Membro 3 — Dispositivi, Concorrenza e Testing - Francesco

**File di competenza:** `src/devices/`, `src/utils/random_delay.c`, `scripts/`, `tests/`

**Obiettivi:**

- Implementare stati, switch e registry interni per tutti i device: `Hub`, `Timer`, `Bulb`, `Window`, `Fridge`.
- Applicare il delay casuale (1–3 s) prima di ogni risposta IPC tramite `random_delay.c`.
- Gestire in `hub.c`:
  - Aggregazione degli stati dei figli.
  - Rilevamento e segnalazione di *manual override* (stati discordanti tra figli).
- Gestire in `timer.c`: logica di schedule, validazione orari, auto-propagazione.
- Gestire in `fridge.c`: auto-chiusura dopo `delay`, sola scrittura manuale per `perc` e `thermostat`.
- Scrivere `manual_interaction.sh`: validazione ID via registry, scrittura FIFO con `timeout`, gestione errori.
- Sviluppare script di test automatizzati in `tests/` per i corner case elencati in §6.3.

**Dipendenze:** usa le interfacce di `device.h` e `ipc.h`.

---

## 11. Regole di Integrazione

1. **Prima cosa da fare insieme:** popolare `include/` con i file `.h` condivisi. **Nessuno scrive codice `.c` prima che le interfacce siano definite e approvate dal gruppo.**

2. Ogni membro lavora **solo sui propri file `.c`**, importando le intestazioni condivise.

3. **È vietato modificare un file `.h` senza notificare il gruppo.** Qualsiasi modifica alle interfacce può rompere compilazione, linking e compatibilità IPC di tutti i moduli.

4. **Prima di fare merge/push di modifiche strutturali:**
   - Testare la compilazione pulita con `make clean && make build`.
   - Verificare che `make clean` rimuova effettivamente le FIFO in `/tmp/`.

5. Il formato del registry (`/tmp/domotica_registry`) deve essere concordato tra Membro 1 (che lo scrive) e Membro 3 (che lo legge da `manual_interaction.sh`) prima di iniziare a lavorare in parallelo.

6. Per conflitti o dubbi sulle specifiche, riferirsi prima a questo documento e poi discutere nel gruppo.

---

## 12. Consegna

### Formato dell'archivio

```
${cognome1}_${cognome2}_${cognome3}.tar.gz
```

L'ordine dei cognomi non è rilevante.

### Contenuto obbligatorio

- `report.pdf` — max 5 pagine. Deve coprire: architettura, scelte di design (inclusa la giustificazione per `select()` invece dei thread), e informazioni rilevanti sul progetto.
- Cartella `code/` con il codice sorgente C e Bash.
- `Makefile` e/o `build.sh` funzionanti su Ubuntu 24.04.

### Scadenze

- La consegna può avvenire a qualsiasi appello: **Giugno, Luglio, Settembre 2026** oppure **Gennaio, Febbraio 2027**.
- L'appello in cui si consegna è quello in cui si sosterrà l'esame (come gruppo).
- La deadline per ciascun appello è pubblicata su Moodle ed è **non negoziabile** (chiusura automatica).
- Si può caricare, eliminare e ricaricare il progetto quante volte si vuole fino alla scadenza. Il pulsante **"Submit"** su Moodle finalizza la consegna e blocca la pagina.

### Vincoli

- Solo C e Bash sono accettati (non C++, non Python, non altri linguaggi).
- Librerie di terze parti sono permesse solo se incluse nella consegna e non causano problemi di compilazione.
- Soluzioni che si discostano dalle specifiche verranno penalizzate in proporzione alla gravità della deviazione.
