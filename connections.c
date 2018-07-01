#define _POSIX_C_SOURCE 200809L /**<feature test macro */

/**
 * @file connections.c
 * @author Andrea Tosti 518111
 *
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore\n
 * NOTA: file fornito in kit_chatty e modificato opportunamente
 * @brief Funzioni per gestire le connessioni
 */

#include <connections.h>

int maxFileSize; 

int openConnection(char* path, unsigned int ntimes, unsigned int secs)
{   
  int sfd;
  struct sockaddr_un addr;

  sfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if(sfd == -1)
    return -1;
  
  memset(&addr, '0', sizeof(struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  while(ntimes > 0)
  {
    if(connect(sfd, (struct sockaddr*)&addr, sizeof(struct sockaddr_un)) == -1)
    {
      printf("(---client---) Connessione fallita\n");
      ntimes--;
      sleep(secs);
    }
    else
    {
      return sfd;
    }
  }
  return -1;
}

int readHeader(long connfd, message_hdr_t *hdr)
{
  int ris;
  if( (ris = readn(connfd, hdr, sizeof(message_hdr_t))) <= 0)
    return ris;
  return 1;
}

int readData(long fd, message_data_t *data)
{
  int ris;
  if( (ris = readn(fd, &(data -> hdr), sizeof(message_data_hdr_t))) <= 0)
    return ris;

  if(data -> hdr.len > 0)
  {
    data -> buf = malloc((data -> hdr.len) * sizeof(char));
    if(!data -> buf)
    {
      perror("malloc");
      return -1;
    }
    memset(data -> buf, 0, (data -> hdr.len) * sizeof(char));
    if( (ris = readn(fd, data -> buf, (data -> hdr.len) * sizeof(char))) <= 0)
    {
      free(data -> buf);
      return ris;
    }
  }
  else
  {
    return -1;
  }
  return 1;
}

int readDataServerSide(long fd, char* newFileName)
{
  int ris;
  message_data_t* data = NULL;
  data = malloc(sizeof(message_data_t));
  if(!data)
  {
    perror("malloc");
    return -1;
  }
  memset(data, 0, sizeof(message_data_t));

  if( (ris = readn(fd, &(data -> hdr), sizeof(message_data_hdr_t))) <= 0)
  {
    free(data);
    return ris;
  }

  assert(maxFileSize != -1);

  /* Memorizzo il file */
  int outputFd, openFlags;
  mode_t filePerms;
  openFlags = O_CREAT | O_WRONLY | O_TRUNC;
  filePerms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH | S_IXUSR;

  /* faccio la stessa cosa che farebbe mkdir, quindi creo
     tutte le sottocartelle necessarie per memorizzare il file */
  if(mkpath(newFileName, filePerms) == -1)
  {
    perror("mkpath");
    return -1;
  }

  /* apro il file descriptor per il nuovo file */
  outputFd = open(newFileName, openFlags, filePerms);
  if(outputFd == -1)
  {
    close(outputFd);
    perror("open");	
    return -1;	
  }

  data -> buf = malloc((data -> hdr.len) * sizeof(char));
  if(!data -> buf)
  {
    perror("malloc");
    return -1;
  }
  memset(data -> buf, 0, (data -> hdr.len) * sizeof(char));

  /* memorizzo il file sul buffer */
  if( (ris = readn(fd, data -> buf, (data -> hdr.len) * sizeof(char))) <= 0)
  {
    free(data -> buf);
    free(data);
    return ris;
  }
  /* se supero la massima dimensione di un file specificata
     nel file di configurazione, in kylobytes, ritorno un errore */
  if(data -> hdr.len > maxFileSize*1000)
  {
    /* scarto il file appena ricevuto */
    free(data -> buf);
    free(data);
    return -2;
  }
  else
  {
    /* se la dimensione del file rientra nei limiti, allora
       memorizzo il buffer su file che ha come file
       descriptor outputFd */
    if( (ris = writen(outputFd, data -> buf, data -> hdr.len)) <= 0)
      return ris;

    free(data -> buf);
    free(data);

    if (close(outputFd) == -1)
    {
      perror("close");
      return -1;
    }
  }

  return 1;
}

int readMsg(long fd, message_t *msg)
{
  int ris;
  if( (ris = readn(fd, &(msg -> hdr), sizeof(message_hdr_t))) <= 0)
    return ris;
  if( (ris = readn(fd, &(msg -> data.hdr), sizeof(message_data_hdr_t))) <= 0)
    return ris;
  if(msg -> data.hdr.len > 0)
  {
    msg -> data.buf = malloc((msg -> data.hdr.len) * sizeof(char));
    memset(msg -> data.buf, 0, (msg -> data.hdr.len) * sizeof(char));
    if( (ris = readn(fd, msg -> data.buf, (msg -> data.hdr.len) * sizeof(char))) <= 0)
      return ris;
  }
  return 1;
}

int readMsgServerSide(long fd, message_t *msg)
{
  int ris;
  if( (ris = readn(fd, &(msg -> hdr), sizeof(message_hdr_t))) <= 0)
    return ris;
  if( (ris = readn(fd, &(msg -> data.hdr), sizeof(message_data_hdr_t))) <= 0)
    return ris;
  if(msg -> data.hdr.len > 0)
  {
    msg -> data.buf = malloc((msg -> data.hdr.len) * sizeof(char));
    memset(msg -> data.buf, 0, (msg -> data.hdr.len) * sizeof(char));
    if( (ris = readn(fd, msg -> data.buf, (msg -> data.hdr.len) * sizeof(char))) <= 0)
    {
      free(msg -> data.buf);
      return ris;
    }
  }
  return 1;
}

int sendRequest(long fd, message_t *msg)
{
  int ris;
  if( (ris = writen(fd, &(msg -> hdr), sizeof(message_hdr_t))) <= 0)
    return ris;
  if( (ris = writen(fd, &(msg -> data.hdr), sizeof(message_data_hdr_t))) <= 0)
    return ris;		
  if(msg -> data.hdr.len > 0)
  {
    if( (ris = writen(fd, msg -> data.buf, (msg -> data.hdr.len) * sizeof(char))) <= 0)
      return ris;
  }
  return 1;
}

int sendData(long fd, message_data_t *msg)
{
  int ris;
  if( (ris = writen(fd, &(msg -> hdr), sizeof(message_data_hdr_t))) <= 0)
  {
    return ris;
  }
  if( (ris = writen(fd, msg -> buf, msg->hdr.len)) <= 0)
  {
    return ris;			
  }
  return 1;
}

int sendDataServerSide(long fd, message_data_t *msg)
{
  int ris;
  if( (ris = writen(fd, &(msg -> hdr), sizeof(message_data_hdr_t))) <= 0)
    return ris;
  if( (ris = writen(fd, msg -> buf, msg->hdr.len)) <= 0)
    return ris;
  return 1;
}

int sendHdr(long fd, message_hdr_t *msg)
{
  int ris;
  if( (ris = writen(fd, msg, sizeof(message_hdr_t))) <= 0)
    return ris;
  return 1;
}
