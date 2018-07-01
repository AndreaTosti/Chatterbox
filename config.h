/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */

/**
 * @file config.h
 * @author Andrea Tosti 518111
 *
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore\n
 * NOTA: file fornito in kit_chatty e modificato opportunamente
 * @brief File contenente alcune define con valori massimi utilizzabili
 */

#if !defined(CONFIG_H_)
#define CONFIG_H_

/** lunghezza massima nome utente o gruppo */
#define MAX_NAME_LENGTH 32

/* aggiungere altre define qui */

/** dimensione massima in bytes del buffer */
#ifndef BUFSIZE
#define BUFSIZE 1024                     
#endif

/** Max richieste pendenti di connessione, se viene superato la connect potrebbe bloccarsi */
#ifndef MAXBACKLOG
#define MAXBACKLOG 50
#endif

/** numero di buckets totali della hash table */
#ifndef HASH_DIM
#define HASH_DIM 256
#endif

/** numero di buckets per ogni lock */
#ifndef HASH_BUCKETS_PER_LOCK
#define HASH_BUCKETS_PER_LOCK 16
#endif

/** to avoid warnings like "ISO C forbids an empty translation unit" */
typedef int make_iso_compilers_happy;

#endif /* CONFIG_H_ */
