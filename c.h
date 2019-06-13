/*-------------------------------------------------------------------------
 *
 * c.h
 *	  Fundamental C definitions.  This is included by every .c file in
 *	  PostgreSQL (via either postgres.h or postgres_fe.h, as appropriate).
 *
 *	  Note that the definitions here are not intended to be exposed to clients
 *	  of the frontend interface libraries --- so we don't worry much about
 *	  polluting the namespace with lots of stuff...
 *
 *
 * Portions Copyright (c) 1996-2006, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/c.h,v 1.214.2.1 2007/01/11 02:40:12 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
/*
 *----------------------------------------------------------------
 *	 TABLE OF CONTENTS
 *
 *		When adding stuff to this file, please try to put stuff
 *		into the relevant section, or add new sections as appropriate.
 *
 *	  section	description
 *	  -------	------------------------------------------------
 *		2)		bool, true, false, TRUE, FALSE, NULL
 *
 * NOTE: since this file is included by both frontend and backend modules, it's
 * almost certainly wrong to put an "extern" declaration here.	typedefs and
 * macros are the kind of thing that might go here.
 *
 *----------------------------------------------------------------
 */
#ifndef C_H
#define C_H

/*
 * We have to include stdlib.h here because it defines many of these macros
 * on some platforms, and we only want our definitions used if stdlib.h doesn't
 * have its own.  The same goes for stddef and stdarg if present.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define _(x) gettext((x))

#ifdef ENABLE_NLS
#include <libintl.h>
#else
#define gettext(x) (x)
#endif

/* ----------------------------------------------------------------
 *				Section 2:	bool, true, false, TRUE, FALSE, NULL
 * ----------------------------------------------------------------
 */

/*
 * bool
 *		Boolean value, either true or false.
 *
 * XXX for C++ compilers, we assume the compiler has a compatible
 * built-in definition of bool.
 */

#ifndef __cplusplus

#ifndef bool
typedef char bool;
#endif

#ifndef true
#define true	((bool) 1)
#endif

#ifndef false
#define false	((bool) 0)
#endif
#endif   /* not C++ */

typedef bool *BoolPtr;

#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

/* /port compatibility functions */
#include "port.h"

#endif   /* C_H */
