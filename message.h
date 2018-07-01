/*
 * chatterbox Progetto del corso di LSO 2017 
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */
#ifndef MESSAGE_H_
#define MESSAGE_H_

#include <assert.h>
#include <string.h>
#include <config.h>
#include <ops.h>

/**
 * @file  message.h
 * @author Andrea Tosti 518111
 *
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore\n
 * NOTA: file fornito in kit_chatty e modificato opportunamente
 * @brief Contiene il formato del messaggio
 */

/** struttura - header del messaggio */
typedef struct {
    op_t     op;                        /**< tipo di operazione richiesta al server */
    char sender[MAX_NAME_LENGTH+1];     /**< nickname del mittente */
} message_hdr_t;

/** struttura - header della parte dati */
typedef struct {
    char receiver[MAX_NAME_LENGTH+1];   /**< nickname del ricevente */
    unsigned int   len;                 /**< lunghezza del buffer dati */
} message_data_hdr_t;

/** struttura - body del messaggio */
typedef struct {
    message_data_hdr_t  hdr;            /**< header della parte dati */
    char               *buf;            /**< buffer dati             */
} message_data_t;

/** struttura - tipo del messaggio */
typedef struct {
    message_hdr_t  hdr;                 /**< header */
    message_data_t data;                /**< dati */
} message_t;

/* ------ funzioni di utilità ------- */

/**
 * @function setheader
 * @brief scrive l'header del messaggio
 * @param[in,out] hdr puntatore all'header
 * @param[in] op tipo di operazione da eseguire
 * @param[in,out] sender mittente del messaggio
 * @return nothing
 */
static inline void setHeader(message_hdr_t *hdr, op_t op, char *sender) {
#if defined(MAKE_VALGRIND_HAPPY)
    memset((char*)hdr, 0, sizeof(message_hdr_t));
#endif
    hdr->op  = op;
    strncpy(hdr->sender, sender, strlen(sender)+1);
}

/**
 * @function setData
 * @brief scrive la parte dati del messaggio
 * @param[in,out] data puntatore al body del messaggio
 * @param[in] rcv nickname o groupname del destinatario
 * @param[in,out] buf puntatore al buffer 
 * @param[in] len lunghezza del buffer
 */
static inline void setData(message_data_t *data, char *rcv, const char *buf, unsigned int len) {
#if defined(MAKE_VALGRIND_HAPPY)
    memset((char*)&(data->hdr), 0, sizeof(message_data_hdr_t));
#endif

    strncpy(data->hdr.receiver, rcv, strlen(rcv)+1);
    data->hdr.len  = len;
    data->buf      = (char *)buf;
}

#endif /* MESSAGE_H_ */
