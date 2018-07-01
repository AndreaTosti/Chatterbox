/**
 * @file mkpath.c
 * @author Andrea Tosti 518111
 * 
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore\n
 * NOTA: Il file e' stato scaricato dalla rete https://stackoverflow.com/a/9210960
 * @brief Funzioni per creare tutte le sottocartelle necessarie a contenere il file
 */

#include <mkpath.h>

/** 
 * @function mkpath
 * @brief a partire da un path di un file, relativo o assoluto, crea tutte le sottocartelle
 *
 * fa la stessa cosa che farebbe mkdir, quindi crea tutte le sottocartelle necessarie\n
 * per contenere il file specificato nel path, o meglio, crea le sottocartelle che\n
 * vengono scorse per arrivare al file di destinatione
 * @param[in,out] file_path path relativo o assoluto che contiene anche il nome del file
 * @param[in] mode bitmask permessi sui file
 * @return 0 in caso di successo, -1 altrimenti
 */
int mkpath(char* file_path, mode_t mode) 
{
    assert(file_path && *file_path);
    char* p;
    for (p = strchr(file_path + 1, '/'); p; p = strchr(p + 1, '/')) 
    {
        *p='\0';
        if (mkdir(file_path, mode) == -1) 
        {
            if (errno != EEXIST && errno != EACCES) 
            {
                *p='/'; 
                return -1; 
            }
        }
        *p='/';
    }
    return 0;
}