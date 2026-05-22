# 🏠 Progetto Operating Systems 2026: Domotica

Questo documento unifica l'architettura tecnica del progetto, le regole di comunicazione IPC, la struttura del repository e la suddivisione dei compiti tra i membri del gruppo.

> ⚠️ **Nessuno deve deviare da queste specifiche senza averne prima discusso con il gruppo.** Qualsiasi modifica a un file `.h` condiviso rompe potenzialmente compilazione, linking e integrazione IPC di tutti gli altri.

---

## Indice

1. [Il "Contratto" IPC](#1-il-contratto-ipc)
2. [Struttura del Repository](#2-struttura-del-repository)
3. [Suddivisione dei Compiti](#3-suddivisione-dei-compiti)
4. [Regole di Integrazione](#4-regole-di-integrazione)

---

## 1. Il "Contratto" IPC

L'errore più comune nei sistemi multi-processo è la cattiva gestione dei canali di comunicazione. Per questo si usa il pattern **"Una FIFO per Nodo"**.

### Topologia FIFO

**Non** avremo code separate di *in* e *out*. Ogni processo (`Controller`, `Hub`, `Bulb`, ecc.) possiede e ascolta su **una singola Named Pipe** esclusiva.

Le pipe sono create in `/tmp/` con la sintassi:

```
/tmp/domotica_fifo_<ID>
```

Il `Controller` ha `ID = 0`.

### Il Registry dei Device Attivi

Il Controller mantiene un file di testo `/tmp/domotica_registry` che mappa ogni ID al tipo del device corrispondente. Il file viene aggiornato ad ogni `add` e `del`.

**Formato:**
```
# /tmp/domotica_registry
# ID   TIPO
0      controller
2      hub
3      bulb
5      fridge
```

**A cosa serve:** `manual_interaction.sh` legge questo file prima di scrivere su una FIFO. Questo risolve due problemi:

1. **Validazione dell'ID:** se l'ID non è nel registry, lo script termina con errore invece di bloccarsi.
2. **Blocco su FIFO senza lettore:** su Linux, aprire una FIFO in scrittura quando il processo lettore non esiste **blocca indefinitamente**. Lo script usa `timeout` per proteggersi:

```bash
timeout 2 bash -c "echo \"EXT|SWITCH|${ID}|${PAYLOAD}\" > /tmp/domotica_fifo_${ID}"
if [ $? -ne 0 ]; then
    echo "Errore: device ${ID} non risponde o non esiste." >&2
    exit 1
fi
```

> ⚠️ **Questo è un punto di integrazione critico.** Il formato del registry deve essere concordato prima che il Membro 1 (che lo scrive) e il Membro 3 (che lo legge da `manual_interaction.sh`) inizino a lavorare in parallelo.



Se il processo A (`ID 2`) vuole inviare un comando al processo B (`ID 3`), apre `/tmp/domotica_fifo_3` in scrittura e invia il messaggio. B lo legge dalla sua unica FIFO.

### Formato dei Messaggi

Per garantire compatibilità con Bash, i payload binari (`struct` C) sono **vietati**. I messaggi sono stringhe terminated da `\n`:

```
SENDER_ID|COMMAND|TARGET_ID|PAYLOAD\n
```

| Campo | Valori | Descrizione |
|-------|--------|-------------|
| `SENDER_ID` | intero oppure `EXT` | ID numerico del mittente (0 = Controller, riservato); `EXT` per script Bash esterni |
| `COMMAND` | `SWITCH`, `LINK`, `INFO`, `DEL`, `STATUS` | Tipo di operazione |
| `TARGET_ID` | intero | ID del destinatario logico |
| `PAYLOAD` | stringa | Dati specifici del comando |

> **Nota sul `SENDER_ID`:** l'ID `0` è **riservato al Controller** e nessun device può averlo. Questo significa che il device ricevente può sempre distinguere se il mittente è il Controller (ID = 0), un altro device (ID > 0) o uno script esterno (`EXT`). Non serve un campo separato per il tipo di mittente.
>
> I messaggi con `SENDER_ID = EXT` **non prevedono risposta**: il `manual_interaction.sh` scrive e chiude, senza attendere nulla sulla FIFO del Controller.

### Esempi

```bash
# L'Hub 2 ordina al device 3 di accendersi
2|SWITCH|3|power,on\n

# Il Controller chiede lo stato al device 5
0|INFO|5|ALL\n

# Script esterno forza lo spegnimento bypassando il Controller (nessuna risposta attesa)
EXT|SWITCH|3|power,off\n
```

---

## 2. Struttura del Repository

```
.
├── Makefile                    # Build, clean (rimozione FIFO), run
│
├── scripts/
│   ├── manual_interaction.sh   # Interazione esterna manuale (Bash)
│   └── cleanup_ipc.sh          # Script di emergenza: kill processi e pulizia /tmp/
│
├── code/
│   ├── include/                # File .h condivisi — il "contratto" del progetto
│   │   ├── protocol.h          # Costanti ID, delimitatori, codici di errore
│   │   ├── ipc.h               # Prototipi funzioni di read/write sicure
│   │   ├── device.h            # Strutture dati astratte dei dispositivi
│   │   └── routing.h           # Strutture per la gestione della gerarchia logica
│   │
│   └── src/
│       ├── controller/         # Shell, fork(), exec(), I/O multiplexing
│       │   ├── main.c
│       │   ├── controller.c
│       │   ├── repl.c
│       │   ├── parser.c
│       │   └── commands.c
│       │
│       ├── ipc/                # Implementazione FIFO e loop eventi
│       │   ├── fifo.c
│       │   ├── message.c
│       │   ├── request_reply.c
│       │   ├── ipc_common.c
│       │   └── event_loop.c
│       │
│       ├── core/               # Routing, gerarchia logica, parsing
│       │   ├── routing.c
│       │   ├── hierarchy.c
│       │   └── serialization.c
│       │
│       ├── devices/            # Logica interna di ogni dispositivo
│       │   ├── bulb.c
│       │   ├── fridge.c
│       │   ├── window.c
│       │   ├── hub.c
│       │   └── timer.c
│       │
│       └── utils/
│           ├── random_delay.c  # sleep(rand()%3+1) prima di ogni risposta IPC
│           └── cleanup.c       # Handler SIGCHLD e pulizia risorse
│
├── tests/                      # Script automatizzati per corner case
│
└── report.pdf                  # Relazione finale (max 5 pagine, vale 10% del voto)
```

---

## 3. Suddivisione dei Compiti

### Membro 1 — Controller e I/O Multiplexing

**Directory di competenza:** `src/controller/`, `src/ipc/event_loop.c`, `src/utils/cleanup.c`

**Obiettivi:**

- Implementare la shell interattiva con i comandi: `add`, `list`, `del`, `link`, `switch`, `info`.
- Gestire `fork()` + `exec()` in `commands.c` per istanziare i processi device.
- Costruire il loop `select()` in `event_loop.c` per monitorare contemporaneamente stdin e la FIFO del Controller.
- Scrivere l'handler `SIGCHLD` in `cleanup.c` per rilevare crash dei figli e pulire le tabelle di routing.

---

### Membro 2 — Infrastruttura IPC e Routing

**Directory di competenza:** `src/ipc/` (escluso `event_loop.c`), `src/core/`

**Obiettivi:**

- Garantire apertura, scrittura e chiusura sicura delle FIFO in `fifo.c`.
- Implementare formattazione, parsing e serializzazione dei messaggi in `message.c` e `serialization.c`.
- Implementare la logica del comando `link` in `routing.c` e `hierarchy.c`:
  - aggiornamento delle routing table e dei file descriptor
  - rilevamento e blocco di cicli nella gerarchia logica (`A → B → A`)
  - rifiuto di link verso Interaction Device come padri
- Garantire codici di errore strutturati per tutte le operazioni IPC.

---

### Membro 3 — Dispositivi, Concorrenza e Testing

**Directory di competenza:** `src/devices/`, `src/utils/random_delay.c`, `scripts/`, `tests/`

**Obiettivi:**

- Implementare stati, switch e registri interni per tutti i device: `Hub`, `Timer`, `Bulb`, `Window`, `Fridge`.
- Applicare il delay casuale (`1-3s`) prima di ogni risposta IPC tramite `random_delay.c`.
- Gestire in `hub.c`:
  - aggregazione degli stati dei figli
  - rilevamento e segnalazione di *manual override* (stati discordanti)
- Scrivere `manual_interaction.sh` per simulare interventi fisici esterni.
- Sviluppare script di test automatizzati in `tests/` per i corner case (crash, override, cicli, link invalidi).

---

## 4. Regole di Integrazione

1. **Prima cosa da fare insieme:** popolare la cartella `include/` con i file `.h` condivisi. Nessuno può iniziare a scrivere `.c` prima che le interfacce siano definite e approvate dal gruppo.

2. Ogni membro lavora **solo sui propri file `.c`**, importando le intestazioni condivise.

3. **È vietato modificare un file `.h` senza notificare il gruppo.** Qualunque modifica alle interfacce può rompere compilazione, linking e compatibilità IPC di tutti i moduli.

4. Per conflitti o casi dubbi sulle specifiche, riferirsi prima alla [wiki del progetto](./PROGETTO_WIKI.md) e poi discutere nel gruppo.
