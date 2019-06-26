/*-------------------------------------------------------------------------
 *
 * port.h
 *	  Header for src/port/ compatibility functions.
 *
 * Portions Copyright (c) 1996-2006, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/port.h,v 1.106.2.1 2007/01/11 02:40:12 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef _PORT_H_
#define _PORT_H_

#include <stdbool.h>

#if defined(WIN32) && !defined(__CYGWIN__)
#define DEVNULL "nul"
/* "con" does not work from the Msys 1.0.10 console (part of MinGW). */
#define DEVTTY	"con"
#else
#define DEVNULL "/dev/null"
#define DEVTTY "/dev/tty"
#endif

/* Portable prompt handling */
extern char *simple_prompt(const char *prompt, int maxlen, bool echo);

#endif   /* _PORT_H_ */
