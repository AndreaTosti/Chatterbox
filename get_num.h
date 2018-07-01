/**
 * @file get_num.h
 * @author Andrea Tosti 518111
 * 
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore\n
 * NOTA: Il file e' stato scaricato dalla rete
 * @copyright Copyright (C) Michael Kerrisk, 2016.
 * @brief Header del file get_num.c
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

#ifndef GET_NUM_H
#define GET_NUM_H

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#define GN_NONNEG	01	/* Valore deve essere >= 0 */
#define GN_GT_0		02  /* Valore deve essere > 0 */

/* Di default gli interi sono in decimale. */

#define GN_ANY_BASE 0100 /* Puo' usare qualsiasi base */
#define GN_BASE_8   0200 /* Valore espresso in ottale */
#define GN_BASE_16  0400 /* Valore espresso in esadecimale */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/** converte la stringa arg ad un long\n
    se arg non contiene una long valido nella stringa\n
    (ad esempio ci sono solo cifre e caratteri + e -)\n
   allora stampa un errore e termina il programma */
long getLong(const char* arg, int flags, const char* name);

/** converte la stringa arg ad un intero\n
    se arg non contiene una intero valido nella stringa\n
    (ad esempio ci sono solo cifre e caratteri + e -)\n
   allora stampa un errore e termina il programma */
int getInt(const char* arg, int flags, const char* name);
#endif