/**
 * @file parser.c
 * @author Andrea Tosti 518111
 * 
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore
 * @brief Fa il parsing del file di configurazione del server
 */

#include <parser.h>

/**
 * @function parseConfigurationFile
 * @brief scansiona i parametri presenti nel file di configurazione del server
 *
 * salva i parametri trovati nel file di configurazione nei campi della\n
 * struttura di tipo config_t
 * @param[in,out] chattyConfigFd file descriptor del file di configurazione\n
 *                               aperto precedentemente in lettura
 * @return nothing (termina direttamente il programma)
 */
config_t* parseConfigurationFile(FILE* chattyConfigFd)
{
  config_t *config = malloc(sizeof(config_t));
  if(!config)
    errExit("malloc");
  config -> unixPath = NULL;
  config -> maxConnections = 0;
  config -> threadsInPool = 0;
  config -> maxMsgSize = 0;
  config -> maxFileSize = 0;
  config -> maxHistMsgs = 0;
  config -> dirName = NULL;
  config -> statFileName = NULL;

  char buf[BUFSIZE];
  char *p;

  while(fgets(buf, BUFSIZE, chattyConfigFd) != NULL)
  {
    p = buf;
    while(*p != '\0')
    {
      if(*p == 35) /* # */
      {
        /* Se e' un commento devo aspettare che arrivi allo \n (quando finisce il commento) */
        //printf("#");
        while(*p != 10 && *p != '\0')   p++;
      }
      else if(*p == 10) /* \n */
      {
        //printf("\n");
        break;
      }
      else if(*p == 32) /* Spazio */
      {
        //printf(" ");
      }
      else if(*p == 9) /* TAB Orizzontale */
      {
        //printf("\t");
      }
      else /* Probabilmente e' un carattere */
      {
        /* Scorro la righa fino a quando non incontro uno spazio o un TAB*/
        size_t length_of_string = 0;
        char* inizioStringa = NULL;
        while(*p != 32 && *p != 9 && *p != '\0')
        {
           p++;
           length_of_string++;
        }
        inizioStringa = (p-length_of_string);
        if((memcmp(inizioStringa, "UnixPath", strlen("UnixPath"))         == 0)
        || (memcmp(inizioStringa, "MaxConnections", strlen("MaxConnections"))   == 0)
        || (memcmp(inizioStringa, "ThreadsInPool", strlen("ThreadsInPool"))    == 0)
        || (memcmp(inizioStringa, "MaxMsgSize", strlen("MaxMsgSize"))       == 0)
        || (memcmp(inizioStringa, "MaxFileSize", strlen("MaxFileSize"))      == 0)
        || (memcmp(inizioStringa, "MaxHistMsgs", strlen("MaxHistMsgs"))      == 0)
        || (memcmp(inizioStringa, "DirName", strlen("DirName"))          == 0)
        || (memcmp(inizioStringa, "StatFileName", strlen("StatFileName"))     == 0))
        {
          int numero = -1;
          char* stringa = NULL;
          /* Ora bisogna trovare il simbolo = */
          while((*p == 32 || *p == 9) && *p != '\0') p++;
          if(*p == 61) /* 61 corrisponde al simbolo dell'uguale (=) */
          {
            p++;
            /* Posso andare avanti */
            while((*p == 32 || *p == 9) && *p != '\0') p++;
            /* ho trovato il valore da memorizzare */
            size_t len2 = 0;
            while(*p != 32 && *p != 9 && *p != 10 && *p != '\0')
            {
              p++;
              len2++;
            }
            if(isdigit(*(p-len2))) /* e' una cifra che va da 0 a 9 */
            {                     
              numero = atoi(p-len2);
            }
            else
            {
              stringa = malloc((len2 + 1) * sizeof(char));
              if(!stringa)
                errExit("malloc");
              memcpy(stringa, p-len2, len2);
              stringa[len2] = '\0';
            }
          }
          else
          {
            errExit("File di configurazione non valido, carattere atteso: = , e'"
                  " stato trovato %c (ASCII: %d) invece", *p, *p);
          }
          if(numero != -1) /* ho memorizzato un numero */
          {
            if(memcmp(inizioStringa, "ThreadsInPool", strlen("ThreadsInPool")) == 0)
              config -> threadsInPool = numero;
            else if(memcmp(inizioStringa, "MaxConnections", strlen("MaxConnections")) == 0)
              config -> maxConnections = numero;
            else if(memcmp(inizioStringa, "MaxMsgSize", strlen("MaxMsgSize")) == 0)
              config -> maxMsgSize = numero;
            else if(memcmp(inizioStringa, "MaxFileSize", strlen("MaxFileSize")) == 0)
              config -> maxFileSize = numero;
            else if(memcmp(inizioStringa, "MaxHistMsgs", strlen("MaxHistMsgs")) == 0)
              config -> maxHistMsgs = numero;
          }
          else /* ho memorizzato una stringa */
          {
            if(memcmp(inizioStringa, "UnixPath", strlen("UnixPath")) == 0)
              config -> unixPath = stringa;
            else if(memcmp(inizioStringa, "DirName", strlen("DirName")) == 0)
              config -> dirName = stringa;
            else if(memcmp(inizioStringa, "StatFileName", strlen("StatFileName")) == 0)
              config -> statFileName = stringa;
          }
        }
        else
        {
          errExit("File di configurazione non valido, opzione non attesa :\n%s", inizioStringa);
        }
        p--;
        break;
      }
      p++;
    }
  }
  if(fclose(chattyConfigFd) != 0)
    errExit("close");
  return config;
}