#!/bin/bash

#-----------------------------------------------------------------------------------------------\
# file   script.sh------------------------------------------------------------------------------|
# author Andrea Tosti 518111 Corso B -----------------------------------------------------------|
# ----------------------------------------------------------------------------------------------|
# Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore -|
#-----------------------------------------------------------------------------------------------/

# NOTA: viene salvato nella variabile dirname l'ultimo valore di DirName disponibile nel file di configurazione

# funzione log che stampa a schermo tutti i DirName trovati nel file, usata quando -v e' presente nelle opzioni 
function log ()
{
  if [[ $_v -eq 1 ]]; then
    echo "$@"
  fi
}

# se il numero di argomenti e' esattamente 0 oppure se ho scritto --help, mostra l'help
if [[ $1 == "--help" || $# -eq 0 ]]; then
  echo "Usage: $0 configuration_file time [-v]"
  echo "-Se time = 0 stampa a schermo tutti i file contenuti in DirName"
  echo "-Se time > 0 rimuove tutti i file e cartelle contenuti in DirName"
  echo "che sono piu' vecchi di un numero di minuti pari a time"
  echo "-Se e' presente -v come terzo parametro, stampa a schermo tutti i"
  echo "DirName trovati nel file di configurazione"
  echo "* usa --help per visualizzare questo help"
  exit 1
fi

# se il numero di argomenti non e' esattamente 2 o 3, esci
if [[ $# -ne 2 && $# -ne 3 ]]; then
  echo "Usa : $0 fileconfig time"
  exit 1
fi

# se il numero di argomenti e' 3 e il terzo parametro e' -v, attiva il debugging
if [[ $# -eq 3 && "$3" == "-v" ]]; then
  _v=1
else
  _v=0
fi

# controllo che il file esista
if ! [ -e $1 ]; then
  echo "il file $(basename $1) non esiste"
  exit 1
elif ! [ -f $1 ]; then
  echo "il file $(basename $1) non e' un file regolare"
  exit 1
elif ! [ -r $1 ]; then
  echo "non hai i permessi di lettura sul file $(basename $1)"
  exit 1
fi

# controllo che time sia un intero maggiore di zero
if ! [ "$2" -eq "$2" ] 2> /dev/null ; then
  echo "time deve essere un intero valido"
  exit 1
elif [ $2 -lt 0 ]; then
  echo "time deve essere maggiore o uguale a 0"
  exit 1
fi

# se il file di configurazione non va mai accapo, read ha letto comunque la riga ritornando false
# quindi aggiungo -n STRING (-n <STRING> True se <STRING> non e' vuota)
while read -r line || [[ -n $line ]]; do
  if [[ $line = \#* ]]; then
    continue
  fi
  # DirName + 0 o piu' spazi/tab + = + 0 o piu' spazi/tab
  exp_regolare='DirName[[:blank:]]*=[[:blank:]]*'
  if [[ $line =~ $exp_regolare ]]; then
    string2=${line//*"DirName"*=/}
    solo_virgolette="${string2//[^\"]}" #togli tutto da string2 tranne le virgolette
    counter="${#solo_virgolette}"       #conta il numero di virgolette totali
    tmp_str=${string2%[[:blank]]*[[:graph:]]*}  #cerca di togliere dal fondo spazi + virgolette
    if [[ $counter -gt 2 ]]; then
      while [[ $counter -gt 2 ]]; do    #ciclo che toglie quante piu' coppie di virgolette possibili
        if [[ $tmp_str == \"*\"* ]]; then
          suffix="\"*"
          tmp_str="${tmp_str%$suffix}"
        fi
        tmp_str=${tmp_str%[[:blank:]]*[[:graph:]]*}
        counter=$((counter -2))
        solo_virgolette2="${tmp_str//[^\"]}" #togli tutto da tmp_str tranne le virgolette
        counter="${#solo_virgolette2}"       #conta il numero di virgolette totali
      done
      if [[ $tmp_str == "" ]]; then
        tmp_str=''
      elif [[ $tmp_str != \"*\"* ]]; then
        tmp_str="${tmp_str%\"*}\""
      fi
      log $tmp_str     #debugging
      dirname=$tmp_str #setto dirname
    else
      string3=${string2%[[:blank:]]*[[:graph:]]*}
      if [[ $string3 != *[!\ ]* ]]; then     #se ho solo spazi dopo la rimozione di spazi + virgolette
        if [[ $counter -eq 2 ]]; then
          if [[ $string2 == *\"*\" ]]; then
            suffix="\"*\""
            tmp_str="${string2%$suffix}"
          else
            suffix="\"*"
            tmp_str="${string2%$suffix}\""
          fi
          log $tmp_str
          dirname=$tmp_str
        else
          log $string2
          dirname=$string2
        fi
      elif [[ $counter -eq 2 ]]; then
        solo_virgolette="${string3//[^\"]}"
        counter="${#solo_virgolette}"
        if [[ $counter -ge 2 || $counter -eq 0 ]]; then
          log $string3
          dirname=$string3
        else
          log $string2
          dirname=$string2
        fi
      else
        log $string3
        dirname=$string3
      fi
    fi
  fi

  # togli tutte le virgolette
  if [[ $dirname =~ \" ]]; then
    dirname="${dirname//\"}"
  fi
  
  # tolgo tutti gli spazi che vengono prima
  while [[ $dirname == ' '* ]]; do
    dirname="${dirname## }"
  done

  # tolgo tutti gli spazi che vengono dopo
  while [[ $dirname == *' ' ]]; do
    dirname="${dirname%% }"
  done

done < "$1" # prendi come input il file di configurazione passato come parametro

if [ $2 -ne 0 ]; then
  domanda="Confermi di eliminare files/directory interni a $dirname piu' vecchi di $2 minuti? [Yy]"
  read -p "$domanda" -n 1 -r
  echo
  if [[ $REPLY =~ ^[Yy]$ ]]; then
  # rimuove tutti i file e cartelle contenuti in dirname
  # che sono piu' vecchi di un numero di minuti pari a time ($2)"
    find "$dirname" -maxdepth 1 -mindepth 1 -mmin +"$2" -print0 | xargs -0 /bin/rm -rf
  fi
else
  # stampa a schermo tutti i nomi dei file contenuti in dirname
  find "$dirname" -maxdepth 1 -type f -exec basename {} \;
fi