/**
 * @file parser.h
 * @author Andrea Tosti 518111
 * 
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore
 * @brief Header del file parser.c
 */

#ifndef PARSER_H_
#define PARSER_H_

#include <stdio.h>
#include <stdlib.h>
#include <error_functions.h>
#include <config.h>
#include <string.h>
#include <ctype.h>

/** struttura contenente campi del file di configurazione */
typedef struct _config_t
{
    char* unixPath;     /**< path utilizzato per la creazione del socket AF_UNIX */
    int maxConnections; /**< numero massimo di connessioni pendenti */
    int threadsInPool;  /**< numero di thread nel pool */
    int maxMsgSize;     /**< dimensione massima di un messaggio testuale (num. di caratteri) */
    int maxFileSize;    /**< dimensione massima di un file accettato dal server (kilobytes) */
    int maxHistMsgs;    /**< numero massimo di messaggi che il server ricorda per ogni client */
    char* dirName;      /**< directory dove memorizzare i files da inviare agli utenti */
    char* statFileName; /**< file nel quale verranno scritte le statistiche del server */
}config_t;

config_t* parseConfigurationFile(FILE* chattyConfigFd);

#endif /* PARSER_H_ */