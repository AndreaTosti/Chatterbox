/**
 * @file rdwrn.c
 * @author Andrea Tosti 518111
 * @brief Contiene le funzioni readn e writen
 *
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore\n
 * NOTA: Il file e' stato scaricato dalla rete\n
 * Le funzioni readn() e writen() prendono in ingresso gli stessi\n
 * parametri di read() e write(), pero' usano un loop per riavviare\n
 * tali chiamate di sistema, cosi' da assicurare che il numero richiesto di\n
 * bytes sia sempre trasferito (a meno di errori o EOF rilevati sulla read()
 * @copyright Copyright (C) Michael Kerrisk, 2016
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

/* Listing 61-1 */

#include <unistd.h>
#include <errno.h>
#include "rdwrn.h"                      /* Declares readn() and writen() */

/**
 * @function readn
 * @brief Si comporta come una read, ma riavvia la read in caso di interruzioni
 *
 * prova a leggere fino a n bytes dal file descriptor fd nel buffer buffer\n
 * se su un socket ci sono meno bytes di quelli richiesti, avviene una lettura parziale\n
 * @param[in] fd file descriptor da cui leggere
 * @param[in,out] buffer buffer in cui memorizzare il dato letto
 * @param[in] n numero di byte da leggere
 * @return ssize_t > 0 il numero di bytes letti\n
 *         ssize_t = 0 in caso di EOF\n
 *         ssize_t < 0 in caso di errore
 */
ssize_t
readn(int fd, void *buffer, size_t n)
{
    ssize_t numRead;                    /* # of bytes fetched by last read() */
    size_t totRead;                     /* Total # of bytes read so far */
    char *buf;

    buf = buffer;                       /* No pointer arithmetic on "void *" */
    for (totRead = 0; totRead < n; ) {
        numRead = read(fd, buf, n - totRead);

        if (numRead == 0)               /* EOF */
            return totRead;             /* May be 0 if this is first read() */
        if (numRead == -1) {
            if (errno == EINTR)
                continue;               /* Interrupted --> restart read() */
            else
                return -1;              /* Some other error */
        }
        totRead += numRead;
        buf += numRead;
    }
    return totRead;                     /* Must be 'n' bytes if we get here */
}

/**
 * @function writen
 * @brief Si comporta come una write, ma riavvia la write in caso di interruzioni
 *
 * prova a scrivere fino a n bytes nel buffer buffer dal file descriptor fd\n
 * puo' avvenire una scrittura parziale se non basta lo spazio nel buffer per trasferire\n
 * tutti i bytes richiesti oltre ad un'altra condizione tra le seguenti:\n
 * un signal handler interrompe la write()\n
 * socket non bloccante\n
 * avviene un errore asincronamente (ad es. crash di un peer)
 * @param[in] fd file descriptor in cui scrivere
 * @param[in,out] buffer buffer in uscita
 * @param[in] n numero di byte da scrivere
 * @return ssize_t > 0 il numero di bytes scritti\n
 *         ssize_t < 0 in caso di errore
 */
ssize_t
writen(int fd, const void *buffer, size_t n)
{
    ssize_t numWritten;                 /* # of bytes written by last write() */
    size_t totWritten;                  /* Total # of bytes written so far */
    const char *buf;

    buf = buffer;                       /* No pointer arithmetic on "void *" */
    for (totWritten = 0; totWritten < n; ) {
        numWritten = write(fd, buf, n - totWritten);

        if (numWritten <= 0) {
            if (numWritten == -1 && (errno == EINTR))
                continue;               /* Interrupted --> restart write() */
            else
                return -1;              /* Some other error */
        }
        totWritten += numWritten;
        buf += numWritten;
    }
    return totWritten;                  /* Must be 'n' bytes if we get here */
}
