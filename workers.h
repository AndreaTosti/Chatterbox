#ifndef WORKER_H_
#define WORKER_H_

#define _POSIX_C_SOURCE 200809L /**<feature test macro */
#include <pthread.h>
#include <time.h>
#include <sys/select.h>
#include <error_functions.h>
#include <signal.h>
#include <errno.h>
#include <inttypes.h>

/**
 * @file workers.h
 * @author Andrea Tosti 518111
 * 
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore 
 * @brief Contiene la funzione svolta dai thread
 */

/** definizione di tipo puntatore a funzione */
typedef void* (*Fun_t)(void*);

/** struttura che contiene gli argomenti per il thread */
typedef struct threadArgs
{
	uint32_t thid; 		 /**<Thread id */
	Queue_t *descrQueue; /**<Coda descrittori */
	Fun_t fun;		     /**<Funzione eseguita dal worker */
	void *EOS;		     /**<End of Service - per far terminare il thread */
}threadArgs_t;

/**
 * @function worker
 * @brief funzione eseguita dal thread

 * estrae dalla coda dei descrittori un file descriptor\n
 * ed esegue una funzione su di esso
 * @param[in] arg struttura dati contenente parametri utili ad eseguire la funzione
 * @return nothing
 */
static void* worker(void *arg)
{
	Queue_t *descrQueue = ((threadArgs_t*)arg) -> descrQueue;
	Fun_t F = ((threadArgs_t*)arg) -> fun;
	void *EOS = ((threadArgs_t*)arg) -> EOS;
	//int thid = ((threadArgs_t*)arg) -> thid;

	for(;;)
	{
		int* cfd = pop(descrQueue);
		if(*cfd == *((int*)EOS)) 
		{
			free(cfd);
			break;
		}
		//printf("{=== Thread ID %" PRIu32 "===} prende in carico cfd %d\n", thid, *cfd);
		F(cfd);
		//printf("{=== Thread ID %" PRIu32 "===} ha finito\n", thid);
		//printf("\n");
		free(cfd);
	}
	return NULL;
}

#endif /* WORKER_H_ */