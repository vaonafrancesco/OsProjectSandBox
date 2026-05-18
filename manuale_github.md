# Manuale base Git da terminale

Questo manuale riassume i comandi essenziali di Git da usare nel terminale e alcune regole di buon uso per lavorare in modo ordinato, leggibile e sicuro in un repository.[cite:6][cite:10][cite:7]

## Cos'è Git

Git è un sistema di controllo di versione distribuito usato per tracciare le modifiche ai file, collaborare su un progetto e mantenere una cronologia dei cambiamenti nel tempo.[cite:7][cite:10]

## Configurazione iniziale

Prima di iniziare conviene impostare nome ed email, perché questi dati vengono associati ai commit.[cite:6][cite:7]

```bash
git config --global user.name "Il Tuo Nome"
git config --global user.email "email@esempio.com"
git config --list
```

### Come usarli

- `git config --global user.name "Il Tuo Nome"`: imposta il nome autore dei commit per tutti i repository locali.[cite:6]
- `git config --global user.email "email@esempio.com"`: imposta l'email autore dei commit.[cite:6]
- `git config --list`: mostra la configurazione Git attiva.[cite:6]

## Creare o copiare un repository

Per iniziare un nuovo progetto si usa `git init`, mentre per lavorare su un progetto esistente si usa `git clone` con l'URL del repository remoto.[cite:6][cite:10]

```bash
git init
git clone https://github.com/utente/progetto.git
```

### Come usarli

- `git init`: inizializza Git nella cartella corrente e crea il repository locale.[cite:6][cite:10]
- `git clone URL`: scarica in locale un repository remoto completo di cronologia.[cite:6][cite:7]

## Controllare lo stato del progetto

Questi comandi servono per capire cosa è cambiato, cosa è pronto per il commit e quali modifiche sono già state salvate nella cronologia.[cite:10][cite:7]

```bash
git status
git log
git diff
```

### Come usarli

- `git status`: mostra file modificati, non tracciati e file già aggiunti allo staging.[cite:10][cite:7]
- `git log`: mostra la cronologia dei commit, in genere dal più recente al meno recente.[cite:10]
- `git diff`: mostra le differenze tra i file modificati e l'ultima versione salvata.[cite:10][cite:7]

## Preparare e salvare modifiche

Il flusso tipico di Git prevede modifica dei file, aggiunta allo staging con `git add` e salvataggio con `git commit`.[cite:6][cite:7][cite:10]

```bash
git add nomefile
git add .
git commit -m "Messaggio chiaro del commit"
```

### Come usarli

- `git add nomefile`: aggiunge un singolo file all'area di staging.[cite:10]
- `git add .`: aggiunge tutte le modifiche rilevate nella cartella corrente.[cite:7][cite:10]
- `git commit -m "Messaggio"`: salva nello storico le modifiche presenti nello staging con un messaggio descrittivo.[cite:6][cite:7]

## Lavorare con i branch

I branch permettono di sviluppare nuove funzionalità o correzioni senza toccare direttamente il ramo principale del progetto.[cite:10][cite:7]

```bash
git branch
git branch feature-login
git checkout feature-login
git checkout -b feature-api
```

### Come usarli

- `git branch`: elenca i branch locali.[cite:10]
- `git branch nome-branch`: crea un nuovo branch.[cite:10][cite:7]
- `git checkout nome-branch`: passa a un branch esistente.[cite:10]
- `git checkout -b nome-branch`: crea un branch e ci si sposta subito sopra.[cite:7][cite:10]

## Unire modifiche

Quando una funzionalità è pronta, il branch può essere integrato nel branch principale con `git merge`.[cite:10][cite:7]

```bash
git checkout main
git merge feature-login
```

### Come usarli

- `git checkout main`: si sposta sul branch principale prima dell'integrazione.[cite:10]
- `git merge feature-login`: unisce nel branch corrente la cronologia del branch indicato.[cite:7][cite:10]

## Collegamento con repository remoto

Per sincronizzare il repository locale con GitHub o altri server Git si usano i remoti, il pull e il push.[cite:6][cite:7]

```bash
git remote -v
git pull origin main
git push origin main
```

### Come usarli

- `git remote -v`: mostra gli URL dei repository remoti configurati.[cite:6]
- `git pull origin main`: scarica e integra nel branch locale gli aggiornamenti del branch remoto `main`.[cite:6][cite:7]
- `git push origin main`: invia i commit locali al branch remoto `main`.[cite:6][cite:7]

## Ripristino e annullamento

Git offre vari comandi per annullare modifiche locali o correggere errori senza perdere il controllo della cronologia.[cite:10][cite:7]

```bash
git restore app.py
git reset
git revert HASH
```

### Come usarli

- `git restore app.py`: ripristina il file alla versione dell'ultimo commit, annullando le modifiche locali non salvate.[cite:10]
- `git reset`: può rimuovere file dallo staging o spostare il puntatore della cronologia, a seconda delle opzioni usate.[cite:10]
- `git revert HASH`: crea un nuovo commit che annulla gli effetti del commit indicato, senza cancellare la cronologia.[cite:7][cite:10]

## Flusso di lavoro tipico

Un ciclo di lavoro base con Git può essere riassunto così.[cite:6][cite:7][cite:10]

1. Creare o clonare il repository con `git init` o `git clone`.[cite:6]
2. Modificare i file del progetto.[cite:7]
3. Controllare lo stato con `git status`.[cite:10]
4. Aggiungere le modifiche con `git add .` oppure `git add nomefile`.[cite:10]
5. Salvare con `git commit -m "messaggio"`.[cite:6]
6. Sincronizzare con il remoto tramite `git pull` e `git push`.[cite:6][cite:7]

## Regole di buon uso

L'uso corretto di Git non dipende solo dai comandi, ma anche dalle abitudini adottate durante il lavoro quotidiano.[cite:7][cite:10]

- Fare commit piccoli e frequenti, così è più semplice capire cosa è cambiato e tornare indietro in caso di errore.[cite:7]
- Scrivere messaggi di commit chiari e specifici, ad esempio `Aggiunge validazione login` invece di `fix`.[cite:7][cite:10]
- Controllare sempre `git status` prima di eseguire commit o push, per evitare di includere file sbagliati.[cite:10]
- Usare branch separati per nuove feature, bugfix o test, invece di lavorare sempre direttamente su `main`.[cite:7][cite:10]
- Eseguire `git pull` prima di `git push` quando si collabora con altre persone, così si riduce il rischio di conflitti.[cite:6][cite:7]
- Evitare di versionare file temporanei, credenziali, password o cartelle generate automaticamente; per questo si usa `.gitignore`.[cite:7]
- Non usare comandi distruttivi come `git reset --hard` senza sapere esattamente cosa fanno, perché possono eliminare modifiche locali.[cite:10]
- Scrivere una struttura di branch coerente, ad esempio un branch per ogni attività o ticket.[cite:7]

## Esempio pratico

```bash
git clone https://github.com/utente/progetto.git
cd progetto
git checkout -b feature-report
git status
git add .
git commit -m "Aggiunge report mensile"
git pull origin main
git push origin feature-report
```

In questo esempio viene clonato un repository, creato un branch di lavoro, salvate le modifiche e infine pubblicato il branch sul remoto.[cite:6][cite:7][cite:10]
