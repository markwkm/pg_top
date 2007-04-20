#include "config.h"

#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if STDC_HEADERS
#include <string.h>
#include <stdlib.h>
#define setbuffer(f, b, s)	setvbuf((f), (b), (b) ? _IOFBF : _IONBF, (s))
#define memzero(a, b)		memset((a), 0, (b))
#else /* !STDC_HEADERS */
#ifndef HAVE_STRCHR
#define strchr(a, b)		index((a), (b))
#define strrchr(a, b)		rindex((a), (b))
#endif /* HAVE_STRCHR */
#ifdef HAVE_MEMCPY
#define memzero(a, b)		memset((a), 0, (b))
#else
#define memcpy(a, b, c)		bcopy((b), (a), (c))
#define memzero(a, b)		bzero((a), (b))
#define memcmp(a, b, c)		bcmp((a), (b), (c))
#endif /* HAVE_MEMCPY */
#ifdef HAVE_STRINGS_H
#include <strings.h>
#else
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#endif
char *getenv();
caddr_t malloc();
#endif /* STDC_HEADERS */

/* we must have both sighold and sigrelse to use them */
#if defined(HAVE_SIGHOLD) && !defined(HAVE_SIGRELSE)
#undef HAVE_SIGHOLD
#endif

/* include unistd.h on Solaris, to get fewer
 * warrnigs if compiling with Sun's Studio compilers.
 * This should be included on anything that has it, but 
 * I don't have time to figure out if that will cause
 * other problems.  --wnl
 */
#if defined (__sun) && defined (__SVR4)
#include <unistd.h>
#endif
