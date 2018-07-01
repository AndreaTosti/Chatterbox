#if !defined(MEMBOX_STATS_)
#define MEMBOX_STATS_

#include <stdio.h>
#include <time.h>

/**
 * @file stats.h
 * @author Andrea Tosti 518111
 * 
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore 
 * @brief Contiene le funzioni per gestire le statistiche
 */

/** struttura che contiene i valori delle statistiche */
struct statistics {
    unsigned long nusers;                       /**< n. di utenti registrati */
    unsigned long nonline;                      /**< n. di utenti connessi */
    unsigned long ndelivered;                   /**< n. di messaggi testuali consegnati */
    unsigned long nnotdelivered;                /**< n. di messaggi testuali non ancora consegnati */
    unsigned long nfiledelivered;               /**< n. di file consegnati */
    unsigned long nfilenotdelivered;            /**< n. di file non ancora consegnati */
    unsigned long nerrors;                      /**< n. di messaggi di errore */
};

/* aggiungere qui altre funzioni di utilita' per le statistiche */

/**
 * @function updateStats
 * @brief Aggiorna le statistiche sommando i parametri in ingresso con quelli presenti in chattyStats
 *
 * @param[in] nusers numero di utenti registrati
 * @param[in] nonline numeri di utenti connessi
 * @param[in] ndelivered numero di messaggi testuali consegnati
 * @param[in] nnotdelivered numero di messaggi testuali non ancora consegnati
 * @param[in] nfiledelivered numero di file consegnati
 * @param[in] nfilenotdelivered numero di file non ancora consegnati
 * @param[in] nerrors numero di messaggi di errore
 * @return 0 in caso di successo
 */
static inline int updateStats(int nusers, int nonline, int ndelivered, 
                                int nnotdelivered, int nfiledelivered, 
                                    int nfilenotdelivered, int nerrors) 
{
    extern struct statistics chattyStats;
    chattyStats.nusers += nusers; 
    chattyStats.nonline += nonline;
    chattyStats.ndelivered += ndelivered;
    chattyStats.nnotdelivered += nnotdelivered;
    chattyStats.nfiledelivered += nfiledelivered;
    chattyStats.nfilenotdelivered += nfilenotdelivered;
    chattyStats.nerrors += nerrors;
    return 0;
}

/**
 * @function printStats
 * @brief Stampa le statistiche nel file passato come argomento
 *
 * @param fout descrittore (gia' aperto) del file delle statistiche
 *
 * @return 0 in caso di successo, -1 in caso di fallimento 
 */
static inline int printStats(FILE *fout) {
    extern struct statistics chattyStats;

    if (fprintf(fout, "%ld - %ld %ld %ld %ld %ld %ld %ld\n",
		(unsigned long)time(NULL),
		chattyStats.nusers, 
		chattyStats.nonline,
		chattyStats.ndelivered,
		chattyStats.nnotdelivered,
		chattyStats.nfiledelivered,
		chattyStats.nfilenotdelivered,
		chattyStats.nerrors
		) < 0) return -1;
    fflush(fout);
    return 0;
}

#endif /* MEMBOX_STATS_ */
