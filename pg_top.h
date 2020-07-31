/*
 *	Top - a top users display for Berkeley Unix
 *
 *	General (global) definitions
 *
 *	Copyright (c) 2007-2019, Mark Wong
 */

#ifndef _PG_TOP_H_
#define _PG_TOP_H_

#include "machine.h"
#include "os.h"

/* Log base 2 of 1024 is 10 (2^10 == 1024) */
#define LOG1024		10

/* Special atoi routine returns either a non-negative number or one of: */
#define Infinity	-1
#define Invalid		-2

/* maximum number we can have */
#define Largest		0x7fffffff

struct ext_decl
{
	int			(*f_minibar) (char *, int);
	int			(*f_display) (char *, int);
};

/*
 *	Definitions for things that might vary between installations.
 */

/*
 *	"Table_size" defines the size of the hash tables used to map uid to
 *	username.  Things will work best if the number is a prime number.
 *	We use a number that should be suitable for most installations.
 */
#ifndef Table_size
#define Table_size	8191
#endif

/*
 *	"Nominal_TOPN" is used as the default TOPN when Default_TOPN is Infinity
 *	and the output is a dumb terminal.	If we didn't do this, then
 *	installations who use a default TOPN of Infinity will get every
 *	process in the system when running top on a dumb terminal (or redirected
 *	to a file).  Note that Nominal_TOPN is a default:  it can still be
 *	overridden on the command line, even with the value "infinity".
 */
#ifndef Nominal_TOPN
#define Nominal_TOPN	40
#endif

#ifndef Default_DELAY
#define Default_DELAY	5
#endif

/*
 *	If the local system's getpwnam interface uses random access to retrieve
 *	a record (i.e.: 4.3 systems, Sun "yellow pages"), then defining
 *	RANDOM_PW will take advantage of that fact.  If RANDOM_PW is defined,
 *	then getpwnam is used and the result is cached.  If not, then getpwent
 *	is used to read and cache the password entries sequentially until the
 *	desired one is found.
 *
 *	We initially set RANDOM_PW to something which is controllable by the
 *	Configure script.  Then if its value is 0, we undef it.
 */

#define RANDOM_PW	1
#if RANDOM_PW == 0
#undef RANDOM_PW
#endif

enum pgparams
{
	PG_HOST,
	PG_PORT,
	PG_USER,
	PG_PASSWORD,
	PG_DBNAME
};

struct pg_top_context
{
#ifdef ENABLE_COLOR
	int			color_on;
#endif
	int			delay;
	int			displays;
	void		(*d_header) (char *);
	char		do_unames;
	char		dostates;
	char	   *header_options[2][MODE_TYPES];
	char	   *header_text;
	char		interactive;
	int			mode;
	int			mode_remote;	/* Mode for monitoring a remote database
								 * system. */
	int			order_index;
	char	   *order_name;
	struct process_select ps;
	char		show_tags;
	struct statics statics;
	struct system_info system_info;
	struct timeval timeout;
	int			topn;
	struct pg_conninfo_ctx conninfo;
};

void		quit(int);
void		reset_display(struct pg_top_context *);

#endif							/* _PG_TOP_H_ */
