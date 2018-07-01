#ifndef QUEUE_H_
#define QUEUE_H_

/**
 * @file queue.h
 * @author Andrea Tosti 518111
 * 
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore\n
 * NOTA: il file e' stato preso e modificato dalle soluzioni delle esercitazioni di laboratorio
 * @brief Header del file queue.c
 */

/** nodo - elemento di una coda */
typedef struct Node
{
	void* data;				/**< contenuto del nodo 	   */
	struct Node *next;		/**< puntatore nodo successivo */
}Node_t;

/** struttura dati coda */
typedef struct Queue
{
	Node_t *head;			/**< puntatore nodo testa */
	Node_t *tail;			/**< puntatore nodo coda */
	unsigned long qlen;		/**< lunghezza della coda */
}Queue_t;

/**
 * @function initQueue
 * @brief alloca e inizializza una coda
 * @return NULL se si son verificati problemi nell'allocazione\n
 * 		   con conseguente errno settato, altrimenti ritorna\n
 * 		   un puntatore alla coda allocata
 */
Queue_t *initQueue();

/**
 * @function deleteQueue
 * @brief cancella una coda precedentemente allocata con initQueue
 * @param[in] q puntatore alla coda da cancellare
 * @return nothing
 */
void deleteQueue(Queue_t *q);

/**
 * @function push
 * @brief inserisce un dato nella coda
 * @param[in] q puntatore alla coda in cui inserire il dato
 * @param[in] data dato da inserire nella coda
 * @return 0 in caso di successo, -1 altrimenti settando errno
 */
int push(Queue_t *q, void* data);

/**
 * @function pop
 * @brief preleva un dato dalla coda
 * @param[in] q puntatore alla coda da cui estrarre il dato
 * @return nothing
 */
void* pop(Queue_t *q);

/**
 * @function length
 * @brief restituisce la lunghezza della coda
 * @param[in] q puntatore alla coda
 * @return lunghezza della coda
 */
unsigned long length(Queue_t *q);

#endif /* QUEUE_H_ */