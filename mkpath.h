/**
 * @file mkpath.h
 * @author Andrea Tosti 518111
 * 
 * Si dichiara che il contenuto di questo file e' in ogni sua parte opera originale dell'autore\n
 * NOTA: Il file e' stato scaricato dalla rete https://stackoverflow.com/a/9210960
 * @brief Header del file mkpath.c
 */

#ifndef MKPATH_H_
#define MKPATH_H_

#include <fcntl.h>		/* Bit modes for file access modes */
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <stdio.h>

int mkpath(char* file_path, mode_t mode);

#endif /* MKPATH_H_ */