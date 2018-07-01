#!/bin/bash

# registro un po' di nickname
./client -l $1 -c founderX &
./client -l $1 -c founderY &
./client -l $1 -c membroA &
./client -l $1 -c membroB &
./client -l $1 -c membroC &
wait

# creo il gruppo1
./client -l $1 -k founderX -g gruppo1
if [[ $? != 0 ]]; then
    exit 1
fi

# creo il gruppo2
./client -l $1 -k founderY -g gruppo2
if [[ $? != 0 ]]; then
    exit 1
fi

# creo il gruppo3
./client -l $1 -k founderX -g gruppo3
if [[ $? != 0 ]]; then
    exit 1
fi

# aggiungo founderX al gruppo2
./client -l $1 -k founderX -a gruppo2
if [[ $? != 0 ]]; then
    exit 1
fi

# aggiungo membroA ai gruppi gruppo1 gruppo2 gruppo3
./client -l $1 -k membroA -a gruppo1 -a gruppo2 -a gruppo3
if [[ $? != 0 ]]; then
    exit 1
fi

# tolgo membroA dal gruppo2
./client -l $1 -k membroA -d gruppo2
if [[ $? != 0 ]]; then
    exit 1
fi

# aggiungo membroB ai gruppi gruppo1 e gruppo2
./client -l $1 -k membroB -a gruppo1 -a gruppo2
if [[ $? != 0 ]]; then
    exit 1
fi

# deregistro membroC e subito dopo provo a fare un'operazione
# messaggio di errore che mi aspetto dal prossimo comando
# l'utente membroC non puo' ricevere i messaggi precedenti perche' non e' piu' registrato
# (serve a mostrare che venga fatto il controllo anche subito dopo la deregistrazione, da parte del server)
OP_FAIL=25
./client -l $1 -k membroC -C membroC -p
e=$?
if [[ $((256-e)) != $OP_FAIL ]]; then
    echo "Errore non corrispondente $e" 
    exit 1
fi

# membroB si deregistra
# messaggio di errore che mi aspetto dal prossimo comando
# l'utente membroB non puo' mandare il messaggio perche' non e' iscritto al gruppo2
# (serve a mostrare che membroB si e' automaticamente tolto dal gruppo2 dopo essersi deregistrato)
OP_NICK_UNKNOWN=27
./client -l $1 -k membroB -C membroB -c membroB -k membroB -S "Ciao sono membroB":gruppo2
e=$?
if [[ $((256-e)) != $OP_NICK_UNKNOWN ]]; then
    echo "Errore non corrispondente $e" 
    exit 1
fi

# founderX si deregistra
# (di conseguenza i gruppi gruppo1 e gruppo3 non esistono piu')
SUCCESS=0
./client -l $1 -k founderX -C founderX
e=$?
if [[ $e != $SUCCESS ]]; then
    echo "Errore: $((256-e))" 
    exit 1
fi

# membroA vuole togliersi dal gruppo1
# messaggio di errore che mi aspetto dal prossimo comando
# il gruppo1 non esiste piu' quindi membroA non ne fa piu' parte
# (serve a mostrare che tutti gli utenti appartenenti al gruppo1 non vi appartengono
#  piu' perche' il fondatore lo ha eliminato in fase di deregistrazione)
OP_FAIL=25
./client -l $1 -k membroA -d gruppo1
e=$?
if [[ $((256-e)) != $OP_FAIL ]]; then
    echo "Errore non corrispondente $e" 
    exit 1
fi

# a questo punto solo founderY appartiene al gruppo2
# membroB manda un messaggio al gruppo2
SUCCESS=0
./client -l $1 -k founderY -S "Ciao":gruppo2 -R 1
e=$?
if [[ $e != $SUCCESS ]]; then
    echo "Errore: $((256-e))" 
    exit 1
fi

# founderY si cancella dal gruppo2 (di cui e' fondatore)
# (serve a mostrare che quando un utente si toglie dal gruppo di cui
#  e' fondatore, allora il gruppo venga eliminato)
SUCCESS=0
./client -l $1 -k founderY -d gruppo2
e=$?
if [[ $e != $SUCCESS ]]; then
    echo "Errore: $((256-e))" 
    exit 1
fi

# membroA vuole aggiungersi al gruppo2
# messaggio di errore che mi aspetto dal prossimo comando
# il gruppo2 non esiste quindi membroA non puo' aggiungersi
OP_NICK_UNKNOWN=27
./client -l $1 -k membroA -a gruppo2
e=$?
if [[ $((256-e)) != $OP_NICK_UNKNOWN ]]; then
    echo "Errore non corrispondente $e" 
    exit 1
fi

echo "Test OK!"
exit 0
