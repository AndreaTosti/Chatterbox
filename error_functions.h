/**
 * @file error_functions.h
 * @author Andrea Tosti 518111
 * 
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore\n
 * NOTA: Il file e' stato scaricato dalla rete
 * @copyright Copyright (C) Michael Kerrisk, 2016.
 * @brief Header del file error_functions.c
 */

/*************************************************************************\
*                  Copyright (C) Michael Kerrisk, 2016.                   *
*                                                                         *
* This program is free software. You may use, modify, and redistribute it *
* under the terms of the GNU Lesser General Public License as published   *
* by the Free Software Foundation, either version 3 or (at your option)   *
* any later version. This program is distributed without any warranty.    *
* See the files COPYING.lgpl-v3 and COPYING.gpl-v3 for details.           *
\*************************************************************************/

#ifndef ERROR_FUNCTIONS_H
#define ERROR_FUNCTIONS_H

/** errMsg  stampa un messaggio sullo standard error.
			stampa l'errore testuale del valore corrente
			di errno. (nome errore, descrizione dell'errore
			ritornata da strerror() seguita dall'output
			formattato specificato nella lista argomenti) */
void errMsg(const char* format, ...);

#ifdef __GNUC__

/** Macro per evitare che gcc -Wall si lamenti ogni volta che
   usiamo le funzioni seguenti per terminare il main()
   o altre funzioni non-void stampando a video l'errore
   "control reaches end of non-void function" */
#define NORETURN __attribute__ ((__noreturn__))
#else
#define NORETURN
#endif

/** 0 - FALSE | 1 - TRUE */
typedef enum
{
	FALSE,
	TRUE
}Boolean;

/** errExit() funziona come errMsg(), in piu' termina il programma
			chiamando exit() o abort() per produrre un core_dump */
void errExit(const char* format, ...) NORETURN;

/** err_exit() e' simile a errExit() ma differisce in due aspetti:
			non fa il flush dello standard output prima di stampare
			il messaggio di errore;
			termina il processo chiamando _exit() invece che exit().
			Cio' fa si che il processo termini senza fare il flush
			dei buffer stdio o invocando gestori di exit.
			E' molto utile nel caso scriviamo una funzione di 
			libreria che crea un processo figlio che ha bisogno
			di terminare a causa di un errore. */
void err_exit(const char* format, ...) NORETURN;

/** errExitEN() e' come errExit() eccetto che invece di stampare
			il testo d'errore corrispondente al valore corrente
			di errno, stampa il testo corrispondente al numero errore
			dato nel parametro errnum */
void errExitEN(int errnum, const char* format, ...) NORETURN;

/** fatal() viene usata per diagnosticare errori generali, inclusi
			errori da funzioni di libreria che non settano errno.
			Stampa sullo standard error e termina il programma con una errExit() */
void fatal(const char* format, ...) NORETURN;

/** usageErr() viene usata per diagnosticare errori sull'uso
			di parametri nella command-line.
			Prende una lista di parametri e stampa la stringa:
			Usage: seguita dal testo formattato sullo standard error
			e poi termina il programma chiamando exit(). */
void usageErr(const char* format, ...) NORETURN;

/** cmdLineErr() simile a usageErr() ma e' intesa per uso
			diagnostica di errori sui parametri nella command-line
			specificati al programma */
void cmdLineErr(const char* format, ...) NORETURN;
#endif