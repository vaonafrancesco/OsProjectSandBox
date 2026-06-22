#!/bin/bash

# Nome del file in cui salveremo i comandi
FILE="stress.txt"

echo "Preparazione nastro di proiettili in $FILE..."

# Svuota il file se esiste già
> $FILE

# 1. Creiamo 20 dispositivi alternati (10 bulb, 10 hub)
for i in {1..10}; do
    echo "add bulb" >> $FILE
    echo "add hub" >> $FILE
done

# 2. Li bombardiamo di letture di stato e comandi di switch
for i in {1..20}; do
    echo "info $i" >> $FILE
    echo "switch $i power on" >> $FILE
    echo "list" >> $FILE
done

# 3. Tentiamo link a raffica (cerchiamo di collegare le bulb agli hub)
for i in {1..10}; do
    let "padre = $i + 10"
    echo "link $i to $padre" >> $FILE
done

# 4. Fase di sterminio: diamo l'ordine di cancellare tutti gli ID
for i in {1..20}; do
    echo "del $i" >> $FILE
done

# 5. Ordine di chiusura gentile per vedere se ci sono memory leak
echo "exit" >> $FILE

echo "Finito! Il file $FILE è pronto e contiene $(wc -l < $FILE) comandi."
