/*-------------------------------------------------------------------------
 *
 * sprompt.c
 *	  simple_prompt() routine
 *
 * Portions Copyright (c) 1996-2006, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/port/sprompt.c,v 1.18 2006/10/04 00:30:14 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */


/*
 * simple_prompt
 *
 * Generalized function especially intended for reading in usernames and
 * password interactively. Reads from /dev/tty or stdin/stderr.
 *
 * prompt:		The prompt to print
 * maxlen:		How many characters to accept
 * echo:		Set to false if you want to hide what is entered (for passwords)
 *
 * Returns a malloc()'ed string with the input (w/o trailing newline).
 */
#include "c.h"

#include <termios.h>

extern char *simple_prompt(const char *prompt, int maxlen, bool echo);

char *
simple_prompt(const char *prompt, int maxlen, bool echo)
{
	int			length;
	char	   *destination;
	FILE	   *termin,
			   *termout;

	struct termios t_orig,
				t;

	destination = (char *) malloc(maxlen + 1);
	if (!destination)
		return NULL;

	/*
	 * Do not try to collapse these into one "w+" mode file. Doesn't work on
	 * some platforms (eg, HPUX 10.20).
	 */
	termin = fopen(DEVTTY, "r");
	termout = fopen(DEVTTY, "w");
	if (!termin || !termout
		)
	{
		if (termin)
			fclose(termin);
		if (termout)
			fclose(termout);
		termin = stdin;
		termout = stderr;
	}

	if (!echo)
	{
		tcgetattr(fileno(termin), &t);
		t_orig = t;
		t.c_lflag &= ~ECHO;
		tcsetattr(fileno(termin), TCSAFLUSH, &t);
	}

	if (prompt)
	{
		fputs(_(prompt), termout);
		fflush(termout);
	}

	if (fgets(destination, maxlen + 1, termin) == NULL)
		destination[0] = '\0';

	length = strlen(destination);
	if (length > 0 && destination[length - 1] != '\n')
	{
		/* eat rest of the line */
		char		buf[128];
		int			buflen;

		do
		{
			if (fgets(buf, sizeof(buf), termin) == NULL)
				break;
			buflen = strlen(buf);
		} while (buflen > 0 && buf[buflen - 1] != '\n');
	}

	if (length > 0 && destination[length - 1] == '\n')
		/* remove trailing newline */
		destination[length - 1] = '\0';

	if (!echo)
	{
		tcsetattr(fileno(termin), TCSAFLUSH, &t_orig);
		fputs("\n", termout);
		fflush(termout);
	}

	if (termin != stdin)
	{
		fclose(termin);
		fclose(termout);
	}

	return destination;
}
