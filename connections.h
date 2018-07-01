/*
 * chatterbox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */

 /**
 * @file connections.h
 * @author Andrea Tosti 518111
 *
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore\n
 * NOTA: file fornito in kit_chatty e modificato opportunamente
 * @brief Header del file connections.c
 */

#ifndef CONNECTIONS_H_
#define CONNECTIONS_H_

#define MAX_RETRIES     10   /**< numero massimo di tentativi di connessione */
#define MAX_SLEEPING     3   /**< intervallo di tempo tra un tentativo e l'altro */
#if !defined(UNIX_PATH_MAX)
#define UNIX_PATH_MAX  108   /**< sizeof(addr.sun_path) */
#endif

#include <message.h>
#include <connections.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <rdwrn.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <sys/stat.h>	//Bit modes for access permission
#include <fcntl.h>		//Bit modes for file access modes
#include <errno.h>
#include <mkpath.h>

extern int maxFileSize; /**< dichiarazione dimensione massima di un file */

/**
 * @function openConnection
 *
 * Lato client
 * @brief Apre una connessione AF_UNIX verso il server 
 * @param[in] path Path del socket 
 * @param[in] ntimes numero massimo di tentativi di retry
 * @param[in] secs tempo di attesa tra due retry consecutive
 * @return il descrittore associato alla connessione in caso di successo
 *         -1 in caso di errore
 */
int openConnection(char* path, unsigned int ntimes, unsigned int secs);

/**
 * @function readHeader
 * @brief Legge l'header del messaggio
 *
 * Lato client
 * @param[in] connfd descrittore della connessione
 * @param[in] hdr puntatore all'header del messaggio da leggere
 *
 * @return intero < 0 se c'e' stato un errore\n
 *         intero = 0 se connessione chiusa\n
 *         intero = 1 in caso di successo
 */
int readHeader(long connfd, message_hdr_t *hdr);

/**
 * @function readData
 * @brief Legge il body del messaggio
 *
 * Lato client
 * @param[in] fd descrittore della connessione
 * @param[in, out] data puntatore al body del messaggio
 *
 * @return intero < 0 se c'e' stato un errore\n
 *         intero = 0 se connessione chiusa\n
 *         intero = 1 in caso di successo
 */
int readData(long fd, message_data_t *data);


/**
 * @function readDataServerSide
 * @brief Legge il body del messaggio
 *
 * Lato server
 * @param[in] fd descrittore della connessione
 * @param[in] newFileName nome del file da creare
 *
 * @return intero < 0 se c'e' stato un errore\n
 *                = -1 errore generico\n
 *                = -2 se file eccede maxFileSize\n
 *         intero = 0 se connessione chiusa\n
 *         intero = 1 in caso di successo
 */
int readDataServerSide(long fd, char* newFileName);


/**
 * @function readMsg
 * @brief Legge l'intero messaggio
 *
 * Client side
 * @param[in] fd descrittore della connessione
 * @param[in,out] msg puntatore al messaggio
 *
 * @return intero < 0 se c'e' stato un errore\n
 *         intero = 0 se connessione chiusa\n
 *         intero = 1 in caso di successo
 */
int readMsg(long fd, message_t *msg);

/**
 * @function readMsgServerSide
 * @brief Legge l'intero messaggio
 *
 * Server side
 * @param[in] fd descrittore della connessione
 * @param[in,out] msg puntatore al messaggio
 *
 * @return intero < 0 se c'e' stato un errore\n
 *         intero = 0 se connessione chiusa\n
 *         intero = 1 in caso di successo
 */
int readMsgServerSide(long fd, message_t *msg);

/**
 * @function sendRequest
 * @brief Invia un messaggio di richiesta
 *
 * Server-Client sides
 * @param[in] fd descrittore della connessione
 * @param[in,out] msg puntatore al messaggio da inviare
 *
 * @return intero < 0 se c'e' stato un errore\n
 *         intero = 0 se connessione chiusa\n
 *         intero = 1 in caso di successo
 */ 
int sendRequest(long fd, message_t *msg);

/**
 * @function sendData
 * @brief Invia il body del messaggio
 *
 * Client side
 * @param[in] fd descrittore della connessione
 * @param[in,out] msg puntatore al messaggio da inviare
 *
 * @return intero < 0 se c'e' stato un errore\n
 *         intero = 0 se connessione chiusa\n
 *         intero = 1 in caso di successo
 */
int sendData(long fd, message_data_t *msg);

/**
 * @function sendDataServerSide
 * @brief Invia il body del messaggio
 *
 * Server side
 * @param[in] fd descrittore della connessione
 * @param[in,out] msg puntatore al messaggio da inviare
 *
 * @return intero < 0 se c'e' stato un errore\n
 *         intero = 0 se connessione chiusa\n
 *         intero = 1 in caso di successo
 */
int sendDataServerSide(long fd, message_data_t *msg);

/**
 * @function sendHdr
 * @brief Invia l'header del messaggio
 *
 * Server side
 * @param[in] fd descrittore della connessione
 * @param[in,out] msg puntatore al messaggio da inviare
 *
 * @return intero < 0 se c'e' stato un errore\n
 *         intero = 0 se connessione chiusa\n
 *         intero = 1 in caso di successo
 */
int sendHdr(long fd, message_hdr_t *msg);

#endif /* CONNECTIONS_H_ */
