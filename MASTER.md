# 🏠 Sistema di Domotica Emulato — Specifica Master

> **Documento unico e autoritativo per lo sviluppo.**
> Le specifiche del professore hanno **sempre priorità assoluta**.
> Le sezioni marcate con 🔧 contengono decisioni implementative del gruppo (Tommaso · Poli · Francesco) e regole di integrazione concordate — non devono essere contraddette senza discussione collettiva.

---

## Indice

1. [Struttura del Progetto e Valutazione](#1-struttura-del-progetto-e-valutazione)
2. [Architettura del Sistema](#2-architettura-del-sistema)
3. [Dispositivi — Specifiche Complete](#3-dispositivi--specifiche-complete)
4. [Gerarchia Logica e Linking](#4-gerarchia-logica-e-linking)
5. [Interazione Manuale Esterna](#5-interazione-manuale-esterna)
6. [Concorrenza](#6-concorrenza)
7. [Gestione Errori ed Edge Case](#7-gestione-errori-ed-edge-case)
8. [Interfacce Utente](#8-interfacce-utente)
9. [Contratto IPC 🔧](#9-contratto-ipc-)
10. [Build System](#10-build-system)
11. [Struttura del Repository 🔧](#11-struttura-del-repository-)
12. [Suddivisione dei Compiti 🔧](#12-suddivisione-dei-compiti-)
13. [Regole di Integrazione 🔧](#13-regole-di-integrazione-)
14. [Consegna](#14-consegna)

---

## 1. Struttura del Progetto e Valutazione

*(Fonte: specifiche ufficiali del professore)*

Il progetto vale **10 punti** sul voto totale del corso, con questa distribuzione:

| Componente | Peso |
|---|---|
| Codice (correttezza, qualità, aderenza alle specifiche) | 20% |
| Report (`report.pdf`, max 5 pagine) | 10% |
| Presentazione orale all'esame | 70% |

---

## 2. Architettura del Sistema

*(Fonte: specifiche ufficiali del professore)*

Ogni dispositivo **deve** essere rappresentato da un distinto processo OS.

### 2.1 Gerarchia OS (piatta)

La struttura a livello di Sistema Operativo è **sempre piatta**:

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
- Le informazioni specifiche sui figli **non devono essere cachate nel Controller**: devono essere recuperate interrogando attivamente i processi figlio via IPC.

**Esempio:**
```
Controller  (logico)
└── Hub 2   (logico: figlio del Controller)
    ├── Bulb 3
    └── Timer 4
        └── Fridge 5
```

---

## 3. Dispositivi — Specifiche Complete

*(Fonte: specifiche ufficiali del professore)*

### 3.1 Struttura base comune a tutti i dispositivi

Ogni dispositivo ha:
- **State**: rappresentazione dello stato corrente (es. on/off, open/closed).
- **Switches**: controlli a due posizioni (on/off) per modificare lo stato.
- **Registry**: insieme di parametri e attributi specifici del dispositivo (è possibile aggiungerne di personalizzati oltre a quelli obbligatori descritti di seguito).

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

**Override Resolution Rule (comune a tutti i Control Device):** qualsiasi nuovo comando inviato a un Control Device sovrascrive immediatamente lo stato di tutti i suoi figli, annullando qualsiasi override manuale precedente.

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
  - `perc`: percentuale di riempimento (0–100%) — **modificabile solo manualmente**
  - `temp`: temperatura interna corrente
  - `thermostat`: temperatura target — **modificabile solo manualmente**

---

## 4. Gerarchia Logica e Linking

*(Fonte: specifiche ufficiali del professore)*

Poiché tutti i processi sono figli OS del Controller, la gerarchia di sistema è puramente logica e definita dal routing IPC. Quando viene emesso il comando `link <id1> to <id2>`, **non è necessario terminare o riavviare i processi**. Invece:

1. Inviare un messaggio IPC a `id1` comunicandogli il nuovo padre logico (`id2`).
2. Inviare un messaggio IPC a `id2` (se è un Control Device) comunicandogli il nuovo figlio logico (`id1`).
3. I dispositivi devono aggiornare le proprie routing table/file descriptor interni, in modo che i futuri comandi e le query di stato fluiscano tra `id2` e `id1`.

Se `id2` non è un Control Device (cioè non è Controller, Hub o Timer), il comando `link` deve **fallire immediatamente** con un errore appropriato.

---

## 5. Interazione Manuale Esterna

*(Fonte: specifiche ufficiali del professore)*

Deve essere possibile inviare comandi a un device bypassando il Controller, per simulare un'azione fisica (es. premere fisicamente un pulsante su una Bulb smart).

Un eseguibile separato deve essere fornito:

```bash
./manual_interaction.sh <id> <command> [parameters]
```

Può essere implementato come script Bash o programma C, ma deve:
- Inviare comandi direttamente al processo device target, bypassando il routing del Controller.
- Per trovare il processo target, il sistema deve pubblicare informazioni di routing (es. il Controller mantiene un file `.registry` che mappa gli ID ai PID/Endpoint, oppure i device creano endpoint IPC con nome fisso).

---

## 6. Concorrenza

*(Fonte: specifiche ufficiali del professore)*

- Il Controller deve essere in grado di gestire comandi dalla shell interattiva **mentre contemporaneamente** elabora messaggi IPC dai device figlio. La scelta del meccanismo di concorrenza (thread, `select()`, I/O non bloccante) è libera, ma **deve essere giustificata nel report**.
- Gli override manuali possono arrivare in qualsiasi momento, anche mentre il Controller sta propagando un comando. Il sistema deve rimanere consistente: un device non deve mai essere lasciato in uno stato indefinito.
- Per permettere la verifica della concorrenza, ogni device deve attendere un **tempo casuale tra 1 e 3 secondi** prima di rispondere a qualsiasi comando IPC, per simulare il tempo di elaborazione.

> 🔧 **Decisione del gruppo:** Il Controller usa `select()` (no thread). Motivazione da includere nel report: evita race condition sullo stato condiviso, più semplice da debuggare, sufficiente per i volumi di traffico IPC del progetto. Il loop `select()` monitora simultaneamente `STDIN` e `/tmp/domotica_fifo_0`.

---

## 7. Gestione Errori ed Edge Case

*(Fonte: specifiche ufficiali del professore, con dettagli implementativi 🔧)*

### 7.1 Codici di Errore

Tutte le operazioni devono usare return code per indicare successo o fallimento. I codici devono essere definiti e usati **consistentemente** da C e Bash (non serve condivisione diretta, ma i valori devono coincidere):

```c
// In C (protocol.h)  🔧
#define OK                    0
#define DEVICE_NOT_FOUND      1
#define INVALID_COMMAND       2
#define LINK_FAILED           3
#define DEVICE_TYPE_MISMATCH  4
#define IPC_ERROR             5
#define CYCLE_DETECTED        6
```

```bash
# In Bash (stessi valori numerici)  🔧
OK=0
DEVICE_NOT_FOUND=1
INVALID_COMMAND=2
LINK_FAILED=3
DEVICE_TYPE_MISMATCH=4
IPC_ERROR=5
CYCLE_DETECTED=6
```

### 7.2 Rilevamento Crash dei Device

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

> 🔧 **Da verificare:** Se un device rileva un errore `EPIPE` (Broken Pipe) tentando di rispondere sulla FIFO del Controller, o se rileva che il PID del padre non è più attivo, deve procedere a una terminazione pulita delle proprie risorse. Da discutere in gruppo se implementare il polling del PID padre o affidarsi solo a EPIPE.

### 7.3 Edge Case Obbligatori da Gestire

*(Fonte: specifiche ufficiali del professore)*

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

## 8. Interfacce Utente

*(Fonte: specifiche ufficiali del professore)*

### 8.1 Shell Interattiva del Controller

Il processo Controller apre una shell interattiva con i seguenti comandi:

| Comando | Descrizione |
|---|---|
| `list` | Elenca tutti i device, il loro ID univoco e un sommario delle caratteristiche |
| `add <device>` | Crea un nuovo processo device (`fork` + `exec`) e stampa i dettagli. Es: `add bulb` |
| `del <id>` | Termina logicamente e fisicamente il processo. Se è un Control Device, propaga la terminazione a cascata via IPC a tutti i figli logici |
| `link <id1> to <id2>` | Aggiorna il routing IPC in modo che `id1` sia controllato logicamente da `id2` (solo Controller, Hub o Timer possono essere padre) |
| `switch <id> <label> <pos>` | Imposta lo switch `label` del device `id` alla posizione `pos`. Es: `switch 3 power on` |
| `info <id>` | Visualizza i dettagli completi del device |

### 8.2 Interazione Manuale Esterna

```bash
./scripts/manual_interaction.sh <id> <command> [parameters]
```

- Scrive direttamente sulla FIFO del device target (bypass del Controller).
- Deve leggere `/tmp/domotica_registry` per validare l'ID prima di aprire la FIFO.
- **Non riceve risposta** (EXT non attende).

---

## 9. Contratto IPC 🔧

> ⚠️ **Questo è il punto di integrazione più critico del progetto.** Il formato delle FIFO e dei messaggi è concordato e congelato. Nessuno può modificarlo senza approvazione collettiva.

### 9.1 Pattern: Una FIFO per Nodo

Ogni processo possiede e ascolta su **una singola Named Pipe** esclusiva.

**Convenzione di naming:**
```
/tmp/domotica_fifo_<ID>
```

- Il Controller ha sempre `ID = 0` → `/tmp/domotica_fifo_0`
- Device con ID 3 → `/tmp/domotica_fifo_3`

Le FIFO risiedono in `/tmp/` per garantire accesso rapido e pulizia semplice. Se il processo A (`ID 2`) vuole inviare un messaggio al processo B (`ID 3`): apre `/tmp/domotica_fifo_3` in scrittura e invia il messaggio. B lo legge dalla propria FIFO.

### 9.2 Registry dei Device Attivi

Il Controller mantiene `/tmp/domotica_registry` che mappa ogni ID attivo al tipo del device. Aggiornato ad ogni `add` e `del`.

**Formato:**
```
# /tmp/domotica_registry
# ID   TIPO
0      controller
2      hub
3      bulb
5      fridge
```

**A cosa serve:**
1. `manual_interaction.sh` legge questo file per validare l'ID prima di scrivere sulla FIFO.
2. Evita il blocco indefinito: su Linux, aprire una FIFO in scrittura quando il lettore non esiste blocca indefinitamente. Se l'ID non è nel registry, lo script termina con errore.

> 🔧 **Sincronizzazione del registry:** poiché è una risorsa condivisa tra Controller (scrittore) e `manual_interaction.sh` (lettore), l'accesso deve essere sincronizzato. Opzioni: file locking (`flock` in Bash, `flock()`/`fcntl()` in C), oppure il Controller crea un file temporaneo e lo rinomina atomicamente (`rename()`) solo a scrittura ultimata. **Da decidere in gruppo quale approccio usare.**

### 9.3 Formato dei Messaggi

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
- L'ID `0` è riservato al Controller: nessun device può averlo.
- I messaggi con `SENDER_ID = EXT` **non prevedono risposta**: `manual_interaction.sh` scrive e chiude senza attendere nulla.

### 9.4 Esempi di Messaggi

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

### 9.5 Protezione contro Blocco FIFO in `manual_interaction.sh`

```bash
timeout 2 bash -c "echo \"EXT|SWITCH|${ID}|${PAYLOAD}\" > /tmp/domotica_fifo_${ID}"
if [ $? -ne 0 ]; then
    echo "Errore: device ${ID} non risponde o non esiste." >&2
    exit 1
fi
```

### 9.6 Simulazione del Delay nei Device

Prima di scrivere qualsiasi risposta sulla FIFO verso il mittente, ogni device esegue:

```c
sleep((rand() % 3) + 1);
```

Questo è **obbligatorio** per testare la robustezza della concorrenza del Controller.

---

## 10. Build System

*(Fonte: specifiche ufficiali del professore, con note implementative 🔧)*

Il progetto deve supportare **o Makefile o `build.sh`** (da scegliere uno: verificare sul PDF originale del prof quale è richiesto o preferito).

### Target/Comandi obbligatori

| Target | Comportamento |
|---|---|
| `make build` / `./build.sh build` | Compila tutti i sorgenti C, genera gli eseguibili |
| `make clean` / `./build.sh clean` | Rimuove binari, **esegue `rm -f /tmp/domotica_*.fifo`** per pulire pipe residue, rimuove `/tmp/domotica_registry` |
| `make run` / `./build.sh run` | Avvia il Controller. Supporta variabile `ARGS` per argomenti opzionali (es. `make run ARGS="--num-cooks=5"`) |

> ⚠️ **Nota critica su `clean`:** la pulizia delle FIFO in `/tmp/` è essenziale. Pipe orfane da run precedenti possono causare comportamenti inspiegabili.

### Flag di Compilazione

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11
```

> 🔧 **Posizione del Makefile/build.sh:** deve essere nella directory radice dell'archivio consegnato, ma configurato per operare sui file in `code/`. I file oggetto (`.o`) e i binari finali devono essere generati in una sottodirectory dedicata (es. `code/build/` o `code/bin/`), per non sporcare la root.

---

## 11. Struttura del Repository 🔧

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

## 12. Suddivisione dei Compiti 🔧

### Membro 1 — Controller e I/O Multiplexing — Tommaso

**File di competenza:** `src/controller/`, `src/ipc/event_loop.c`, `src/utils/cleanup.c`

**Obiettivi:**
- Implementare la shell interattiva con tutti i comandi: `add`, `list`, `del`, `link`, `switch`, `info`.
- Gestire `fork()` + `exec()` in `commands.c` per istanziare i processi device.
- Costruire il loop `select()` in `event_loop.c` per monitorare simultaneamente stdin e la FIFO del Controller.
- Scrivere l'handler `SIGCHLD` in `cleanup.c` per rilevare crash dei figli, pulire le routing table e aggiornare il registry.
- Gestire la creazione e aggiornamento di `/tmp/domotica_registry` ad ogni `add` e `del`.

**Dipendenze:** usa le interfacce di `ipc.h`, `device.h`, `routing.h`.

---

### Membro 2 — Infrastruttura IPC e Routing — Poli

**File di competenza:** `src/ipc/` (escluso `event_loop.c`), `src/core/`

**Obiettivi:**
- Garantire apertura, scrittura e chiusura sicura delle FIFO in `fifo.c` (protezione da blocchi indefiniti).
- Implementare formattazione, parsing e serializzazione dei messaggi in `message.c` e `serialization.c`.
- Implementare la logica del comando `link` in `routing.c` e `hierarchy.c`:
  - Aggiornamento delle routing table e dei file descriptor.
  - Rilevamento e blocco di cicli nella gerarchia logica (A → B → A).
  - Rifiuto di link verso Interaction Device come padri.
- Garantire codici di errore strutturati (definiti in `protocol.h`) per tutte le operazioni IPC.

**Dipendenze:** definisce e mantiene le interfacce condivise `ipc.h` e `routing.h`.

---

### Membro 3 — Dispositivi, Concorrenza e Testing — Francesco

**File di competenza:** `src/devices/`, `src/utils/random_delay.c`, `scripts/`, `tests/`

**Obiettivi:**
- Implementare stati, switch e registry interni per tutti i device: `Hub`, `Timer`, `Bulb`, `Window`, `Fridge`.
- Applicare il delay casuale (1–3 s) prima di ogni risposta IPC tramite `random_delay.c`.
- Gestire in `hub.c`: aggregazione degli stati dei figli, rilevamento e segnalazione di *manual override*.
- Gestire in `timer.c`: logica di schedule, validazione orari, auto-propagazione.
- Gestire in `fridge.c`: auto-chiusura dopo `delay`, sola scrittura manuale per `perc` e `thermostat`.
- Scrivere `manual_interaction.sh`: validazione ID via registry, scrittura FIFO con `timeout`, gestione errori.
- Sviluppare script di test automatizzati in `tests/` per i corner case di §7.3.

**Dipendenze:** usa le interfacce di `device.h` e `ipc.h`.

---

## 13. Regole di Integrazione 🔧

1. **Prima cosa da fare insieme:** popolare `include/` con i file `.h` condivisi. **Nessuno scrive codice `.c` prima che le interfacce siano definite e approvate dal gruppo.**

2. Ogni membro lavora **solo sui propri file `.c`**, importando le intestazioni condivise.

3. **È vietato modificare un file `.h` senza notificare il gruppo.** Qualsiasi modifica alle interfacce può rompere compilazione, linking e compatibilità IPC di tutti i moduli.

4. **Prima di fare merge/push di modifiche strutturali:**
   - Testare la compilazione pulita con `make clean && make build`.
   - Verificare che `make clean` rimuova effettivamente le FIFO in `/tmp/`.

5. Il formato del registry (`/tmp/domotica_registry`) deve essere concordato tra Membro 1 (che lo scrive) e Membro 3 (che lo legge da `manual_interaction.sh`) prima di lavorare in parallelo.

6. Per conflitti o dubbi sulle specifiche: **le specifiche del professore hanno sempre la precedenza** su questo documento. In caso di dubbio, rileggere il PDF originale e/o chiedere al professore.

---

## 14. Consegna

*(Fonte: specifiche ufficiali del professore)*

### Formato dell'archivio

```
${cognome1}_${cognome2}_${cognome3}.tar.gz
```

L'ordine dei cognomi non è rilevante.

### Contenuto obbligatorio

- `report.pdf` — max 5 pagine. Deve coprire: architettura, scelte di design (inclusa la giustificazione per `select()` invece dei thread), e informazioni rilevanti sul progetto.
- Cartella `code/` con il codice sorgente C e Bash.
- `Makefile` e/o `build.sh` funzionanti su **Ubuntu 24.04**.

### Scadenze

Potete scegliere di consegnare a qualsiasi appello: **Giugno, Luglio, Settembre 2026** oppure **Gennaio, Febbraio 2027**. L'appello in cui si consegna è quello in cui si sosterrà l'esame (come gruppo). La deadline per ciascun appello è pubblicata su Moodle ed è non negoziabile (chiusura automatica). Il pulsante **"Submit"** su Moodle finalizza la consegna.

### Vincoli

- Solo **C e Bash** sono accettati (non C++, non Python, non altri linguaggi).
- Librerie di terze parti permesse solo se incluse nella consegna e senza problemi di compilazione.
- Soluzioni che si discostano dalle specifiche verranno penalizzate in proporzione alla gravità della deviazione.

---

*Ultima revisione: vedi git log. In caso di discrepanza tra questo documento e il PDF originale del professore, il PDF ha sempre la precedenza.*
