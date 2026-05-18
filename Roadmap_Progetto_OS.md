# 🏠 Progetto Operating Systems 2026: Domotica - Roadmap di Gruppo

Questo documento definisce i compiti per ogni membro del gruppo, garantendo che ognuno possa lavorare in autonomia (da casa) senza dipendere dal codice degli altri fino alla fase di integrazione finale.

---

## 📋 Il "Contratto" (Regole comuni obbligatorie)
Per far sì che i pezzi si incastrino, tutti devono rispettare queste specifiche:
* **Linguaggio:** C per i processi, Bash per lo script manuale.
* **Comunicazione:** Named Pipes (FIFO) in `/tmp/`.
* **Standard Naming FIFO:** * `/tmp/dev_<ID>_in`  (Il Controller/Hub scrive qui, il dispositivo legge)
    * `/tmp/dev_<ID>_out` (Il dispositivo scrive qui, il Controller/Hub legge)
* **Protocollo Messaggi (Stringhe):**
    * `SET|STATE|ON` o `SET|STATE|OFF` (Comandi di scrittura)
    * `GET|INFO` (Comando di lettura)
    * `OK` o `ERROR` (Risposte semplici)

---

## 👤 Persona 1: L'Architetto del Core (Controller & Shell)
*Gestisce la vita dei processi e l'interfaccia utente.*

### Task:
1.  **Shell Interattiva:** Creare il processo `controller` che parsa i comandi `list`, `add`, `del`, `link`, `switch`, `info`.
2.  **Gestione Processi:** Implementare `fork()` ed `exec()` per creare i processi figli. Nota: a livello OS sono tutti figli del controller (gerarchia piatta).
3.  **Registro (Tabella di Routing):** Gestire un file di testo o una struttura in memoria che associ `ID -> PID -> Tipo`.
4.  **Gestione FIFO:** Creare (`mkfifo`) e distruggere (`unlink`) le pipe all'aggiunta/rimozione di un dispositivo.
5.  **Script Manuale:** Scrivere `manual_interaction.sh` che legge l'ID, trova la FIFO corretta e invia il comando.

**Come testare da solo:** Crea una FIFO a mano (`mkfifo /tmp/dev_test_in`), lancia la tua shell e verifica se scrive correttamente il comando sulla FIFO quando digiti `switch`.

---

## 👤 Persona 2: Lo Sviluppatore della Logica (Dispositivi)
*Crea l'intelligenza dei singoli attuatori (Lampadine, Finestre, Frigo).*

### Task:
1.  **Interaction Devices:** Scrivere i file `bulb.c`, `window.c`, `fridge.c`.
2.  **Loop di Ascolto:** Ogni dispositivo deve:
    * Aprire la FIFO `_in` in lettura.
    * Leggere il comando.
    * Applicare un `sleep` casuale (1-3 sec).
    * Aggiornare i registri (es. temperatura, tempo accensione).
    * Rispondere sulla FIFO `_out`.
3.  **Persistenza:** Gestire i dati specifici (es. `fridge` ha temperatura e riempimento).

**Come testare da solo:** Lancia il tuo processo `./bulb 1`. In un altro terminale scrivi `echo "SET|STATE|ON" > /tmp/dev_1_in`. Verifica se il processo cambia stato.

---

## 👤 Persona 3: Il Master dei Control Devices (Hub & Timer)
*Gestisce la logica di gruppo e la propagazione dei messaggi.*

### Task:
1.  **Logica Hub:** Creare il processo `hub`. Deve mantenere un array/lista di ID dei figli logici.
2.  **Propagazione:**
    * **Write:** Invia il comando a tutte le FIFO `_in` dei suoi figli.
    * **Read:** Chiede lo stato a tutti, aggrega e gestisce il `"manual override"` se i figli sono discordanti.
3.  **Logica Timer:** Creare il processo `timer` che controlla un figlio in base agli orari `begin` ed `end`.
4.  **Makefile & Build:** Gestire il sistema di compilazione per tutto il gruppo.

**Come testare da solo:** Crea degli script bash "finti" che scrivono su una pipe e vedi se il tuo Hub riesce a coordinarli e rilevare l'override se uno risponde ON e l'altro OFF.

---

## 📅 Integrazione Finale
L'integrazione avverrà unendo i file in un'unica cartella:
1.  Il Controller (Persona 1) lancia i binari dei dispositivi (Persona 2).
2.  I comandi di `link` informano gli Hub (Persona 3) su quali pipe devono monitorare.
