# Script Bash
Deve essere realizzato uno script bash che prende in input come argomenti il file di  
configurazione del server chatterbox ed un numero intero positivo (t).   
Lo script estrae il nome della directory associato all’opzione DirName dal file passato come  
primo argomento e rimuove tutti i files (e directories) contenuti in tale directory che  
sono piú vecchi di t minuti. Se t vale 0 (t==0) allora dovranno essere stampati sullo  
standard output tutti i file contenuti nella directory DirName.  
Lo script deve stampare il messaggio di uso dello script se lanciato senza opzioni   
o se l’opzione lunga “–help” è presente tra gli argomenti del programma.  
È fatto esplicito divieto di usare sed o awk nella realizzazione dello script.  
