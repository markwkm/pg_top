/*
 *	Top - a top users display for Berkeley Unix
 *
 *	General (global) definitions
 */

#ifndef _PG_TOPP_H_
#define _PG_TOPP_H_

#include "machine.h"

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

struct pg_top_context
{
#ifdef ENABLE_COLOR
	int color_on;
#endif
	char conninfo[4096];
	struct db_info db_info;
	char dbname[1024];
	int dbport;
	char dbusername[1024];
	int delay;
	struct disk_info disk_info;
	int displays;
	char do_unames;
	char dostates;
	char *(*get_userid)(uid_t);
	char *header_text;
	char *header_processes;
	char interactive;
	struct io_info io_info;
	int mode;
	int order_index;
	char *order_name;
	char password[1001];
	struct process_select ps;
	char show_tags;
	char socket[1024];
	struct statics statics;
	struct system_info system_info;
	struct timeval timeout;
	int topn;

};

void		quit(int);
void		reset_display();

#endif   /* _PG_TOPP_H_ */
