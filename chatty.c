/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */

/**
 * @file chatty.c
 * @author Andrea Tosti 518111
 * 
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore 
 * @brief File principale del server chatterbox
 */

#define _POSIX_C_SOURCE 200809L /**<feature test macro */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
/* inserire gli altri include che servono */
#include <config.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <message.h>
#include <connections.h>
#include <queue.h>
#include <workers.h>
#include <sys/mman.h>
#include <parser.h>
#include <icl_hash.h>
#include <utlist.h>
#include <stats.h>
#include <rdwrn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <get_num.h>
#include <error_functions.h>

/** restituisce il min tra m ed n */
#define min(m,n) ((m) < (n) ? (m) : (n))
/** restituisce il max tra m ed n */
#define max(m,n) ((m) > (n) ? (m) : (n))

/** struttura che memorizza le statistiche del server */
struct statistics chattyStats = { 0, 0, 0, 0, 0, 0, 0 };

/** struttura che memorizza un utente o un gruppo */
typedef struct _user
{
  message_t *previousMessages;        /**< array messaggi pendenti */
  int num_pending_messages;           /**< numero di messaggi pendenti */
  int index_pending_messages;         /**< indicizza l'array di messaggi pendenti */
  Boolean isAGroup;                   /**< TRUE se e' un gruppo, FALSE se e' un utente */
}User;

/** elemento di una lista */
typedef struct list_nicks_
{
  char nickname[MAX_NAME_LENGTH + 1]; /**< nickname */
  struct list_nicks_ *prev;           /**< puntatore elemento precedente */
  struct list_nicks_ *next;           /**< puntatore elemento successivo */
}list_nick_el;

/** struttura che memorizza un utente o un gruppo */
typedef struct list_users_
{
  char nickname[MAX_NAME_LENGTH + 1]; /**< nome dell'utente o del gruppo */
  int fd;                             /**< file descriptor di un utente se connesso\n
                                       *   -1 se e' un utente offline oppure se e' un gruppo
                                       */
  Boolean online;                     /**< TRUE se l'utente e' online\n
                                       *   FALSE se l'utente e' offline oppure se e' un gruppo
                                       */
  struct list_nicks_* groupMembers;   /**< lista di utenti appartenenti al gruppo se e' un gruppo\n
                                       *   se diverso da NULL, il primo elemento della 
                                       *   lista e' il founder del gruppo\n
                                       *   se uguale a NULL, allora e' un utente
                                       */
  struct list_users_ *prev;           /**< puntatore elemento precedente */
  struct list_users_ *next;           /**< puntatore elemento successivo */
}list_string_el;

/** struttura che memorizza gli argomenti da passare alle funzioni_Op */
typedef struct funOpArgs_
{
  message_t* message_read;       /**<messaggio ricevuto dal client mittente */
  const int* cfd;                /**<file descriptor del client mittente */
  const int* userExists;         /**<uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet; /**<insieme di bucket gia' lockato se l'utente esiste */
  message_hdr_t* risposta_hdr;   /**<messaggio di risposta verso il destinatario */
}funOpArgs_t;

/** pipe per gestire le interruzioni */
static int pipe_trick_fd[2];

/** path socket */
static char* socket_path = NULL;

/** numero massimo di messaggi che puo' contenere la history */
static int maxHistMsgs = -1;

/** dimensione massima di un messaggio */
static int maxMsgSize = -1;

/** numero client connessi e gestiti concorrentemente in un istante di tempo */
static int numClientConnessi = 0;

/** path del file di configurazione del server */
static char* dirName = NULL;

/** dimensione massima di un file */
int maxFileSize = -1;   //definizione

/** coda dei descrittori prelevati dai worker */
Queue_t* fdQueue = NULL;

/** lista degli utenti e dei gruppi registrati */
list_string_el *listUsers  = NULL;

/** insieme dei file descriptor condiviso fra tutti i thread */
static fd_set master_read_fd;

/** mutex per il set dei file descriptor */
pthread_mutex_t fdsetlock =       PTHREAD_MUTEX_INITIALIZER; 

/** mutex per la lista degli utenti/gruppi */
pthread_rwlock_t listuserslock =  PTHREAD_RWLOCK_INITIALIZER;

/** mutex per le statistiche */
pthread_mutex_t statslock =       PTHREAD_MUTEX_INITIALIZER;

/** mutex per il contatore di utenti online */
pthread_mutex_t numonlinelock =   PTHREAD_MUTEX_INITIALIZER;

/**
 * array di mutex per lockare gruppi di bucket della hashtable\n
 * l'array conterra' #HASH_DIM / #HASH_BUCKETS_PER_LOCK che corrisponde
 * al numero di lock per coprire l'intera hash table
 */
pthread_mutex_t hashlock[HASH_DIM / HASH_BUCKETS_PER_LOCK] = PTHREAD_MUTEX_INITIALIZER;

/** hashtable che contiene utenti e gruppi */
icl_hash_t *usersTable;

/**
 * @function free_hashtable_data
 * @brief libera memoria allocata per il valore value
 * @param[in,out] value valore, in questo caso di tipo User
 * @return nothing
 */
static void free_hashtable_data(void* value)
{
  User* utente = (User*)value;
  int indice = utente -> index_pending_messages;
  for(int i = 0; i < utente -> num_pending_messages; i++)
  {
    message_t *item_destinatario = utente -> previousMessages;
    indice--;
    if(indice < 0)
      indice += maxHistMsgs;
    free(item_destinatario[indice].data.buf);
  }
  free(utente -> previousMessages);
  free(utente);
}

/**
 * @function free_hashtable_key
 * @brief libera memoria allocata per la chiave key
 * @param[in,out] key chiave, in questo caso di tipo char*
 * @return nothing
 */
static void free_hashtable_key(void* key)
{
   free(key);
}

/**
 * @function LockHashTable
 * @brief fa il lock di una mutex appartenente all'array hashlock
 * @param[in] bucket_set insieme di bucket di una hashtable da lockare 
 *
 * l'array hashlock viene indicizzato con bucket_set dato in ingresso
 * @return nothing
 */
static inline void LockHashTable(unsigned int bucket_set)
{
  int s;
  s = pthread_mutex_lock(&hashlock[bucket_set]);
  if(s != 0)
    errExitEN(s, "pthread_mutex_lock");
  //printf("[        ] BucketSet lock = %u\n", bucket_set);
}

/**
 * @function UnlockHashTable
 * @brief fa l'unlock di una mutex appartenente all'array hashlock
 * @param[in] bucket_set insieme di bucket di una hashtable da unlockare
 * 
 * l'array hashlock viene indicizzato con bucket_set dato in ingresso
 * @return nothing
 */
static inline void UnlockHashTable(unsigned int bucket_set)
{
  int s;
  s = pthread_mutex_unlock(&hashlock[bucket_set]);
  if(s != 0)
    errExitEN(s, "pthread_mutex_unlock");
  //printf("[        ] BucketSet unlock = %u\n", bucket_set);
}

/**
 * @function LockNumOnline
 * @brief fa il lock della mutex numonlinelock
 * @return nothing
 */
static inline void LockNumOnline()
{
  int s;
  s = pthread_mutex_lock(&numonlinelock);
  if(s != 0)
    errExitEN(s, "pthread_mutex_lock");
}

/**
 * @function UnlockNumOnline
 * @brief fa l'unlock della mutex numonlinelock
 * @return nothing
 */
static inline void UnlockNumOnline()
{
  int s;
  s = pthread_mutex_unlock(&numonlinelock);
  if(s != 0)
    errExitEN(s, "pthread_mutex_unlock");
}

/**
 * @function LockStats
 * @brief fa il lock della mutex statslock
 * @return nothing
 */
static inline void LockStats()
{
  int s;
  s = pthread_mutex_lock(&statslock);
  if(s != 0)
    errExitEN(s, "pthread_mutex_lock");
}

/**
 * @function UnlockStats
 * @brief fa l'unlock della mutex statslock
 * @return nothing
 */
static inline void UnlockStats()
{
  int s;
  s = pthread_mutex_unlock(&statslock);
  if(s != 0)
    errExitEN(s, "pthread_mutex_unlock");
}

/**
 * @function LockFdSet
 * @brief fa il lock della mutex fdsetlock
 * @return nothing
 */
static inline void LockFdSet()
{
  int s;
  s = pthread_mutex_lock(&fdsetlock);
  if(s != 0)
    errExitEN(s, "pthread_mutex_lock");
}

/**
 * @function UnlockFdSet
 * @brief fa l'unlock della mutex fdsetlock
 * @return nothing
 */
static inline void UnlockFdSet()
{
  int s;
  s = pthread_mutex_unlock(&fdsetlock);
  if(s != 0)
    errExitEN(s, "pthread_mutex_unlock");
}

/**
 * @function LockAndReadListUsers
 * @brief fa il lock in lettura della mutex listuserslock
 * @return nothing
 */
static inline void LockAndReadListUsers()
{
  int s;
  s = pthread_rwlock_rdlock(&listuserslock);
  if(s != 0)
    errExitEN(s, "pthread_rwlock_rdlock");
}

/**
 * @function LockAndWriteListUsers
 * @brief fa il lock in scrittura della mutex listuserslock
 * @return nothing
 */
static inline void LockAndWriteListUsers()
{
  int s;
  s = pthread_rwlock_wrlock(&listuserslock);
  if(s != 0)
    errExitEN(s, "pthread_rwlock_wrlock");
}

/**
 * @function UnlockListUsers
 * @brief fa l'unlock della mutex listuserslock
 * @return nothing
 */
static inline void UnlockListUsers()
{
  int s;
  s = pthread_rwlock_unlock(&listuserslock);
  if(s != 0)
    errExitEN(s, "pthread_rwlock_unlock");
}

/**
 * @function nick_el_compare_nick
 * @brief fa un confronto lessicografico dei parametri in ingresso
 * @param[in] a elemento di lista il quale contiene un nickname
 * @param[in] b elemento di lista il quale contiene un nickname
 * @return un intero < 0 se a viene prima di b lessicograficamente\n
 *         un intero = 0 se a e' uguale a b lessicograficamente\n
 *         un intero > 0 se a viene dopo b lessicograficamente
 */
static int nick_el_compare_nick(list_nick_el* a, list_nick_el* b)
{
  return strcmp(a -> nickname, b -> nickname);
}

/**
 * @function string_el_compare_nick
 * @brief fa un confronto lessicografico dei parametri in ingresso
 * @param[in] a elemento di lista il quale contiene un nickname
 * @param[in] b elemento di lista il quale contiene un nickname
 * @ return un intero < 0 se a viene prima di b lessicograficamente\n
 *          un intero = 0 se a e' uguale a b lessicograficamente\n
 *          un intero > 0 se a viene dopo b lessicograficamente
 */
static int string_el_compare_nick(list_string_el* a, list_string_el* b) 
{
  return strcmp(a -> nickname, b -> nickname);
}

/**
 * @function string_el_compare_fd
 * @brief fa un confronto numerico dei parametri in ingresso
 * @param[in] a elemento di lista il quale contiene un file descriptor fd
 * @param[in] b elemento di lista il quale contiene un file descriptor fd
 * @return un intero < 0 se a<b\n
 *         un intero = 0 se a=b\n
 *         un intero > 0 se a>b
 */
static int string_el_compare_fd(list_string_el* a, list_string_el* b)
{
  return (a -> fd > b -> fd) - (a -> fd < b -> fd);
}

/**
 * @function usage
 * @brief stampa sullo standard error il modo di avviare correttamente il server
 * @param[in] progname nome dell'eseguibile del server
 * @return nothing
 */
static void usage(const char *progname) 
{
  fprintf(stderr, "[--MAIN--]Il server va lanciato con il seguente comando:\n");
  fprintf(stderr, "  %s -f conffile\n", progname);
}

/**
 * @function handler
 * @brief all'arrivo di un certo segnale scrivo su una pipe un valore
 * 
 * \n sulla pipe "pipe_trick_fd" verra' scritto: \n
 * 0 se si tratta di un segnale SIGINT, SIGQUIT oppure SIGTERM \n
 * 1 se si tratta di un segnale SIGUSR1 \n
 * sara' poi compito della select prelevare il dato dalla pipe
 * @param[in] sig numero del segnale catturato
 * @return nothing
 */
static void handler(int sig)
{
  int savedErrno;     /* nel caso dovesse cambiare errno */
  savedErrno = errno;
  switch(sig)
  {
    case SIGINT :
    {
      write(STDOUT_FILENO, "\nSegnale di SIGINT ricevuto\n", 29);
      if(write(pipe_trick_fd[1], "0", 1) == -1 && errno != EAGAIN)
        errExit("write");
    }break;

    case SIGQUIT :
    {
      write(STDOUT_FILENO, "\nSegnale di SIGQUIT ricevuto\n", 30);
      if(write(pipe_trick_fd[1], "0", 1) == -1 && errno != EAGAIN)
        errExit("write");
    }break;

    case SIGTERM :
    {
      write(STDOUT_FILENO, "\nSegnale di SIGTERM ricevuto\n", 30);
      if(write(pipe_trick_fd[1], "0", 1) == -1 && errno != EAGAIN)
        errExit("write");
    }break;

    case SIGUSR1 :
    {
      write(STDOUT_FILENO, "\nSegnale di SIGUSR1 ricevuto\n", 30);
      if(write(pipe_trick_fd[1], "1", 1) == -1 && errno != EAGAIN)
        errExit("write");
    }break;
  }
  errno = savedErrno;
}

/**
 * @function sendConnectedUsersList
 * @brief invia al file descriptor fd la lista di tutti gli utenti online
 * @param[in] fd file descriptor del client destinatario
 * @return 1 in caso di successo
 */
static int sendConnectedUsersList(int fd)
{
  printf("[FD: %4d]Success, invio lista di tutti gli utenti online\n", fd);
  /* Possiedo gia' il lock sul bucket del client destinatario */ 
  message_data_t* risposta_data = NULL;
  risposta_data = malloc(sizeof(message_data_t));
  if(!risposta_data)
    errExit("malloc");
  memset(risposta_data, 0, sizeof(message_data_t));

  list_string_el* str_curr_el = NULL;   /* elemento della lista corrente */
  list_string_el* str_dummy_el = NULL;  /* elemento aggiuntivo che serve a scorrere la lista */
  int num_utenti_online = 0;

  LockAndReadListUsers();
  /* Scorro la lista degli utenti/gruppi */
  DL_FOREACH_SAFE(listUsers, str_curr_el , str_dummy_el)
  {
    /* Un gruppo e' sempre offline, non faccio controlli su di esso */
    if(str_curr_el -> online == TRUE)
    {
      num_utenti_online++;        
    }
  }

  assert(num_utenti_online > 0);

  int dim_data_buf = num_utenti_online * (MAX_NAME_LENGTH + 1);
  risposta_data -> buf = (char*) malloc(dim_data_buf * sizeof(char));
  if(!risposta_data -> buf)
    errExit("malloc");
  memset(risposta_data -> buf, 0, dim_data_buf);
  risposta_data -> hdr.len = dim_data_buf;

  int i_buf = 0;

  str_curr_el = NULL;
  str_dummy_el = NULL;

  /* Scorro di nuovo la lista degli utenti/gruppi */
  DL_FOREACH_SAFE(listUsers, str_curr_el , str_dummy_el)
  {
    /* se l'utente e' online, concateno il suo nickname al buffer da inviare */
    if(str_curr_el -> online == TRUE)
    {
      memcpy(&(risposta_data -> buf)[i_buf], str_curr_el -> nickname, strlen(str_curr_el -> nickname) + 1);
      i_buf = i_buf + MAX_NAME_LENGTH + 1;              
    }
  }
  UnlockListUsers();
  if(sendDataServerSide(fd, risposta_data) == -1)
    errExit("sendDataServerSide");

  free(risposta_data -> buf);
  free(risposta_data); 
  
  return 1;
}

/** 
 * @function saveInPreviousMessages
 * @brief salva il messaggio nell'array dei messaggi precedenti dell'utente destinatario
 *
 * \n l'array dei messaggi precedenti e' circolare, quindi anche i messaggi che non sono mai stati\n 
 * letti possono essere sovrascritti per fare spazio a messaggi piu' recenti
 * @param[in,out] utente_destinatario puntatore alla struttura utente dell'utente destinatario
 * @param[in] message_read messaggio ricevuto dal client mittente
 * @param[in] op_type tipo di messaggio che puo' essere TXT_MESSAGE oppure FILE_MESSAGE
 * @return nothing 
 */
static void saveInPreviousMessages(User* utente_destinatario, message_t* message_read, const op_t op_type)
{
  /* Possiedo gia' il lock sul bucket del client destinatario */ 
  message_t *item_destinatario = utente_destinatario -> previousMessages;  /* aliasing */
  /* Nel caso in cui debba rimpiazzare dei messaggi vecchi che non sono stati ancora letti */
  if(utente_destinatario -> num_pending_messages == maxHistMsgs)
  {
    /* libero il vecchio messaggio che era stato allocato, ma mai letto (e quindi mai deallocato) */
    free(item_destinatario[utente_destinatario -> index_pending_messages].data.buf);                     
  }
  /* copio tutte le parti del messaggio nell'array dei messaggi precedenti dell'utente destinatario */
  item_destinatario[utente_destinatario -> index_pending_messages].data.buf = malloc((strlen(message_read -> data.buf) + 1) * sizeof(char));
  strcpy(item_destinatario[utente_destinatario -> index_pending_messages].data.buf, message_read -> data.buf);
  strcpy(item_destinatario[utente_destinatario -> index_pending_messages].hdr.sender, message_read -> hdr.sender);
  item_destinatario[utente_destinatario -> index_pending_messages].data.hdr.len = message_read -> data.hdr.len;
  item_destinatario[utente_destinatario -> index_pending_messages].hdr.op = op_type;
  utente_destinatario -> index_pending_messages = ((utente_destinatario -> index_pending_messages) + 1) % maxHistMsgs;
  if(utente_destinatario -> num_pending_messages < maxHistMsgs)
  {
    utente_destinatario -> num_pending_messages = utente_destinatario -> num_pending_messages + 1;
  }
}

/** 
 * @function sendMessageToListOfUsers
 * @brief invia un messaggio testuale/file ad una lista di utenti
 *
 * \n se non si riesce ad inviare il messaggio direttamente, salvo\n
 * il messaggio nei messaggi precedenti dell'utente destinatario
 *
 * @param[in,out] message_read messaggio ricevuto dal client mittente
 * @param[in,out] snapListUsers lista di utenti
 * @param[in,out] last_locked_bucket_set ultimo set di bucket lockato
 * @param[in] op_type tipo di messaggio che puo' essere TXT_MESSAGE oppure FILE_MESSAGE
 * @param[in,out] risposta_hdr messaggio di risposta verso il destinatario 
 * @return nothing
 */
static void sendMessageToListOfUsers(message_t* message_read, list_string_el* snapListUsers, 
            unsigned int* last_locked_bucket_set, const op_t op_type, message_hdr_t* risposta_hdr)
{
  /* Ordino la lista in ordine di file descriptor => bucket raggruppati il piu' possibile */
  DL_SORT(snapListUsers, string_el_compare_fd);

  list_string_el* str_curr_el = NULL;
  list_string_el* str_dummy_el = NULL;
  /* Scorro la lista degli utenti */
  DL_FOREACH_SAFE(snapListUsers, str_curr_el, str_dummy_el)
  {
    unsigned int hashed_valueFor = usersTable->hash_function(str_curr_el -> nickname);
    unsigned int bucketSetFor = (hashed_valueFor % HASH_DIM) / HASH_BUCKETS_PER_LOCK;
    /* Se non ho gia' fatto il lock, ad esempio Bucket set destinatario == Bucket set mittente */
    /* o ad esempio due utenti successivi nella listUsers appartengono allo stesso bucket set */
    /* Allora sblocco il lock sul bucket set e ne locko un altro */
    if(bucketSetFor != *last_locked_bucket_set)
    {
      UnlockHashTable(*last_locked_bucket_set);

      /*-----------------------------------------------------------------------------------*/
      /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
      /*-----------------------------------------------------------------------------------*/

      LockHashTable(bucketSetFor);
      *last_locked_bucket_set = bucketSetFor;
    }

    /* Ricontrollo se l'utente si sia disconnesso nel frattempo */
    list_string_el* recheck_str_curr_el = NULL;
    list_string_el recheck_str_tmp_el;
    strcpy(recheck_str_tmp_el.nickname, str_curr_el -> nickname);
    LockAndReadListUsers();
    /* Scorro la lista utenti/gruppi */
    DL_SEARCH(listUsers, recheck_str_curr_el, &recheck_str_tmp_el, string_el_compare_nick);
    if(recheck_str_curr_el != NULL)
    {  
      /* Se l'utente e' ancora registrato, aggiorno lo status e il file descriptor */
      str_curr_el -> online = recheck_str_curr_el -> online;
      str_curr_el -> fd = recheck_str_curr_el -> fd; 
    }
    UnlockListUsers();

    /* Se l'utente e' offline */
    if(str_curr_el -> online == FALSE)
    {
      /* Salva nei messaggi precedenti per tale utente */
      printf("[        ]Success, l'utente destinatario %s non e' online, salvo nei messaggi precedenti\n", str_curr_el -> nickname);
      User* utente_destinatario = icl_hash_find(usersTable, str_curr_el -> nickname);
      /* Se l'utente e' ancora presente nella hashtable */
      if(utente_destinatario != NULL)
      {                                
        saveInPreviousMessages(utente_destinatario, message_read, op_type);
        LockStats();
        if(op_type == TXT_MESSAGE)
        {
          updateStats(0, 0, 0, 1, 0, 0, 0); /* aggiorno la stat relativa ai messaggi non inviati */
        }
        else
        {
          updateStats(0, 0, 0, 0, 0, 1, 0); /* aggiorno la stat relativa ai file non inviati */
        }
        UnlockStats();
      }
      else
      {
        printf("[        ]Error, non e' stato possibile salvare il messaggio in previousMessages "
        "          l'utente potrebbe essersi deregistrato nel frattempo\n");
        LockStats();
        updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
        UnlockStats();
      }
    }
    else
    {
      /* Se l'utente e' online */
      message_read -> hdr.op = op_type;
      /* invio il messaggio al destinatario */
      if(sendRequest(str_curr_el -> fd, message_read) == -1)
      {
        /* Se ottengo una EPIPE in scrittura, salvo nei messaggi precedenti di tale utente */
        if(errno == EPIPE)
        {
          printf("[        ]Error, EPIPE con l'utente %s, salvo nei messaggi precedenti\n", str_curr_el -> nickname);
          User* utente_destinatario = icl_hash_find(usersTable, str_curr_el -> nickname);
          /* Se l'utente e' ancora presente nella hashtable */
          if(utente_destinatario != NULL)
          {
            saveInPreviousMessages(utente_destinatario, message_read, op_type);
            LockStats();
            if(op_type == TXT_MESSAGE)
            {
              updateStats(0, 0, 0, 1, 0, 0, 0); /* aggiorno la stat relativa ai messaggi non inviati */
            }
            else
            {
              updateStats(0, 0, 0, 0, 0, 1, 0); /* aggiorno la stat relativa ai file non inviati */
            }
            UnlockStats();
          }
          else
          {
            LockStats();
            updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
            UnlockStats();
          }
        }
        else
        {
          errExit("sendMessageToListOfUsers");
        }
      }
      else
      {
        /* Ho spedito con successo, aggiorno le stats */
        printf("[        ]Success, messaggio inviato con successo all'utente %s\n", str_curr_el -> nickname);
        LockStats();
        if(op_type == TXT_MESSAGE)
        {
          updateStats(0, 0, 1, 0, 0, 0, 0); /* aggiorno la stat relativa ai messaggi inviati */
        }
        else
        {
          updateStats(0, 0, 0, 0, 1, 0, 0); /* aggiorno la stat relativa ai file inviati */
        }
        UnlockStats();
      }
    }
    list_string_el* toFree = snapListUsers;
    DL_DELETE(snapListUsers, snapListUsers);
    free(toFree);
  }
  risposta_hdr -> op = OP_OK;
}

/**
 * @function sendMessageToUserOrGroup
 * @brief invia un messaggio testuale/file ad un utente o ad un gruppo
 * @param[in,out] message_read messaggio ricevuto dal client mittente
 * @param[in] bucketSet ultimo set di bucket lockato
 * @param[in] op_type tipo di messaggio che puo' essere TXT_MESSAGE oppure FILE_MESSAGE
 * @param[in,out] risposta_hdr messaggio di risposta verso il destinatario 
 * @return nothing
 */
static void sendMessageToUserOrGroup(message_t* message_read, const unsigned int* bucketSet, const op_t op_type, message_hdr_t* risposta_hdr)
{
  unsigned int hashed_value_destinatario = usersTable->hash_function(message_read -> data.hdr.receiver);
  unsigned int bucketSet_destinatario = (hashed_value_destinatario % HASH_DIM) / HASH_BUCKETS_PER_LOCK;
  unsigned int last_locked_bucket_set = *bucketSet;
  /* Se non ho gia' fatto il lock, ad esempio Bucket set destinatario == Bucket set mittente */
  /* Allora sblocco il lock sul bucket set e ne locko un altro */
  if(*bucketSet != bucketSet_destinatario)
  {
    //Non possiedo il lock del bucket dell'utente destinatario
    UnlockHashTable(*bucketSet);

    /*-----------------------------------------------------------------------------------*/
    /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
    /*-----------------------------------------------------------------------------------*/

    LockHashTable(bucketSet_destinatario);
    last_locked_bucket_set = bucketSet_destinatario;
  }

  User* utente_destinatario = icl_hash_find(usersTable, message_read -> data.hdr.receiver);
  /* se il destinatario esiste */
  if(utente_destinatario != NULL)
  {
    /* se il destinatario e' un gruppo */
    if(utente_destinatario -> isAGroup == TRUE)
    {
      /* devo inviare il messaggio a tutti gli utenti presenti nel gruppo specificato */
      risposta_hdr -> op = OP_OK;
      list_nick_el* gruppoDestinatario  = NULL;
      list_string_el* str_curr_el = NULL;
      list_string_el str_tmp_el;
      strcpy(str_tmp_el.nickname, message_read -> data.hdr.receiver);

      /* cerco il gruppo destinatario in listUsers */
      LockAndWriteListUsers();
      DL_SEARCH(listUsers, str_curr_el, &str_tmp_el, string_el_compare_nick);
      if(str_curr_el != NULL)
      {  
        assert(strcmp(str_curr_el -> nickname, message_read -> data.hdr.receiver) == 0);
        assert(str_curr_el -> groupMembers != NULL);
        gruppoDestinatario = str_curr_el -> groupMembers; /* aliasing */
      }
      /* devo assicurarmi che il mittente appartenga al gruppo */
      list_nick_el* nick_curr_el = NULL;
      list_nick_el nick_tmp_el;
      strcpy(nick_tmp_el.nickname, message_read -> hdr.sender);
      /* cerco il mittente nel gruppo destinatario */
      DL_SEARCH(gruppoDestinatario, nick_curr_el, &nick_tmp_el, nick_el_compare_nick);
      if(nick_curr_el != NULL)
      {
        /* l'utente appartiene al gruppo destinatario e quindi ha i permessi per
           inviare il messaggio al gruppo */
        list_string_el* snapListUsers  = NULL;
        str_curr_el = NULL;
        list_string_el* str_dummy_el = NULL;
        /* scorro la lista utenti/gruppi e aggiungo in una lista temporanea gli 
           utenti registrati al gruppo in quell'istante */
        DL_FOREACH_SAFE(listUsers, str_curr_el, str_dummy_el)
        {
          /* se non e' un gruppo */
          if(str_curr_el -> groupMembers == NULL)
          {
            list_nick_el* nick_curr_el = NULL;
            list_nick_el nick_tmp_el;
            strcpy(nick_tmp_el.nickname, str_curr_el -> nickname);
            /* cerco uno degli utenti nel gruppo */
            DL_SEARCH(gruppoDestinatario, nick_curr_el, &nick_tmp_el, nick_el_compare_nick);
            if(nick_curr_el != NULL)
            {
              /* l'utente appartiene al gruppo  destinatario */
              list_string_el* new_el = malloc(sizeof(list_string_el));
              strcpy(new_el -> nickname, str_curr_el -> nickname);
              new_el -> fd = str_curr_el -> fd;
              new_el -> online = str_curr_el -> online;
              /* aggiungo l'utente del gruppo alla lista temporanea */
              DL_APPEND(snapListUsers, new_el);            
            }
          }
        }
        UnlockListUsers();
        assert(snapListUsers != NULL);
        /* invio il messaggio agli utenti appartenenti alla lista temporanea,
           ovvero gli utenti appartenenti al gruppo */
        sendMessageToListOfUsers(message_read, snapListUsers, &last_locked_bucket_set, op_type, risposta_hdr);
      }
      else
      {
        UnlockListUsers();
        /* il mittente non appartiene al gruppo destinatario e non puo' inviare
           il messaggio al gruppo */
        printf("[        ]Error, %s non appartiene al gruppo %s\n"
        "          impossibile inviare il file/messaggio\n", 
        message_read -> hdr.sender, message_read -> data.hdr.receiver);
        risposta_hdr -> op = OP_NICK_UNKNOWN;
        LockStats();
        updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
        UnlockStats();
      }
      UnlockHashTable(last_locked_bucket_set);
    }
    else
    {
      /* se il destinatario e' un utente */
      list_string_el* str_curr_el = NULL;
      list_string_el str_tmp_el;
      strcpy(str_tmp_el.nickname, message_read -> data.hdr.receiver);
      int fd_destinatario = -1;
      int receiverIsOnline = 0;
      /* controllo se l'utente e' online e aggiorno il file descriptor */
      LockAndWriteListUsers();
      DL_SEARCH(listUsers, str_curr_el, &str_tmp_el, string_el_compare_nick);
      if(str_curr_el != NULL)
      {  
        if(str_curr_el -> online == TRUE)
          receiverIsOnline = 1;
        assert(strcmp(str_curr_el -> nickname, message_read -> data.hdr.receiver) == 0);
        fd_destinatario = str_curr_el -> fd;
      }
      UnlockListUsers();
      if(receiverIsOnline == 1)
      {
        /* l'utente destinatario e' online, mando il messaggio */
        message_read -> hdr.op = op_type;
        if(sendRequest(fd_destinatario, message_read) == -1)
        {
          /* se ottengo una EPIPE in scrittura, salvo nei messaggi precedenti di tale utente */
          if(errno == EPIPE)
          {
            printf("[        ]Error, EPIPE con l'utente %s, salvo nei messaggi precedenti\n", message_read -> data.hdr.receiver);
            User* utente_destinatario = icl_hash_find(usersTable, message_read -> data.hdr.receiver);
            /* Se l'utente e' ancora presente nella hashtable */
            if(utente_destinatario != NULL)
            {
              saveInPreviousMessages(utente_destinatario, message_read, op_type);
              LockStats();
              if(op_type == TXT_MESSAGE)
              {
                updateStats(0, 0, 0, 1, 0, 0, 0); /* aggiorno la stat relativa ai messaggi non inviati */
              }
              else
              {
                updateStats(0, 0, 0, 0, 0, 1, 0); /* aggiorno la stat relativa ai file non inviati */
              }
              UnlockStats();
            }
            else
            {
              LockStats();
              updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
              UnlockStats();
            }
          }
          else
          {
            errExit("sendMessageToUserOrGroup");
          }
        }
        else
        {
          //Ho spedito con successo, aggiorno le stats
          printf("[        ]Success, messaggio inviato con successo all'utente %s\n", str_curr_el -> nickname);
          LockStats();
          if(op_type == TXT_MESSAGE)
          {
            updateStats(0, 0, 1, 0, 0, 0, 0); /* aggiorno la stat relativa ai messaggi inviati */
          }
          else
          {
            updateStats(0, 0, 0, 0, 1, 0, 0); /* aggiorno la stat relativa ai file inviati */
          }
          UnlockStats();
        }
      }
      else
      {
        //L'utente e' offline, salvo nei messaggi precedenti
        printf("[        ]Success, l'utente destinatario %s non e' online, salvo nei messaggi precedenti\n",
               str_curr_el -> nickname);
        saveInPreviousMessages(utente_destinatario, message_read, op_type);
        LockStats();
        if(op_type == TXT_MESSAGE)
        {
          updateStats(0, 0, 0, 1, 0, 0, 0); /* aggiorno la stat relativa ai messaggi non inviati */
        }
        else
        {
          updateStats(0, 0, 0, 0, 0, 1, 0); /* aggiorno la stat relativa ai file non inviati */
        }
        UnlockStats();
      }
      risposta_hdr -> op = OP_OK;
    }
  }
  else
  {
    /* il destinatario non esiste */
    risposta_hdr -> op = OP_NICK_UNKNOWN;
    printf("[        ]Error, l'utente destinatario %s non risulta registrato\n"
    "          impossibile inviare il file\n", message_read -> data.hdr.receiver);
    LockStats();
    updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
    UnlockStats();           
  }
  if(*bucketSet != bucketSet_destinatario)
  {
    UnlockHashTable(bucketSet_destinatario);
  }
  else
  {
    UnlockHashTable(*bucketSet);
  }
}

/**
 * @function registerFun
 * @brief registra un nuovo utente se non e' gia' presente
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */
static void registerFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */

  /* controllo se il client mi stia mandando un nome troppo lungo,
     con l'assunzione che nella setHeader sia stata fatta una memset 0 del nickname */
  if(message_read -> hdr.sender[MAX_NAME_LENGTH] != '\0')
  {
    /* se supero il limite */
    printf("[FD: %4d]OP: REGISTER    Sender: ?\n", *cfd);
    risposta_hdr -> op = OP_FAIL;
    printf("[FD: %4d]Error, nickname troppo lungo\n", *cfd);
    LockStats();
    updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
    UnlockStats();
    if(*userExists == 1)
      UnlockHashTable(*bucketSet);
  }
  else
  {
    /* il nickname rientra nei limiti */
    printf("[FD: %4d]OP: REGISTER    Sender: %s\n", *cfd, message_read -> hdr.sender);
    User* utente = NULL;
    if(*userExists == 0)
      LockHashTable(*bucketSet);

    /* cerco il mittente nella hashtable */
    utente = icl_hash_find(usersTable, message_read -> hdr.sender);
    if(utente != NULL)
    {
      /* se esiste gia' un utente/gruppo associato a tale nome */
      if(utente -> isAGroup == TRUE)
      {
        /* se esiste gia' un gruppo con il nome che si intende registrare */
        printf("[FD: %4d]Error, esiste gia' un gruppo con il nome %s\n", *cfd, message_read -> hdr.sender);
      }
      else
      {
        /* se esiste gia' un gruppo con il nome che si intende registrare */
        printf("[FD: %4d]Error, esiste gia' un utente con il nome %s\n", *cfd, message_read -> hdr.sender);
      }
      risposta_hdr -> op = OP_NICK_ALREADY;
      LockStats();
      updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
      UnlockStats();
      UnlockHashTable(*bucketSet);
    }
    else
    {
      /* se non esiste un utente/gruppo associato a tale nome */
      utente = (User*) malloc(sizeof(User));
      if(utente == NULL)
        errExit("malloc");
      utente -> previousMessages = malloc(maxHistMsgs * sizeof(message_t));
      memset(utente -> previousMessages, 0, maxHistMsgs * sizeof(message_t));

      if(utente -> previousMessages == NULL)
        errExit("malloc");
      utente -> num_pending_messages = 0;
      utente -> index_pending_messages = 0;
      utente -> isAGroup = FALSE;
      list_string_el* str_curr_el = NULL;
      list_string_el* new_online = malloc(sizeof(list_string_el));
      strcpy(new_online -> nickname, message_read -> hdr.sender);
      new_online -> fd = *cfd;
      new_online -> online = TRUE;
      new_online -> groupMembers = NULL;
      LockAndWriteListUsers();
      DL_SEARCH(listUsers, str_curr_el, new_online, string_el_compare_nick);
      /* Aggiungo il nuovo utente alla listUsers */
      if(str_curr_el == NULL)
      {  
        DL_APPEND(listUsers, new_online);               
      }
      else
      {
        errExit("REGISTER_OP");
      }
      UnlockListUsers();
      char* new_user = malloc((MAX_NAME_LENGTH + 1 ) * sizeof(char));
      strcpy(new_user, message_read -> hdr.sender);
      /* inserisco il nuovo utente anche nella hashtable */
      if((icl_hash_insert(usersTable, new_user, utente)) == NULL)
        errExit("icl_hash_insert");
      printf("[FD: %4d]Success, utente %s nuovo, inserito con successo nella HashTable\n", 
             *cfd, message_read -> hdr.sender);
      risposta_hdr -> op = OP_OK;
      LockStats();
      updateStats(1, 1, 0, 0, 0, 0, 0); /* aggiorno le stat relative, nell'ordine,
                                          a num. utenti registrati e num. utenti connessi */
      UnlockStats();
      UnlockHashTable(*bucketSet);
    }
  }
  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

  LockHashTable(*bucketSet);
  if(sendHdr(*cfd, risposta_hdr) == -1)
    errExit("sendHdr REGISTER_OP");
  /*-----------------------------------------------------------------------------------*/
  /* In questo istante non posso ancora lasciare il lock                               */
  /*-----------------------------------------------------------------------------------*/ 
  
  /* Mando la lista degli utenti connessi solo se non ci sono stati problemi */
  if(risposta_hdr -> op == OP_OK)
  {
    sendConnectedUsersList(*cfd);
  }
  /* Dopo aver mandato o meno la lista, posso lasciare il lock */
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

}

/**
 * @function connectFun
 * @brief connette un utente se e' gia' registrato
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */
static void connectFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */
  if(*userExists == 0)
    LockHashTable(*bucketSet);
  printf("[FD: %4d]OP: CONNECT     Sender: %s\n", *cfd, message_read -> hdr.sender);
  /* cerco il mittente nella hashtable */
  User* utente = icl_hash_find(usersTable, message_read -> hdr.sender);
  if(utente != NULL)
  {
    /* se esiste una chiave nella hash table */
    if(utente -> isAGroup == FALSE)
    {
      /* e' un utente e risulta registrato */
      printf("[FD: %4d]Success, login utente %s effettuato\n", *cfd, message_read -> hdr.sender);
      list_string_el* str_curr_el = NULL;
      list_string_el new_online;
      strcpy(new_online.nickname, message_read -> hdr.sender);
      LockAndWriteListUsers();
      /* aggiorno listUsers con il nuovo utente */
      DL_SEARCH(listUsers, str_curr_el, &new_online, string_el_compare_nick);
      if(str_curr_el != NULL)
      {  
				if(str_curr_el -> online == FALSE)
				{
					LockStats();
					updateStats(0, 1, 0, 0, 0, 0, 0); /* aggiorno la stat relativa a num. utenti connessi */
					UnlockStats();
				}
        str_curr_el -> online = TRUE;
        str_curr_el -> fd = *cfd;         
      }
      else
      {
        errExit("connect_op");
      }
      UnlockListUsers();
      risposta_hdr -> op = OP_OK;
    }
    else
    {
      /* e' un gruppo e quindi l'utente non risulta registrato */
      printf("[FD: %4d]Error, utente %s nuovo, esiste gia' un gruppo con questo nome"
                             " non e' possibile connettersi\n", *cfd, message_read -> hdr.sender);
      risposta_hdr -> op = OP_NICK_UNKNOWN;
      LockStats();
      updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
      UnlockStats();
    }
  }
  else
  {
    /* non esiste una chiave nella hash table */
    printf("[FD: %4d]Error, utente %s nuovo, non e' possibile connettersi\n", *cfd, message_read -> hdr.sender);
    risposta_hdr -> op = OP_NICK_UNKNOWN;
    LockStats();
    updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
    UnlockStats();
  }
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

  LockHashTable(*bucketSet);
  if(sendHdr(*cfd, risposta_hdr) == -1)
    errExit("sendHdr REGISTER_OP");
  /*-----------------------------------------------------------------------------------*/
  /* In questo istante non posso ancora lasciare il lock                               */
  /*-----------------------------------------------------------------------------------*/

  /* Mando la lista degli utenti connessi solo se non ci sono stati problemi */
  if(risposta_hdr -> op == OP_OK)
  {
    sendConnectedUsersList(*cfd);
  }
  /* Dopo aver mandato o meno la lista, posso lasciare il lock */
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

}

/**
 * @function unregisterFun
 * @brief deregistra un utente
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */

static void unregisterFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */
  if(*userExists == 0)
  {
    LockHashTable(*bucketSet);
    /* l'utente puo' aver fatto la deregistrazione e aver chiesto un'altra
       operazione nella stessa sessione */
    risposta_hdr -> op = OP_FAIL;
    printf("[FD: %4d]Error, l'utente %s non risulta piu' registrato\n", *cfd, message_read -> hdr.sender);
  }
  else
  {
    printf("[FD: %4d]OP: UNREGISTER  Sender: %s\n", *cfd, message_read -> hdr.sender);
    /* cerco il mittente nella hashtable */
    User* utente = icl_hash_find(usersTable, message_read -> hdr.sender);
    assert(utente != NULL);
    assert(utente -> isAGroup == FALSE);

    /* elimino l'utente dalla hashtable */
    if(icl_hash_delete(usersTable, message_read -> hdr.sender , free_hashtable_key, free_hashtable_data) == -1)
      errExit("icl_hash_delete");

    /* Elimino il nickname dell'utente da tutti i gruppi cui faceva parte */
    list_string_el* str_curr_el = NULL;
    list_string_el* str_dummy_el = NULL;

    /* Lista temporanea di nomi dei gruppi in cui l'utente sia fondatore */
    list_nick_el* founderOfList = NULL;
    list_nick_el* nick_curr_el = NULL;
    list_nick_el nick_tmp_el;
    LockAndWriteListUsers();
    /* scorro listUsers */
    DL_FOREACH_SAFE(listUsers, str_curr_el , str_dummy_el)
    {
      /* se e' un gruppo */
      if(str_curr_el -> groupMembers != NULL)
      {
        /* e' un gruppo, scorro la lista dei membri di tale gruppo */
        strcpy(nick_tmp_el.nickname, message_read -> hdr.sender);
        DL_SEARCH(str_curr_el -> groupMembers, nick_curr_el, &nick_tmp_el, nick_el_compare_nick);
        if(nick_curr_el != NULL)
        {
          /* sono all'interno del gruppo;
            se sono il founder del gruppo (il primo elemento della lista corrisponde
            al founder del gruppo) allora salvo in una lista temporanea il nome del gruppo, 
            altrimenti elimino subito l'utente dal gruppo */
          if(strcmp(message_read -> hdr.sender, str_curr_el -> groupMembers -> nickname) == 0)
          {
            /* sono fondatore del gruppo;
              salvo in una lista temporanea i nomi dei gruppi in cui sono fondatore */
            list_nick_el* group_name = malloc(sizeof(list_nick_el));
            strcpy(group_name -> nickname, str_curr_el -> nickname);
            DL_APPEND(founderOfList, group_name);
          }
          else
          {
            /* non sono il fondatore del gruppo, esco subito dal gruppo */
            printf("[FD: %4d]Success, rimozione dell'utente %s dal gruppo %s effettuata con successo\n", 
                    *cfd, message_read -> hdr.sender, str_curr_el -> nickname);
            DL_DELETE(str_curr_el -> groupMembers, nick_curr_el);
            free(nick_curr_el -> nickname);
          }
        }
      }
    }
    str_curr_el = NULL;
    list_string_el del_online;
    strcpy(del_online.nickname, message_read -> hdr.sender);
    /* scorro listUsers */
    DL_SEARCH(listUsers, str_curr_el, &del_online, string_el_compare_nick);
    if(str_curr_el != NULL)
    {  
      /* elimino l'utente dalla lista degli utenti registrati */
      DL_DELETE(listUsers, str_curr_el);    
      free(str_curr_el);    
    }
    UnlockListUsers();

    //Posso lavorare sulla copia tranquillamente
    unsigned int last_locked_bucket_set = *bucketSet;
    nick_curr_el = NULL;
    list_nick_el* nick_dummy_el = NULL;
    list_string_el str_tmp_el;

    DL_FOREACH_SAFE(founderOfList, nick_curr_el, nick_dummy_el)
    {
      unsigned int hashed_valueFor = usersTable->hash_function(nick_curr_el -> nickname);
      unsigned int bucketSetFor = (hashed_valueFor % HASH_DIM) / HASH_BUCKETS_PER_LOCK;
      //Locko la regione della Hash Table a patto che questa:
      // - non la abbia gia' lockata (ad esempio Bucket destinatario == Bucket mittente)
      // - non la abbia gia' lockata (due utenti successivi nella listUsers appartengono allo stesso bucket)
      if(bucketSetFor != last_locked_bucket_set)
      {
        UnlockHashTable(last_locked_bucket_set);
        last_locked_bucket_set = bucketSetFor;
        LockHashTable(bucketSetFor);
      }
      printf("[FD: %4d]Success, rimozione dell'intero gruppo con nome %s da parte del fondatore %s\n", 
              *cfd, nick_curr_el -> nickname, message_read -> hdr.sender);
      
      //Rimuovo la chiave del nome gruppo dalla Hash Table
      User* gruppo = icl_hash_find(usersTable, nick_curr_el -> nickname);
      assert(gruppo != NULL);
      assert(gruppo -> isAGroup == TRUE);
      if(icl_hash_delete(usersTable, nick_curr_el -> nickname, free_hashtable_key, free_hashtable_data) == -1)
        errExit("icl_hash_delete");

      LockAndWriteListUsers();
      //Devo cercare il gruppo nella listUsers
      str_curr_el = NULL;
      strcpy(str_tmp_el.nickname, nick_curr_el -> nickname);
        
      DL_SEARCH(listUsers, str_curr_el, &str_tmp_el, string_el_compare_nick);
      assert(str_curr_el != NULL);
      assert(strcmp(str_curr_el -> nickname, nick_curr_el -> nickname) == 0);
      //Devo eliminare il gruppo anche da listUsers...

      list_nick_el* g_nick_curr_el = NULL;
      list_nick_el* g_nick_dummy_el = NULL;
      DL_FOREACH_SAFE(str_curr_el -> groupMembers, g_nick_curr_el, g_nick_dummy_el)
      {
        DL_DELETE(str_curr_el -> groupMembers, g_nick_curr_el);
        free(g_nick_curr_el);
      }
      DL_DELETE(listUsers, str_curr_el);             
      free(str_curr_el);

      DL_DELETE(founderOfList, nick_curr_el);             
      free(nick_curr_el);
      UnlockListUsers();
    }
    UnlockHashTable(last_locked_bucket_set);
    risposta_hdr -> op = OP_OK;
    LockStats();
    updateStats(-1, -1, 0, 0, 0, 0, 0);
    UnlockStats();
    printf("[FD: %4d]Success, l'utente %s e' stato rimosso con successo dalla hash table\n", *cfd, message_read -> hdr.sender);
    /*-----------------------------------------------------------------------------------*/
    /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
    /*-----------------------------------------------------------------------------------*/

    LockHashTable(*bucketSet);
  }
  if(sendHdr(*cfd, risposta_hdr) == -1)
    errExit("sendHdr UNREGISTER_OP");
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

}

/**
 * @function disconnectFun
 * @brief disconnette un utente
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */
static void disconnectFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */
  if(*userExists == 0)
  {
    LockHashTable(*bucketSet);
    /* l'utente puo' aver fatto la deregistrazione e aver chiesto un'altra
       operazione nella stessa sessione */
    risposta_hdr -> op = OP_FAIL;
    printf("[FD: %4d]Error, l'utente %s non risulta piu' registrato\n", *cfd, message_read -> hdr.sender);
  }
  else
  {
    printf("[FD: %4d]OP: DISCONNECT  Sender: %s\n", *cfd, message_read -> hdr.sender);
    /* cerco il mittente nella hashtable */
    User* utente = icl_hash_find(usersTable, message_read -> hdr.sender);
    assert(utente != NULL);
    assert(utente -> isAGroup == FALSE);
    list_string_el* str_curr_el = NULL;
    list_string_el del_online;
    strcpy(del_online.nickname, message_read -> hdr.sender);
    LockAndWriteListUsers();
    /* scorro listUsers */
    DL_SEARCH(listUsers, str_curr_el, &del_online, string_el_compare_nick);
    if(str_curr_el != NULL)
    {  
      str_curr_el -> online = FALSE;
      str_curr_el -> fd = -1;    
    }
    else
    {
      errExit("disconnect_op");
    }
    UnlockListUsers();
    risposta_hdr -> op = OP_OK;
    LockStats();
    updateStats(0, -1, 0, 0, 0, 0, 0); /* aggiorno la stat relativa agli utenti connessi */
    UnlockStats();
    printf("[FD: %4d]Success, logout utente %s effettuato\n", *cfd, message_read -> hdr.sender);
  }
  if(sendHdr(*cfd, risposta_hdr) == -1)
    errExit("sendHdr DISCONNECT_OP");
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

}

/**
 * @function usrlistFun
 * @brief invia la lista degli utenti connessi
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */
static void usrlistFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */
  if(*userExists == 0)
  {
    LockHashTable(*bucketSet);
    /* l'utente puo' aver fatto la deregistrazione e aver chiesto un'altra
       operazione nella stessa sessione */
    risposta_hdr -> op = OP_FAIL;
    printf("[FD: %4d]Error, l'utente %s non risulta piu' registrato\n", *cfd, message_read -> hdr.sender);
    if(sendHdr(*cfd, risposta_hdr) == -1)
      errExit("sendHdr USRLIST_OP");
  }
  else
  {
    printf("[FD: %4d]OP: USRLIST     Sender: %s\n", *cfd, message_read -> hdr.sender);
    /* cerco il mittente nella hashtable */
    User* utente = icl_hash_find(usersTable, message_read -> hdr.sender);
    assert(utente != NULL);
    assert(utente -> isAGroup == FALSE);
    risposta_hdr -> op = OP_OK;

    UnlockHashTable(*bucketSet);

    /*-----------------------------------------------------------------------------------*/
    /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
    /*-----------------------------------------------------------------------------------*/

    LockHashTable(*bucketSet);
    if(sendHdr(*cfd, risposta_hdr) == -1)
      errExit("sendHdr USRLIST_OP");
    /*-----------------------------------------------------------------------------------*/
    /* In questo istante non posso ancora lasciare il lock                               */
    /*-----------------------------------------------------------------------------------*/ 
    //Mando la lista degli utenti connessi
    sendConnectedUsersList(*cfd);
    //Dopo aver mandato la lista, posso lasciare il lock
  }
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

}

/**
 * @function posttxtFun
 * @brief manda un messaggio ad un utente o a un gruppo
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */
static void posttxtFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */
  if(*userExists == 0)
  {
    LockHashTable(*bucketSet);
    /* l'utente puo' aver fatto la deregistrazione e aver chiesto un'altra
       operazione nella stessa sessione */
    risposta_hdr -> op = OP_FAIL;
    printf("[FD: %4d]Error, l'utente %s non risulta piu' registrato\n", *cfd, message_read -> hdr.sender);
  }
  else
  {
    printf("[FD: %4d]OP: POSTTXT     Sender: %s\n", *cfd, message_read -> hdr.sender);
    /* cerco il mittente nella hashtable */
    User* utente = icl_hash_find(usersTable, message_read -> hdr.sender);
    assert(utente != NULL);
    /* controllo se il messaggio e' troppo lungo */
    if((message_read -> data.hdr.len) > maxMsgSize)
    {
      /* il messaggio e' troppo lungo per essere inviato */
      risposta_hdr -> op = OP_MSG_TOOLONG;
      printf("[FD: %4d]Error, messaggio troppo lungo, operazione fallita\n", *cfd);
      LockStats();
      updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
      UnlockStats();
      UnlockHashTable(*bucketSet);
    }
    else
    {
      /* il messaggio rispetta la lunghezza maxMsgSize, lo invio */
      sendMessageToUserOrGroup(message_read, bucketSet, TXT_MESSAGE, risposta_hdr);
    }

    /*-----------------------------------------------------------------------------------*/
    /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
    /*-----------------------------------------------------------------------------------*/

    LockHashTable(*bucketSet);
  }
  if(sendHdr(*cfd, risposta_hdr) == -1)
    errExit("sendHdr POSTTXT_OP");
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

  free(message_read -> data.buf);
}

/**
 * @function posttxtallFun
 * @brief manda un messaggio ad uno o piu' utenti
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */
static void posttxtallFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */
  if(*userExists == 0)
  {
    LockHashTable(*bucketSet);
    /* l'utente puo' aver fatto la deregistrazione e aver chiesto un'altra
       operazione nella stessa sessione */
    risposta_hdr -> op = OP_FAIL;
    printf("[FD: %4d]Error, l'utente %s non risulta piu' registrato\n", *cfd, message_read -> hdr.sender);
  }
  else
  {
    printf("[FD: %4d]OP: POSTTXTALL  Sender: %s\n", *cfd, message_read -> hdr.sender);
    /* cerco il mittente nella hashtable */
    User* utente = icl_hash_find(usersTable, message_read -> hdr.sender);
    assert(utente != NULL);
    /* controllo se il messaggio e' troppo lungo */
    if((message_read -> data.hdr.len) > maxMsgSize)
    {
      /* il messaggio e' troppo lungo per essere inviato */
      risposta_hdr -> op = OP_MSG_TOOLONG;
      printf("[FD: %4d]Error, messaggio troppo lungo, operazione fallita\n", *cfd);          
      LockStats();
      updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
      UnlockStats();
      UnlockHashTable(*bucketSet);
    }
    else
    {
      /* il messaggio rispetta la lunghezza maxMsgSize, lo invio */
      risposta_hdr -> op = OP_OK;
      /* Salvo in una lista temporanea gli utenti registrati */
      list_string_el* snapListUsers  = NULL;
      list_string_el* str_curr_el = NULL;
      list_string_el* str_dummy_el = NULL;
      LockAndReadListUsers();
      /* scorro listUsers */
      DL_FOREACH_SAFE(listUsers, str_curr_el, str_dummy_el)
      {
        /* Aggiungo alla copia solo gli utenti, evitando di inserire i gruppi */
        if(str_curr_el -> groupMembers == NULL)
        {
          list_string_el* new_el = malloc(sizeof(list_string_el));
          strcpy(new_el -> nickname, str_curr_el -> nickname);
          new_el -> fd = str_curr_el -> fd;
          new_el -> online = str_curr_el -> online;
          DL_APPEND(snapListUsers, new_el);
        }
      }
      UnlockListUsers();
      unsigned int last_locked_bucket_set = *bucketSet;
      assert(snapListUsers != NULL);
      /* invio il messaggio agli utenti presenti nella lista temporanea */
      sendMessageToListOfUsers(message_read, snapListUsers, &last_locked_bucket_set, TXT_MESSAGE, risposta_hdr);
      UnlockHashTable(last_locked_bucket_set);
    }
    /*-----------------------------------------------------------------------------------*/
    /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
    /*-----------------------------------------------------------------------------------*/

    LockHashTable(*bucketSet);
  }
  if(sendHdr(*cfd, risposta_hdr) == -1)
    errExit("sendHdr POSTTXTALL_OP");
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

  free(message_read -> data.buf);
}

/**
 * @function postfileFun
 * @brief manda un file ad un utente o un gruppo
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */
static void postfileFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */
  if(*userExists == 0)
  {
    LockHashTable(*bucketSet);
    /* l'utente puo' aver fatto la deregistrazione e aver chiesto un'altra
       operazione nella stessa sessione */
    risposta_hdr -> op = OP_FAIL;
    printf("[FD: %4d]Error, l'utente %s non risulta piu' registrato\n", *cfd, message_read -> hdr.sender);
  }
  else
  {
    printf("[FD: %4d]OP: POSTFILE    Sender: %s\n", *cfd, message_read -> hdr.sender);
    /* cerco il mittente nella hashtable */
    User* utente = icl_hash_find(usersTable, message_read -> hdr.sender);
    assert(utente != NULL);
    /* prendo l'ultimo carattere presente nella stringa dirName */
    char last_char = dirName[strlen(dirName) - 1];
    char* fullPath;
    /* se l'ultimo carattere della stringa dirName non e' uno slash / 
      allora lo aggiungo manualmente */
    if(last_char == '/')
    {
      fullPath = malloc( (strlen(dirName) + strlen(message_read -> data.buf) + 1) * sizeof(char));
      fullPath[0] = '\0';
      strcat(fullPath, dirName);
      strcat(fullPath, message_read -> data.buf);
    }
    else
    {
      fullPath = malloc( (strlen(dirName) + strlen(message_read -> data.buf) + 2) * sizeof(char));
      fullPath[0] = '\0';
      strcat(fullPath, dirName);
      strcat(fullPath, "/");
      strcat(fullPath, message_read -> data.buf);
    }
    /* scarico il file con path fullPath in locale */
    int ris = readDataServerSide(*cfd, fullPath);
    free(fullPath);
    if(ris == -2)
    {
      /* il file e' troppo grande per essere memorizzato nel server */
      risposta_hdr -> op = OP_MSG_TOOLONG;
      printf("[FD: %4d]Error, il file e' troppo grande e non puo' essere accettato\n", *cfd);
      LockStats();
      updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
      UnlockStats();

      UnlockHashTable(*bucketSet);
    }
    else if(ris == 1)
    {
      /* il file rientra nelle dimensioni consentite ed e' stato memorizzato */
      /* invio il file all'utente o al gruppo destinatario */
      sendMessageToUserOrGroup(message_read, bucketSet, FILE_MESSAGE, risposta_hdr);
    }
    else
    {
      errExit("readDataServerSide");
    }

    /*-----------------------------------------------------------------------------------*/
    /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
    /*-----------------------------------------------------------------------------------*/

    LockHashTable(*bucketSet);
  }
  if(sendHdr(*cfd, risposta_hdr) == -1)
    errExit("sendHdr POSTFILE_OP");
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

  free(message_read -> data.buf);
}

/**
 * @function getfileFun
 * @brief manda un file al mittente
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */
static void getfileFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */
  if(*userExists == 0)
  {
    LockHashTable(*bucketSet);
    /* l'utente puo' aver fatto la deregistrazione e aver chiesto un'altra
       operazione nella stessa sessione */
    risposta_hdr -> op = OP_FAIL;
    printf("[FD: %4d]Error, l'utente %s non risulta piu' registrato\n", *cfd, message_read -> hdr.sender);
    if(sendHdr(*cfd, risposta_hdr) == -1)
      errExit("sendHdr GETFILE_OP");
  }
  else
  {
    printf("[FD: %4d]OP: GETFILE     Sender: %s\n", *cfd, message_read -> hdr.sender);
    /* cerco il mittente nella hashtable */
    User* utente = icl_hash_find(usersTable, message_read -> hdr.sender);
    assert(utente != NULL);
    int file_fd;
    
    /* tento di aprire il file in sola lettura */
    file_fd = open(message_read -> data.buf, O_RDONLY);
    if(file_fd == -1 && errno == ENOENT)
    {
      /* il file specificato non esiste e quindi il mittente non puo' riceverlo */
      risposta_hdr -> op = OP_NO_SUCH_FILE;
      printf("[FD: %4d]Error, non esiste il file specificato, l'utente %s non puo' riceverlo\n", *cfd, message_read -> hdr.sender);
      LockStats();
      updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
      UnlockStats();
    }
    else if(file_fd == -1 && errno != ENOENT)
    {
      errExit("open");
    }
    else
    {
      /* ho aperto il file con successo;
        assumo che le statistiche riguardanti "n. file consegnati" non si riferiscano 
        alla GETFILE_OP bensi' alla POSTFILE_OP */
      struct stat sb;
      if (fstat(file_fd, &sb) == -1)
        errExit("fstat");
      char* mappedfile = NULL;
      /* mappo il file in memoria */
      mappedfile = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
      if(mappedfile == MAP_FAILED)
        errExit("mmap");
      close(file_fd);
      message_data_t* risposta_data = NULL;
      risposta_data = malloc(sizeof(message_data_t));
      if(!risposta_data)
        errExit("malloc");
      memset(risposta_data, 0, sizeof(message_data_t));
      risposta_data -> hdr.len = sb.st_size;
      risposta_data -> buf = mappedfile;
      risposta_hdr -> op = OP_OK;
      UnlockHashTable(*bucketSet);

      /*-----------------------------------------------------------------------------------*/
      /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
      /*-----------------------------------------------------------------------------------*/

      LockHashTable(*bucketSet);
      if(sendHdr(*cfd, risposta_hdr) == -1)
        errExit("sendHdr GETFILE_OP");

      /* Qui ancora non posso lasciare il lock */

      /* mando effettivamente il file al mittente */
      if(sendDataServerSide(*cfd, risposta_data) == -1)
        errExit("sendDataServerSide GETFILE_OP");
      free(risposta_data);
      munmap(mappedfile, sb.st_size);
    }
  }
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

  free(message_read -> data.buf);
}

/**
 * @function getprevmsgsFun
 * @brief manda zero o piu' messaggi precedenti al mittente
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */
static void getprevmsgsFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */
  /* Assumo che le statistiche riguardanti "n. messaggi consegnati" 
     non si riferiscano alle GETPREVMSGS_OP bensi' alla POSTTXT/POSTTXTALL */
  if(*userExists == 0)
  {
    LockHashTable(*bucketSet);
    /* l'utente puo' aver fatto la deregistrazione e aver chiesto un'altra
       operazione nella stessa sessione */
    risposta_hdr -> op = OP_FAIL;
    printf("[FD: %4d]Error, l'utente %s non risulta piu' registrato\n", *cfd, message_read -> hdr.sender);
    if(sendHdr(*cfd, risposta_hdr) == -1)
      errExit("sendHdr Getpreviousmsgs");
  }
  else
  {
    printf("[FD: %4d]OP: GETPREVMSGS Sender: %s\n", *cfd, message_read -> hdr.sender);
    /* cerco nella hashtable il mittente */
    User* utente = icl_hash_find(usersTable, message_read -> hdr.sender);
    assert(utente != NULL);
    risposta_hdr -> op = OP_OK;
    UnlockHashTable(*bucketSet);

    /*-----------------------------------------------------------------------------------*/
    /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
    /*-----------------------------------------------------------------------------------*/

    LockHashTable(*bucketSet);
    if(sendHdr(*cfd, risposta_hdr) == -1)
      errExit("sendHdr Getpreviousmsgs");
    
    message_data_t* risposta_data = NULL;
    risposta_data = malloc(sizeof(message_data_t));
    if(!risposta_data)
      errExit("malloc");
    memset(risposta_data, 0, sizeof(message_data_t));

    /* scrivo il numero dei messaggi precedenti in nmsgs */
    size_t* nmsgs = malloc(sizeof(size_t));
    *nmsgs = (size_t)utente -> num_pending_messages;
    risposta_data -> buf = malloc(sizeof(size_t));
    memcpy(risposta_data -> buf, nmsgs, sizeof(size_t));
    free(nmsgs);
    risposta_data -> hdr.len = sizeof(size_t);
    
    /* invio il numero dei messaggi precedenti al mittente */
    if(sendDataServerSide(*cfd, risposta_data) == -1)
      errExit("sendDataServerSide GETPREVIOUSMSGS");
    
    free(risposta_data -> buf);
    free(risposta_data);

    /* Qui non posso ancora rilasciare il lock! */

    message_t *item_destinatario = utente -> previousMessages;  /* aliasing */
    int num_pending = utente -> num_pending_messages;
    int indice = utente -> index_pending_messages;

    indice = indice - num_pending;
    if(indice < 0)
      indice += maxHistMsgs;

    /* mando tutti i messaggi precedenti al mittente */
    for(int i = 0; i < num_pending; i++)
    {
      if(sendRequest(*cfd, &item_destinatario[indice]) == -1)
        errExit("sendRequest getpreviousmsgs");
      free(item_destinatario[indice].data.buf);
      (utente -> num_pending_messages)--;
      indice++;
      if(indice >= maxHistMsgs)
        indice -= maxHistMsgs;
    }
  }
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

}

/**
 * @function creategroupFun
 * @brief crea un nuovo gruppo
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */
static void creategroupFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */
  printf("[FD: %4d]OP: CREATEGROUP Sender: %s\n", *cfd, message_read -> hdr.sender);
  User* utente = NULL;
  if(*userExists == 0)
  {
    LockHashTable(*bucketSet);
    /* l'utente puo' aver fatto la deregistrazione e aver chiesto un'altra
       operazione nella stessa sessione */
    risposta_hdr -> op = OP_FAIL;
    printf("[FD: %4d]Error, l'utente %s non risulta piu' registrato\n", *cfd, message_read -> hdr.sender);
  }
  else
  {
    /* cerco il mittente nella hashtable */
    utente = icl_hash_find(usersTable, message_read -> hdr.sender);
    assert(utente != NULL);
    assert(utente -> isAGroup == FALSE);
    
    unsigned int hashed_value_gruppo = usersTable->hash_function(message_read -> data.hdr.receiver);
    unsigned int bucketSet_gruppo = (hashed_value_gruppo % HASH_DIM) / HASH_BUCKETS_PER_LOCK;
    /* Se il gruppo appartiene allo stesso BucketSet, allora possiedo gia' il lock
      altrimenti prendo il lock dell'altro BucketSet */
    if(*bucketSet != bucketSet_gruppo)
    {
      UnlockHashTable(*bucketSet);

      /*-----------------------------------------------------------------------------------*/
      /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
      /*-----------------------------------------------------------------------------------*/

      LockHashTable(bucketSet_gruppo);
    }

    /* qui ho il lock sul gruppo */
    /* cerco il gruppo nella hashtable */
    User* gruppo = icl_hash_find(usersTable, message_read -> data.hdr.receiver);
    if(gruppo != NULL)
    {
      /* esiste una chiave con il nome nella hashtable */
      if(gruppo -> isAGroup == TRUE)
      {
        /* esiste gia' un gruppo con il nome */
        printf("[FD: %4d]Error, esiste gia' un gruppo con il nome %s\n", *cfd, message_read -> data.hdr.receiver);
      }
      else
      {
        /* esiste gia' un utente con il nome */
        printf("[FD: %4d]Error, esiste gia' un utente con il nome %s\n", *cfd, message_read -> data.hdr.receiver);
      }
      risposta_hdr -> op = OP_NICK_ALREADY;
      LockStats();
      updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
      UnlockStats();
    }
    else
    {
      /* non esiste una chiave con il nome nella hashtable, quindi
      il gruppo non esiste e non ci sono utenti con tale nome */
      gruppo = (User*) malloc(sizeof(User));
      if(gruppo == NULL)
        errExit("malloc");
      gruppo -> previousMessages = NULL;
      gruppo -> num_pending_messages = 0;
      gruppo -> index_pending_messages = 0;
      gruppo -> isAGroup = TRUE;

      list_string_el* str_curr_el = NULL;
      list_string_el* new_group = malloc(sizeof(list_string_el));
      strcpy(new_group -> nickname, message_read -> data.hdr.receiver);
      new_group -> fd = -1;
      new_group -> online = FALSE;
      new_group -> groupMembers = NULL;


      /* Inserisco il mittente che sara' il primo membro nonche' fondatore del gruppo */
      list_nick_el* new_user_group = malloc(sizeof(list_nick_el));
      strcpy(new_user_group -> nickname, message_read -> hdr.sender);
      DL_APPEND(new_group -> groupMembers, new_user_group);

      LockAndWriteListUsers();
      /* scorro listUsers */
      DL_SEARCH(listUsers, str_curr_el, new_group, string_el_compare_nick);
      if(str_curr_el == NULL)
      {
        /* aggiungo il nuovo gruppo con il mittente fondatore in groupMembers */  
        DL_APPEND(listUsers, new_group);
      }
      else
      {
        errExit("CREATEGROUP_OP");
      }
      UnlockListUsers();
      char* new_group_name_key = malloc((MAX_NAME_LENGTH + 1) * sizeof(char));
      strcpy(new_group_name_key, message_read -> data.hdr.receiver);
      /* inserisco il gruppo nella hashtable */
      if((icl_hash_insert(usersTable, new_group_name_key, gruppo)) == NULL)
        errExit("icl_hash_insert");
      printf("[FD: %4d]Success, gruppo %s nuovo, inserito con successo nella HashTable\n", 
            *cfd, message_read -> data.hdr.receiver);
      risposta_hdr -> op = OP_OK;
    }
    if(*bucketSet != bucketSet_gruppo)
    {
      UnlockHashTable(bucketSet_gruppo);
    }
    else
    {
      UnlockHashTable(*bucketSet);
    }

    /*-----------------------------------------------------------------------------------*/
    /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
    /*-----------------------------------------------------------------------------------*/

    LockHashTable(*bucketSet);
  }
  if(sendHdr(*cfd, risposta_hdr) == -1)
    errExit("sendHdr POSTTXT_OP");
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

}

/**
 * @function addgroupFun
 * @brief aggiunge il mittente al gruppo
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */
static void addgroupFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */
  printf("[FD: %4d]OP: ADDGROUP    Sender: %s\n", *cfd, message_read -> hdr.sender);
  User* utente = NULL;
  if(*userExists == 0)
  {
    LockHashTable(*bucketSet);
    /* l'utente puo' aver fatto la deregistrazione e aver chiesto un'altra
       operazione nella stessa sessione */
    risposta_hdr -> op = OP_FAIL;
    printf("[FD: %4d]Error, l'utente %s non risulta piu' registrato\n", *cfd, message_read -> hdr.sender);
  }
  else
  {
    /* cerco il mittente nella hashtable */
    utente = icl_hash_find(usersTable, message_read -> hdr.sender);
    assert(utente != NULL);
    assert(utente -> isAGroup == FALSE);

    list_string_el* str_curr_el = NULL;
    list_string_el str_tmp_el;
    strcpy(str_tmp_el.nickname, message_read -> data.hdr.receiver);
    /* cerco il gruppo nella listUsers */
    LockAndWriteListUsers();
    /* scorro listUsers */
    DL_SEARCH(listUsers, str_curr_el, &str_tmp_el, string_el_compare_nick);
    if(str_curr_el != NULL)
    {
      /* esiste il nome nella lista, controllo se e' un gruppo */
      assert(strcmp(str_curr_el -> nickname, message_read -> data.hdr.receiver) == 0);
      if(str_curr_el -> groupMembers == NULL)
      {
        /* non e' un gruppo, bensi' un utente */
        printf("[FD: %4d]Error, non e' stato possibile aggiungersi al gruppo perche'\n"
              "         il nome %s e' in realta' un utente gia' registrato\n", *cfd, message_read -> data.hdr.receiver);
        risposta_hdr -> op = OP_FAIL;
        LockStats();
        updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
        UnlockStats();
      }
      else
      {
        /* e' un gruppo, controllo se ne faccio parte */
        list_nick_el* nick_curr_el = NULL;
        list_nick_el nick_tmp_el;
        strcpy(nick_tmp_el.nickname, message_read -> hdr.sender);
        /* scorro i membri del gruppo */
        DL_SEARCH(str_curr_el -> groupMembers, nick_curr_el, &nick_tmp_el, nick_el_compare_nick);
        if(nick_curr_el != NULL)
        {
          /* se sono gia' membro del gruppo, non posso aggiungermi */
          printf("[FD: %4d]Error, non e' stato possibile aggiungersi al gruppo perche'\n"
                "         l'utente %s e' gia' membro del gruppo %s\n", 
                *cfd, message_read -> hdr.sender, message_read -> data.hdr.receiver);
          risposta_hdr -> op = OP_FAIL;
          LockStats();
          updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
          UnlockStats();
        }
        else
        {
          /* non sono ancora membro del gruppo, posso aggiungermi */
          printf("[FD: %4d]Success, aggiunta dell'utente %s al gruppo %s effettuata con successo\n",
                *cfd, message_read -> hdr.sender, message_read -> data.hdr.receiver);
          risposta_hdr -> op = OP_OK;
          /* aggiungo sempre in coda alla lista membri in modo che 
            il founder sia sempre in testa alla lista */
          list_nick_el* new_user_group = malloc(sizeof(list_nick_el));
          strcpy(new_user_group -> nickname, message_read -> hdr.sender);
          DL_APPEND(str_curr_el -> groupMembers, new_user_group);
        }
      }
    }
    else
    {
      /* non e' ne' un gruppo, ne' un utente */ 
      printf("[FD: %4d]Error, non e' stato possibile aggiungersi al gruppo perche'\n"
      "          il gruppo con nome %s e' inesistente\n", *cfd, message_read -> data.hdr.receiver);
      risposta_hdr -> op = OP_NICK_UNKNOWN;
      LockStats();
      updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
      UnlockStats();
    }
    UnlockListUsers();

    UnlockHashTable(*bucketSet);
    /*-----------------------------------------------------------------------------------*/
    /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
    /*-----------------------------------------------------------------------------------*/

    LockHashTable(*bucketSet);
  }
  if(sendHdr(*cfd, risposta_hdr) == -1)
    errExit("sendHdr ADDGROUP_OP");
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

}

/**
 * @function delgroupFun
 * @brief rimuove il mittente dal gruppo
 * @param[in] funOpArgs struttura contenente variabili usate dalla funzione
 * @return nothing
 */
static void delgroupFun(funOpArgs_t funOpArgs)
{
  message_t* message_read = funOpArgs.message_read;     /* messaggio ricevuto dal client mittente */
  const int* cfd = funOpArgs.cfd;                       /* cfd file descriptor del client mittente */
  const int* userExists = funOpArgs.userExists;         /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
  const unsigned int* bucketSet = funOpArgs.bucketSet;  /* bucketSet che e' gia' lockato solo se l'utente esiste */
  message_hdr_t* risposta_hdr = funOpArgs.risposta_hdr; /* messaggio di risposta verso il destinatario */
  printf("[FD: %4d]OP: DELGROUP    Sender: %s\n", *cfd, message_read -> hdr.sender);
  User* utente = NULL;
  if(*userExists == 0)
  {
    LockHashTable(*bucketSet);
    /* l'utente puo' aver fatto la deregistrazione e aver chiesto un'altra
       operazione nella stessa sessione */
    risposta_hdr -> op = OP_FAIL;
    printf("[FD: %4d]Error, l'utente %s non risulta piu' registrato\n", *cfd, message_read -> hdr.sender);
  }
  else
  {
    /* cerco il mittente nella hashtable */
    utente = icl_hash_find(usersTable, message_read -> hdr.sender);
    assert(utente != NULL);
    assert(utente -> isAGroup == FALSE);

    unsigned int hashed_value_gruppo = usersTable->hash_function(message_read -> data.hdr.receiver);
    unsigned int bucketSet_gruppo = (hashed_value_gruppo % HASH_DIM) / HASH_BUCKETS_PER_LOCK;
    /* Se il gruppo appartiene allo stesso BucketSet, allora possiedo gia' il lock
      altrimenti prendo il lock dell'altro BucketSet */
    if(*bucketSet != bucketSet_gruppo)
    {
      //Non possiedo il lock del bucket del gruppo
      UnlockHashTable(*bucketSet);

      /*-----------------------------------------------------------------------------------*/
      /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
      /*-----------------------------------------------------------------------------------*/

      LockHashTable(bucketSet_gruppo);
    }
          
    /* qui ho il lock sul gruppo */
    /* cerco il gruppo nella hashtable */
    User* gruppo = icl_hash_find(usersTable, message_read -> data.hdr.receiver);
    if(gruppo != NULL)
    {
      /* esiste una chiave con il nome nella hashtable */
      if(gruppo -> isAGroup == TRUE)
      {
        /* e' un gruppo */
        list_string_el* str_curr_el = NULL;
        list_string_el str_tmp_el;
        strcpy(str_tmp_el.nickname, message_read -> data.hdr.receiver);
        LockAndWriteListUsers();
        /* devo cercare il gruppo nella listUsers */
        DL_SEARCH(listUsers, str_curr_el, &str_tmp_el, string_el_compare_nick);
        assert(str_curr_el != NULL);
        assert(strcmp(str_curr_el -> nickname, message_read -> data.hdr.receiver) == 0);
        /* Il primo elemento della lista sta in str_curr_el -> groupMembers ed e' il founder */
        if(strcmp(message_read -> hdr.sender, str_curr_el -> groupMembers -> nickname) == 0)
        {
          /* Il fondatore del gruppo ha implicitamente richiesto la cancellazione dell'intero gruppo */        
          /* Cerco la chiave del gruppo nella hashtable */
          User* gruppo = icl_hash_find(usersTable, message_read -> data.hdr.receiver);
          assert(gruppo != NULL);
          assert(gruppo -> isAGroup == TRUE);
          /* Rimuovo la chiave del gruppo dalla hashtable */
          if(icl_hash_delete(usersTable, message_read -> data.hdr.receiver, free_hashtable_key, free_hashtable_data) == -1)
            errExit("icl_hash_delete");
          printf("[FD: %4d]Success, rimozione dell'intero gruppo con nome %s da parte del fondatore %s\n",
                *cfd, message_read -> data.hdr.receiver, message_read -> hdr.sender);
          list_nick_el* nick_curr_el = NULL;
          list_nick_el* nick_dummy_el = NULL;
          /* scorro la lista dei membri del gruppo togliendo
            tutti i membri di tale gruppo */
          DL_FOREACH_SAFE(str_curr_el -> groupMembers, nick_curr_el, nick_dummy_el)
          {
            DL_DELETE(str_curr_el -> groupMembers, nick_curr_el);
            free(nick_curr_el);
          }
          /* Devo eliminare il gruppo anche da listUsers */
          DL_DELETE(listUsers, str_curr_el);             
          free(str_curr_el);
          risposta_hdr -> op = OP_OK;
        }
        else
        {
          /* e' un gruppo, non sono il fondatore, controllo se ci sono gia' dentro o meno */
          list_nick_el* nick_curr_el = NULL;
          list_nick_el nick_tmp_el;
          strcpy(nick_tmp_el.nickname, message_read -> hdr.sender);
          /* devo cercare l'utente nella lista dei membri del gruppo */
          DL_SEARCH(str_curr_el -> groupMembers, nick_curr_el, &nick_tmp_el, nick_el_compare_nick);
          if(nick_curr_el != NULL)
          {
            /* Sono membro del gruppo, posso togliermi */
            printf("[FD: %4d]Success, rimozione dell'utente %s dal gruppo %s effettuata con successo\n",
                  *cfd, message_read -> hdr.sender, message_read -> data.hdr.receiver);
            risposta_hdr -> op = OP_OK;
            DL_DELETE(str_curr_el -> groupMembers, nick_curr_el);
            free(nick_curr_el -> nickname);
          }
          else
          {
            /* Non sono membro del gruppo, non posso togliermi */
            printf("[FD: %4d]Error, non e' stato possibile togliersi dal gruppo perche'\n"
                  "         l'utente %s non e' membro del gruppo %s\n", 
                  *cfd, message_read -> hdr.sender, message_read -> data.hdr.receiver);
            risposta_hdr -> op = OP_NICK_UNKNOWN;
            LockStats();
            updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
            UnlockStats();
          }
        }
        UnlockListUsers();
      }
      else
      {
        /* Non e' un gruppo, bensi' un utente */
        printf("[FD: %4d]Error, non e' stato possibile togliersi dal gruppo perche'\n"
              "il nome %s e' in realta' un utente gia' registrato\n", *cfd, message_read -> data.hdr.receiver);
        risposta_hdr -> op = OP_FAIL;
        LockStats();
        updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
        UnlockStats();
      }
    }
    else
    {
      /* Non esiste una chiave nella hashtable e quindi non esiste il gruppo */
      printf("[FD: %4d]Error, non e' stato possibile togliersi dal gruppo perche'\n"
      "          il gruppo con nome %s e' inesistente\n", *cfd, message_read -> data.hdr.receiver);
      risposta_hdr -> op = OP_FAIL;
      LockStats();
      updateStats(0, 0, 0, 0, 0, 0, 1); /* aggiorno la stat relativa agli errori */
      UnlockStats();
    }

    if(*bucketSet != bucketSet_gruppo)
    {
      UnlockHashTable(bucketSet_gruppo);
    }
    else
    {
      UnlockHashTable(*bucketSet);
    }

    /*-----------------------------------------------------------------------------------*/
    /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
    /*-----------------------------------------------------------------------------------*/

    LockHashTable(*bucketSet);
  }
  if(sendHdr(*cfd, risposta_hdr) == -1)
    errExit("sendHdr DELGROUP_OP");
  UnlockHashTable(*bucketSet);

  /*-----------------------------------------------------------------------------------*/
  /* In questo istante un altro thread potrebbe prendere il lock e mandare un TXT/FILE */
  /*-----------------------------------------------------------------------------------*/

}

/**
 * @function threadFunc
 * @brief gestisce una connessione verso il client servendo le operazioni richieste
 * @param[in] arg file descriptor su cui lavorera' il thread
 * @return nothing
 */
void* threadFunc(void* arg)
{
  int cfd = *((int*)arg);         /* File descriptor su cui lavorera' il thread */
  assert(cfd >= 0);     
  int ret_read;                   /* valore ritornato dalla read */
  message_t *message_read = NULL; /* messaggio letto */

  list_string_el* str_curr_el = NULL;
  int userExists = 0;
  char* nick = malloc((MAX_NAME_LENGTH + 1) * sizeof(char));
  LockAndReadListUsers();
  /* cerco il file descriptor del mittente nella lista degli utenti registrati
     e grazie ad esso risalgo al nickname del mittente stesso */
  DL_SEARCH_SCALAR(listUsers, str_curr_el , fd, cfd);
  if (str_curr_el != NULL)
  {
    if(str_curr_el -> groupMembers == NULL)
    {
      /* l'utente risulta registrato */
      userExists = 1;
      strcpy(nick, str_curr_el -> nickname);
    }
  }
  UnlockListUsers();

  /* Se l'utente non e' registrato, non c'e' bisogno di prendere alcun lock 
     prima di leggere la richiesta,
     altrimenti il thread cerca di prendere il lock ancor prima di leggere la richiesta */
  unsigned int hashed_value = -1;
  unsigned int bucketSet = -1;
  if(userExists == 1)
  {
    hashed_value = usersTable->hash_function(nick);
    bucketSet = (hashed_value % HASH_DIM) / HASH_BUCKETS_PER_LOCK;
    LockHashTable(bucketSet);
  }
  free(nick);

  /* Alloco spazio per il message_t che andro' a leggere */
  message_read = malloc(sizeof(message_t));
  if(!message_read)
    errExit("malloc message_read");
  memset(message_read, 0, sizeof(message_t));

  /* Alloco spazio per la risposta_hdr che mandero' */
  message_hdr_t* risposta_hdr = NULL;
  risposta_hdr = malloc(sizeof(message_hdr_t));
  if(!risposta_hdr)
    errExit("malloc");
  memset(risposta_hdr, 0, sizeof(message_hdr_t));

  /* Provo a fare una lettura di una richiesta */
  ret_read = readMsgServerSide(cfd, message_read);
  if(ret_read > 0)
  {
    /* la lettura e' andata a buon fine;
       ho letto anche il nickname del mittente e con questo posso
       calcolare il Bucket Set che mi servira' a fare il lock
       nel caso di nuove registrazioni o connessioni da parte
       dei client; */
    hashed_value = usersTable->hash_function(message_read -> hdr.sender);
    bucketSet = (hashed_value % HASH_DIM) / HASH_BUCKETS_PER_LOCK;

    /* struttura che verra' passata alle varie qualsiasi sia
       l'operazione richiesta dal client */
    funOpArgs_t funOpArgs;
    funOpArgs.message_read = message_read; /* messaggio ricevuto dal client mittente */
    funOpArgs.cfd = &cfd;                  /* cfd file descriptor del client mittente */
    funOpArgs.userExists = &userExists;    /* userExists uguale a 1 se l'utente esiste, 0 altrimenti */
    funOpArgs.bucketSet = &bucketSet;      /* bucketSet che e' gia' lockato solo se l'utente esiste */
    funOpArgs.risposta_hdr = risposta_hdr; /* messaggio di risposta verso il destinatario */

    switch(message_read -> hdr.op)
    {
      case REGISTER_OP : 
      {    
        registerFun(funOpArgs);
      }break;
      case CONNECT_OP :
      {
        connectFun(funOpArgs);
      }break;
      case UNREGISTER_OP :
      {
        unregisterFun(funOpArgs);
      }break;
      case DISCONNECT_OP :
      {
        disconnectFun(funOpArgs);
      }break;
      case USRLIST_OP :
      {
        usrlistFun(funOpArgs);
      }break;
      case POSTTXT_OP :
      {
        posttxtFun(funOpArgs);
      }break;
      case POSTTXTALL_OP :
      {
        posttxtallFun(funOpArgs);
      }break;
      case POSTFILE_OP :
      {
        postfileFun(funOpArgs);
      }break;
      case GETFILE_OP :
      {
        getfileFun(funOpArgs);
      }break;
      case GETPREVMSGS_OP :
      {
        getprevmsgsFun(funOpArgs);
      }break;
      case CREATEGROUP_OP :
      {
        creategroupFun(funOpArgs);
      }break;
      case ADDGROUP_OP :
      {
        addgroupFun(funOpArgs);
      }break;
      case DELGROUP_OP :
      {
        delgroupFun(funOpArgs);
      }break;
      default :
      {
        errExit("OP sconosciuta");
      }break;
    }

    /* rimetto il file descriptor del client nel set cosi' da poter
       essere ri-monitorato dalla select */
    LockFdSet();
    FD_SET(cfd, &master_read_fd);
    UnlockFdSet();
  }
  else if(ret_read == 0)
  {
    /* il client ha chiuso la connessione */
    printf("[FD: %4d]Success, il client ha chiuso la connessione [READ = 0]\n", cfd);
    
    /* decremento il numero di client connessi */
		LockNumOnline();
		numClientConnessi--;
		assert(numClientConnessi >= 0);
		UnlockNumOnline();

    list_string_el* str_curr_el = NULL;
    int trovato = 0;
    LockAndWriteListUsers();
    /* cerco l'utente in listUsers */
    DL_SEARCH_SCALAR(listUsers, str_curr_el , fd, cfd);
    if (str_curr_el != NULL)
    {
      /* se l'utente esiste, dico che e' offline */
      trovato = 1;
      str_curr_el -> online = FALSE;
      str_curr_el -> fd = -1;
    }
    UnlockListUsers();
    if(trovato == 1)
    {
      LockStats();
      updateStats(0, -1, 0, 0, 0, 0, 0); /* aggiorno la stat relativa agli utenti connessi */
      UnlockStats();            
    }
    /* riciclo il file descriptor */
    close(cfd);

    if(userExists == 1) /* se avevamo lockato la hashtable.. */
      UnlockHashTable(bucketSet);
  }
  else  /* ret_read < 0 */
  {
    if(errno == ECONNRESET)
    {
      /* il client ha chiuso la connessione */
      printf("[FD: %4d]Error, il client ha chiuso la connessione [READ = -1 & ECONNRESET]\n", cfd);

      /* decremento il numero di client connessi */
			LockNumOnline();
			numClientConnessi--;
			assert(numClientConnessi >= 0);
			UnlockNumOnline();

      list_string_el* str_curr_el = NULL;
      int trovato = 0;
      LockAndWriteListUsers();
      /* cerco l'utente in listUsers */
      DL_SEARCH_SCALAR(listUsers, str_curr_el , fd, cfd);
      if (str_curr_el != NULL)
      {
        /* se l'utente esiste, dico che e' offline */
        trovato = 1;
        str_curr_el -> online = FALSE;
        str_curr_el -> fd = -1;
      }
      UnlockListUsers();
      if(trovato == 1)
      {
        LockStats();
        updateStats(0, -1, 0, 0, 0, 0, 0); /* aggiorno la stat relativa agli utenti connessi */
        UnlockStats();            
      }

      /* riciclo il file descriptor */
      close(cfd);

      if(userExists == 1) /* se avevamo lockato la hashtable.. */
        UnlockHashTable(bucketSet);
      }
      else
      {
        errExit("readMsgServerSide");
      }
  }
  free(message_read);
  free(risposta_hdr);

  return NULL;
}

/**
 * @function main
 */
int main(int argc, char *argv[]) 
{
  /* Creazione della pipe prima dell'istallazione del Signal Handler */
  if(pipe(pipe_trick_fd) == -1)
    errExit("pipe");
  int flags;

  flags = fcntl(pipe_trick_fd[0], F_GETFL);
  if (flags == -1)
    errExit("fcntl-get");
  flags |= O_NONBLOCK; /* read-end non bloccante */
  if (fcntl(pipe_trick_fd[0], F_SETFL, flags) == -1)
    errExit("fcntl-set");

  flags = fcntl(pipe_trick_fd[1], F_GETFL);
  if (flags == -1)
    errExit("fcntl-get");
  flags |= O_NONBLOCK; /* write-end non bloccante */
  if (fcntl(pipe_trick_fd[1], F_SETFL, flags) == -1)
    errExit("fcntl-set");

  /* Installazione Signal Handler */
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sa.sa_handler = handler;

  if(sigaction(SIGINT, &sa, NULL) == -1)
    errExit("Sigaction SIGINT");
  if(sigaction(SIGQUIT, &sa, NULL) == -1)
    errExit("Sigaction SIGQUIT");
  if(sigaction(SIGTERM, &sa, NULL) == -1)
    errExit("Sigaction SIGTERM");
  if(sigaction(SIGUSR1, &sa, NULL) == -1)
    errExit("Sigaction SIGUSR1");

  struct sigaction s_pipe;
  memset(&s_pipe, 0, sizeof(s_pipe));    
  s_pipe.sa_handler = SIG_IGN; /* ignoro SIGPIPE per poterlo gestire */

  if(sigaction(SIGPIPE, &s_pipe, NULL) == -1) 
    errExit("Sigaction SIGPIPE");

  /* Controllo se da linea di comando abbia passato un file di config. valido */
  if(argc != 3)
    usage(argv[0]);
  int opt;
  char* confPath = NULL;
  while((opt = getopt(argc, argv, ":f:")) != -1)
  {
    switch(opt)
    {
      case 'f' :  confPath = optarg;  break;
      default  :  fatal("Opzione inesistente");
    }
  }
  if(confPath == NULL) /* se argomento di -f non e' stato inserito */
    usage(argv[0]);

  /* Il parsing del file restituisce una struttura che ha memorizzati tutti i campi che trova
    e se non ne trova alcuni li lascia a NULL o zero */
  
  /* la chiusura di chattyConfigFc viene fatta nella funzione parseConfigurationFile */
  FILE* chattyConfigFd;
  if((chattyConfigFd = fopen(confPath, "r")) == NULL)
    errExit("Apertura file di configurazione del server(inserito: %s)", confPath);
  
  config_t *config = NULL;
  config = parseConfigurationFile(chattyConfigFd);

  if(config == NULL)
    errExit("La funzione parseConfigurationFile ha ritornato NULL");
  
  /* Predispongo il path del socket per il Signal Handler */
  size_t size_socket_path = strlen(config -> unixPath) + 1;
  socket_path = malloc(size_socket_path * sizeof(char));
  if(!socket_path)
    errExit("malloc");

  /* setto le variabili globali che sono usate in sola lettura */
  strcpy(socket_path, config -> unixPath);
  maxHistMsgs = config -> maxHistMsgs;
  maxMsgSize = config -> maxMsgSize;
  maxFileSize = config -> maxFileSize;
  dirName = config -> dirName;

  assert(maxHistMsgs != -1);

  printf("[--MAIN--]: UnixPath: %s\n", config -> unixPath);
  printf("[--MAIN--]: MaxConnections: %d\n", config -> maxConnections);
  printf("[--MAIN--]: ThreadsInPool: %d\n", config -> threadsInPool);
  printf("[--MAIN--]: MaxMsgSize: %d\n", config -> maxMsgSize);
  printf("[--MAIN--]: MaxFileSize: %d\n", config -> maxFileSize);
  printf("[--MAIN--]: MaxHistMsgs: %d\n", config -> maxHistMsgs);
  printf("[--MAIN--]: DirName: %s\n", config -> dirName);
  printf("[--MAIN--]: StatFileName: %s\n", config -> statFileName);

  /* creo la Hash Table utenti-gruppi */
  usersTable = icl_hash_create(HASH_DIM, NULL, NULL);

  /* creazione Thread Pool */
  pthread_t* th;
  threadArgs_t* thARGS;
  int num_threads = config -> threadsInPool;

  /* istanzio l'array di threads */
  th = malloc(num_threads * sizeof(pthread_t));
  if(!th)
    errExit("malloc");
  
  /* istanzio gli argomenti di ciascun thread */
  thARGS = malloc(num_threads * sizeof(threadArgs_t));
  if(!thARGS)
    errExit("malloc");
  
  /* istanzio la coda dei descrittori */
  fdQueue = initQueue();
  if(!fdQueue)
    errExit("initQueue");
  
  /* istanzio gli argomenti dei threads del Pool */
  int minus_one = -1;
  for(int i = 0; i < num_threads; i++)
  {
    thARGS[i].thid = i;
    thARGS[i].descrQueue = fdQueue;
    thARGS[i].fun = threadFunc;
    thARGS[i].EOS = &minus_one; 
  } 

  /* istanzio i threads del Pool */
  int s;
  printf("[--MAIN--]: Spawn");
  for(int i = 0; i < num_threads; i++)
  {
    s = pthread_create(&th[i], NULL, worker, &thARGS[i]);
    if(s != 0)
      errExitEN(s, "pthread_create");
    printf(" Thread %d", thARGS[i].thid);
  }
  printf(" ... OK \n");

  int sock_listener_fd;
  struct sockaddr_un addr;

  sock_listener_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if(sock_listener_fd == -1)
    errExit("socket");

  if(fcntl(sock_listener_fd, F_SETFL, O_NONBLOCK) == -1)
    errExit("fcntl");

  memset(&addr, '0', sizeof(struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, config -> unixPath, UNIX_PATH_MAX - 1);

  unlink(config -> unixPath);
  if(bind(sock_listener_fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_un)) == -1)
    errExit("bind");    

  int maxConnections = config -> maxConnections;

  if(listen(sock_listener_fd, MAXBACKLOG) == -1)
    errExit("listen");

  char ch;                /* per leggere il byte fittizio del Signal Handler */
  struct timeval timeout;
  int r_select;           /* valore di ritorno della select */
  fd_set readfds;         /* set di descrittori pronti in lettura */
  int max_fd; 
  max_fd = max(pipe_trick_fd[0], sock_listener_fd);

  FD_ZERO(&master_read_fd);
  FD_ZERO(&readfds);
  FD_SET(pipe_trick_fd[0], &master_read_fd);
  FD_SET(sock_listener_fd, &master_read_fd);
  
  /* Preparo il file delle statistiche in caso di USR1 sovrascrivendo il vecchio contenuto */
  FILE* fileStats;
  if( (fileStats = fopen(config -> statFileName, "w+")) == NULL)
    errExit("fopen");

  /* Messaggio di risposta che dovro' mandare a ciascun client che cerchera' di superare maxConnections */
  message_hdr_t* risposta_hdr = NULL;
  risposta_hdr = malloc(sizeof(message_hdr_t));
  if(!risposta_hdr)
    errExit("malloc");
  memset(risposta_hdr, 0, sizeof(message_hdr_t));
  risposta_hdr -> op = OP_FAIL;

  while(1)
  {
    LockFdSet();
    readfds = master_read_fd; /* Copio il set di file descriptor */
    UnlockFdSet();

    timeout.tv_sec = 0;
    timeout.tv_usec = 10000; /* 10 millisecondi = 10000 microsecondi */
    while( ((r_select = select(max_fd + 1, &readfds, NULL, NULL, &timeout)) == -1) && errno == EINTR)
    {
      printf("\n[--MAIN--]EINTR select\n");
      continue;   /* restarta se interrotto da segnale */
    }
    if(r_select < 0)
    {
      errExit("select");
    }
    else if(r_select == 0)
    {
      /* Tempo scaduto per eseguire la select
         I set ritornati sono tutti uguali a NULL */
    }
    else /* r_select > 0 */
    {
      /* ho almeno un descrittore pronto */
      if(FD_ISSET(pipe_trick_fd[0], &readfds))
      {
        /* e' stato chiamato il Signal Handler */
        printf("\n[--MAIN--]Segnale catturato\n");
        for(;;)
        {
          if(read(pipe_trick_fd[0], &ch, 1) == -1)
          {
            if(errno == EAGAIN)
            {
              break; /* finiti i bytes della pipe */
            }
            else
            {
              errExit("read dalla pipe!");
            }                        
          }       
        }
        /* Qui posso gestire il segnale */
        if(ch == '0')
        {
          /* Ho ricevuto un segnale di interruzione
             informo i thread worker di finire l'ultima
             operazione in corso */
          for(int i = 0; i < num_threads; i++)
          {
            int* new_fd = malloc(sizeof(int));
            *new_fd = -1;
            if(push(fdQueue, new_fd) == -1)
              errExit("push");
          }
          printf("[--MAIN--]Ho detto a tutti i thread di finire l'ultimo lavoro in corso\n");
          for(int i = 0; i < num_threads; i++)
          {
            printf("[--MAIN--]Join thread %d in corso ...", thARGS[i].thid);
            pthread_join(th[i], NULL);
            printf("joinato con successo\n");
          }
          goto cleanup;
        }
        else if(ch == '1')
        {
          /* Ho ricevuto un segnale definito dall'utente SIGUSR1
             stampo le statistiche su file */
          printf("[--MAIN--]Segnale di SIGUSR1, stampo le statistiche su file\n");
          printStats(fileStats);
        }
        FD_CLR(pipe_trick_fd[0], &readfds);
      }
      if(FD_ISSET(sock_listener_fd, &readfds))
      {
        /* e' stata ricevuta una richiesta di connessione sul sock_listener_fd,
				   accetto tutte le connessioni in entrata */
				int cfd = -1;
				do
				{
					cfd = accept(sock_listener_fd, NULL, NULL);
					if(cfd >= 0 && cfd <= FD_SETSIZE)
					{
            LockNumOnline();
            /* se supero il numero massimo di client gestiti concorrentemente */
            if(numClientConnessi >= maxConnections)
            {
              /* manda un messaggio di errore al client e poi chiudi la connessione */
              UnlockNumOnline();
              if(sendHdr(cfd, risposta_hdr) == -1)
                errExit("sendHdr SELECT");
              close(cfd);
            }
            else
            {
              /* non ho ancora raggiunto il numero massimo di client gestiti concorrentemente */
              numClientConnessi++;
              assert(numClientConnessi > 0);   
              UnlockNumOnline();
              //printf("[--MAIN--]Accept FD = %d\n", cfd);
						  /* aggiorno max_fd */
              if(cfd > max_fd)
                max_fd = cfd;
              /* metto il file descriptor in coda */
              int* new_fd = malloc(sizeof(int));
              *new_fd = cfd;
              if(push(fdQueue, new_fd) == -1)
                errExit("push");
            }
					}
					else if(cfd < 0)
					{
						UnlockNumOnline();
						if(errno == EAGAIN || errno == EWOULDBLOCK)
						{
              /* ho fatto tutte le accept possibili per ora */
							//printf("[--MAIN--]Ho finito di fare tutte le accept possibili\n");
							break;
						}
						else
						{
							errExit("Select");
						}
					} 
					else
					{
						errExit("Select");
					}
				}while(cfd != -1);
      }
      for(int i = 0; i < max_fd + 1; i++)
      {
        if(i == sock_listener_fd || i == pipe_trick_fd[0])
        {
          continue;   /* ho gia' lavorato sopra con tali file descriptor */
        }
        if(FD_ISSET(i, &readfds))
        {
          //printf("[--MAIN--]File descriptor %d in readfds\n", i);
          LockFdSet();
          FD_CLR(i, &master_read_fd);
          UnlockFdSet();
          /* metto il file descriptor di nuovo in coda */
          int* new_fd = malloc(sizeof(int));
          *new_fd = i;
          if(push(fdQueue, new_fd) == -1)
            errExit("push");
        }
      }
    }
  }

/* etichetta di goto in caso di SIGINT, SIGQUIT oppure SIGTERM */
cleanup:

  free(risposta_hdr);

  /* chiudo il file delle statistiche */
  if(fclose(fileStats) != 0)
    errExit("close");

  list_string_el* list_curr_el = NULL;
  list_string_el* list_dummy_el = NULL;
  /* scorro la listUsers */
  DL_FOREACH_SAFE(listUsers, list_curr_el , list_dummy_el)
  {
    /* per ogni gruppo libero la lista dei nickname che vi appartengono */
    list_nick_el* nick_curr_el = NULL;
    list_nick_el* nick_dummy_el = NULL;
    /* scorro la lista dei nickname di un gruppo */
    DL_FOREACH_SAFE(list_curr_el -> groupMembers, nick_curr_el, nick_dummy_el)
    {
      free(nick_curr_el);
    }
    DL_DELETE(listUsers, list_curr_el);
    free(list_curr_el); 
  }

  free(config -> unixPath);
  free(config -> dirName);
  free(config -> statFileName);
  free(config);
  free(socket_path);
  free(th);
  free(thARGS);

  /* elimino la coda dei descrittori */
  deleteQueue(fdQueue);
  /* elimino la hashtable */
  icl_hash_destroy(usersTable, free_hashtable_key, free_hashtable_data);
  fflush(stdout);
  fflush(stdin);
  return 0;
  
}
