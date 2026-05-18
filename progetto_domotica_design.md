markdown_content = """# Progetto Sistemi Operativi 2026 - Sistema di Domotica Emulato

## 1. Introduzione e Obiettivi
Il presente documento definisce le specifiche architettoniche e le regole di design per lo sviluppo del progetto del corso di Operating Systems. L'obiettivo è realizzare un sistema domotico distribuito su più processi, dove la comunicazione e la sincronizzazione avvengono tramite primitive di sistema Unix.

### Obiettivi Primari:
* Gestione avanzata dei processi (`fork`, `exec`, `waitpid`).
* Implementazione di Inter-Process Communication (IPC) robusta.
* Gestione della concorrenza tramite I/O multiplexing (`select`).
* Interoperabilità tra linguaggio C e script Bash.

---

## 2. Architettura del Sistema

### 2.1 Gerarchia dei Processi (Livello OS)
A livello di Sistema Operativo, l'architettura è **piatta**.
* **Controller:** È il processo radice e "padre" di tutti gli altri processi.
* **Dispositivi:** Ogni dispositivo aggiunto (Bulb, Hub, ecc.) viene creato dal Controller come figlio diretto.
* **Vincolo:** Non esiste una gerarchia di parentela OS tra dispositivi (es. un Hub non è padre OS di una Bulb), per semplificare la gestione dei segnali e la pulizia del sistema.

### 2.2 Gerarchia Logica (Livello Applicativo)
La gerarchia "chi controlla chi" è puramente **logica** e dinamica.
* Viene definita tramite il comando `link`.
* È mantenuta attraverso **tabelle di routing** interne a ogni processo che mappano gli ID dei figli logici.

---

## 3. Inter-Process Communication (IPC)

### 3.1 Scelta Tecnica: Named Pipes (FIFO)
Abbiamo optato per le **Named Pipes (FIFO)** come unica primitiva di comunicazione.
* **Motivazione:** Permettono una facile interazione manuale tramite script Bash (`manual_interaction.sh`) via semplici operazioni di redirect del testo (`echo > pipe`).
* **Posizionamento:** Tutte le pipe devono risiedere in `/tmp/` per garantire accesso rapido.

### 3.2 Convenzione di Naming
Per evitare collisioni e permettere ai processi di trovarsi, useremo la seguente convenzione:
* ` /tmp/domotica_ctrl.fifo`: Pipe in cui il Controller riceve notifiche dai figli.
* `/tmp/domotica_<ID>.fifo`: Pipe dedicata ad ogni singolo dispositivo (ID univoco) per ricevere comandi dal Controller o da interazioni manuali.

---

## 4. Protocollo di Comunicazione (Application Layer)

Per garantire compatibilità tra C e Bash, il protocollo è **testuale** (stringhe delimitate). Ogni messaggio deve terminare con un carattere di newline (`\\n`).

### 4.1 Formato del Messaggio
`SENDER_TYPE:SENDER_ID|COMMAND|TARGET_ID|PAYLOAD`

* **SENDER_TYPE:** `CTRL` (Controller), `DEV` (Dispositivo), `EXT` (Manuale/Bash).
* **COMMAND:** `SWITCH`, `LINK`, `INFO`, `DEL`, `STATUS`.
* **PAYLOAD:** Dati specifici (es. `power,on` o `12:00,14:00`).

### 4.2 Esempio di Flusso
1.  **Richiesta Info:** `CTRL:0|INFO|3|ALL\n` inviato sulla pipe del dispositivo 3.
2.  **Risposta (dopo delay):** `DEV:3|STATUS|0|on,temp:20\n` inviato sulla pipe del Controller.

---

## 5. Design del Controller e Concorrenza

### 5.1 I/O Multiplexing con `select()`
Il Controller non deve usare thread. Utilizzerà un ciclo principale basato su `select()` per monitorare contemporaneamente:
1.  `STDIN`: Per i comandi digitati dall'utente nella shell interattiva.
2.  `FIFO_CTRL`: Per i messaggi in arrivo dai dispositivi figli.

### 5.2 Gestione Asincrona
Poiché i dispositivi rispondono con un ritardo di 1-3 secondi, il Controller **non deve bloccarsi** dopo aver inviato un comando. Deve tornare nel loop della `select` e processare la risposta solo quando il descrittore della pipe di ritorno diventa pronto.

---

## 6. Implementazione dei Dispositivi

### 6.1 Struttura Base del Nodo (C)
Ogni dispositivo deve gestire:
* **Identità:** ID univoco e tipo.
* **Stato Interno:** Variabili per on/off, timer, sensori.
* **Routing:** ID del padre logico e lista degli ID dei figli logici (per Control Devices).

### 6.2 Simulazione del Delay
Prima di scrivere qualsiasi risposta sulla pipe verso il Controller, ogni dispositivo deve eseguire:
`sleep((rand() % 3) + 1);`
Questo serve a testare la robustezza della concorrenza del Controller.

---

## 7. Gestione Errori e Casi Limite

### 7.1 Rilevamento Crash
Il Controller deve installare un handler per il segnale `SIGCHLD`.
* All'arrivo del segnale, userà `waitpid(..., WNOHANG)` per identificare quale processo è morto.
* Se un processo muore, il Controller deve rimuovere il suo ID dalle tabelle di routing e segnalare l'anomalia all'utente.

### 7.2 Manual Override & Consistenza
Se arriva un comando manuale mentre il Controller sta inviando un comando, l'ultimo comando ricevuto "vince". Lo stato deve sempre essere coerente; l'uso di file di stato o mutex (se necessario tra processi) deve essere valutato per evitare stati indefiniti.

---

## 8. Build System e Regole di Compilazione

Il progetto deve essere compilato tramite un `Makefile` con i seguenti target:
* `make build`: Compila tutti i sorgenti C e genera gli eseguibili.
* `make clean`: 
    1.  Rimuove i binari.
    2.  **Cruciale:** Esegue `rm -f /tmp/domotica_*.fifo` per pulire il sistema da pipe residue.
* `make run`: Avvia il Controller.

---

## 9. Struttura delle Cartelle Suggerita
```text
.
├── Makefile
├── build.sh
├── report.pdf
└── code/
    ├── src/
    │   ├── controller.c      # Logica della shell e select()
    │   ├── devices.c         # Logica comune ai dispositivi
    │   ├── ipc_utils.c       # Funzioni per lettura/scrittura pipe
    │   └── models/           # Implementazione specifica (bulb, fridge, etc.)
    ├── include/
    │   ├── protocol.h        # Definizioni costanti e protocollo
    │   └── common.h          # Strutture dati condivise
    └── manual_interaction.sh # Script Bash per interazione esterna