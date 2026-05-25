#pragma once

/* This file intended to serve as a drop-in replacement for 
 * unistd.h on Windows
 */

#include <stdlib.h>
#include <io.h>
#include <direct.h> /* for _getcwd() */
#include <BaseTsd.h>
#include <sys/stat.h>

/* Values for the second argument to access.
   These may be OR'd together.  */
#define R_OK    4       /* Test for read permission.  */
#define W_OK    2       /* Test for write permission.  */
#define F_OK    0       /* Test for existence.  */

#define access _access
#define fileno _fileno
/* read, write, and close are NOT being #defined here, because while there are file handle specific versions for Windows, they probably don't work for sockets. You need to look at your app and consider whether to call e.g. closesocket(). */

#define ssize_t SSIZE_T

#define STDIN_FILENO 0
