/*
 *	Top users/processes display for Unix
 *	Version 3
 *
 *	This program may be freely redistributed,
 *	but this entire comment MUST remain intact.
 *
 *	Copyright (c) 1984, 1989, William LeFebvre, Rice University
 *	Copyright (c) 1989, 1990, 1992, William LeFebvre, Northwestern University
 *	Copyright (c) 2007-2019, Mark Wong
 */

/*
 *	This file contains the routines that implement some of the interactive
 *	mode commands.	Note that some of the commands are implemented in-line
 *	in "main".	This is necessary because they change the global state of
 *	"top" (i.e.:  changing the number of processes to display).
 */

#include "os.h"
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#include <unistd.h>

#include "sigdesc.h"			/* generated automatically */
#include "pg_top.h"
#include "boolean.h"
#include "utils.h"
#include "version.h"
#include "machine.h"
#include "help.h"
#include "display.h"
#include "pg.h"
#include "commands.h"
#include "screen.h"
#include "username.h"

extern int	errno;

extern char *copyright;

/* imported from screen.c */
extern int	overstrike;

extern int max_topn;

/*
 *	Some of the commands make system calls that could generate errors.
 *	These errors are collected up in an array of structures for later
 *	contemplation and display.	Such routines return a string containing an
 *	error message, or NULL if no errors occurred.  We need an upper limit on
 *	the number of errors, so we arbitrarily choose 20.
 */

#define ERRMAX 20

struct errs						/* structure for a system-call error */
{
	int			errnum;			/* value of errno (that is, the actual error) */
	char	   *arg;			/* argument that caused the error */
};

static struct errs errs[ERRMAX];
static int	errcnt;
static char *err_toomany = " too many errors occurred";
static char *err_listem =
" Many errors occurred.  Press `e' to display the list of errors.";

char header_index_stats[43] = "  I_SCANS   I_READS I_FETCHES INDEXRELNAME";
char header_io_stats[64] =
		"  PID RCHAR WCHAR   SYSCR   SYSCW READS WRITES CWRITES COMMAND";
char header_statements[46] = "  CALLS CALLS%   TOTAL_TIME     AVG_TIME QUERY";

/* These macros get used to reset and log the errors */
#define ERR_RESET	errcnt = 0
#define ERROR(p, e) if (errcnt >= ERRMAX) \
			{ \
			return(err_toomany); \
			} \
			else \
			{ \
			errs[errcnt].arg = (p); \
			errs[errcnt++].errnum = (e); \
			}

#define BEGIN "BEGIN;"
#define ROLLBACK "ROLLBACK;"

struct cmd cmd_map[] = {
    {'\014', cmd_redraw},
	{'#', cmd_number},
    {' ', cmd_update},
    {'?', cmd_help},
	{'A', cmd_explain_analyze},
	{'c', cmd_cmdline},
#ifdef ENABLE_COLOR
	{'C', cmd_color},
#endif /* ENABLE_COLOR */
	{'d', cmd_displays},
	{'e', cmd_errors},
	{'E', cmd_explain},
    {'h', cmd_help},
	{'i', cmd_idletog},
	{'I', cmd_io},
#ifdef ENABLE_KILL
	{'k', cmd_kill},
#endif /* ENABLE_KILL */
	{'L', cmd_locks},
	{'n', cmd_number},
	{'M', cmd_order_mem},
	{'N', cmd_order_pid},
	{'o', cmd_order},
	{'P', cmd_order_cpu},
	{'q', cmd_quit},
	{'Q', cmd_current_query},
#ifdef ENABLE_KILL
	{'r', cmd_renice},
#endif /* ENABLE_KILL */
	{'s', cmd_delay},
	{'S', cmd_statements},
	{'t', cmd_toggle},
	{'T', cmd_order_time},
	{'u', cmd_user},
	{'X', cmd_indexes},
    {'\0', NULL},
};

#ifdef ENABLE_COLOR
int
cmd_color(struct pg_top_context *pgtctx)
{
	reset_display(pgtctx);
	if (pgtctx->color_on)
	{
		pgtctx->color_on = 0;
		display_resize(); /* To realloc screenbuf */
		new_message(MT_standout | MT_delayed, " Color off");
	}
	else
	{
		if (!smart_terminal)
		{
			new_message(MT_standout | MT_delayed,
						" Sorry, cannot do colors on this terminal type");
		}
		else
		{
			pgtctx->color_on = 1;
			new_message(MT_standout | MT_delayed, " Color on");
		}
	}
	return No;
}
#endif /* ENABLE_COLOR */

int
cmd_cmdline(struct pg_top_context *pgtctx)
{
	if (pgtctx->statics.flags.fullcmds)
	{
		pgtctx->ps.fullcmd = (pgtctx->ps.fullcmd + 1) % 3;
		switch (pgtctx->ps.fullcmd) {
		case 2:
			new_message(MT_standout | MT_delayed, " Displaying current query.");
			break;
		case 1:
			new_message(MT_standout | MT_delayed,
					" Displaying full command lines.");
			break;
		case 0:
		default:
			new_message(MT_standout | MT_delayed,
			" Not displaying full command lines.");
		}
	}
	else
	{
		new_message(MT_standout, " Full command display not supported.");
		/* no_command = Yes; */
	}
	putchar('\r');
	return No;
}

int
cmd_current_query(struct pg_top_context *pgtctx)
{
	int newval;
	char tempbuf1[50];

	new_message(MT_standout, "Current query of process: ");
	newval = readline(tempbuf1, 8, Yes);
	reset_display(pgtctx);
	display_pagerstart();
	show_current_query(pgtctx->values, newval);
	display_pagerend();
	return No;
}

int
cmd_delay(struct pg_top_context *pgtctx)
{
	int i;
	char tempbuf[50];

	new_message(MT_standout, "Seconds to delay: ");
	if ((i = readline(tempbuf, 8, Yes)) > -1)
	{
		if ((pgtctx->delay = i) == 0 && getuid() != 0)
		{
			pgtctx->delay = 1;
		}
	}
	clear_message();
	return No;
}

int
cmd_displays(struct pg_top_context *pgtctx)
{
	int i;
	char tempbuf[50];

	new_message(MT_standout, "Displays to show (currently %s): ",
			pgtctx->displays == -1 ? "infinite" : itoa(pgtctx->displays));
	if ((i = readline(tempbuf, 10, Yes)) > 0)
	{
		pgtctx->displays = i;
	}
	else if (i == 0)
	{
		quit(0);
	}
	clear_message();
	return No;
}

int
cmd_errors(struct pg_top_context *pgtctx)
{
	char ch;

	if (error_count() == 0)
	{
		new_message(MT_standout, " Currently no errors to report.");
		putchar('\r');
		return Yes;
	}
	else
	{
		reset_display(pgtctx);
		clear();
		show_errors();
		standout("Hit any key to continue: ");
		fflush(stdout);
		(void) read(0, &ch, 1);
	}
	return No;
}

int
cmd_explain(struct pg_top_context *pgtctx)
{
	int newval;
	char tempbuf1[50];

	new_message(MT_standout, "Re-determine execution plan: ");
	newval = readline(tempbuf1, 8, Yes);
	reset_display(pgtctx);
	display_pagerstart();
	show_explain(pgtctx->values, newval, EXPLAIN);
	display_pagerend();
	return No;
}

int
cmd_explain_analyze(struct pg_top_context *pgtctx)
{
	int newval;
	char tempbuf1[50];

	new_message(MT_standout, "Re-run SQL for analysis: ");
	newval = readline(tempbuf1, 8, Yes);
	reset_display(pgtctx);
	display_pagerstart();
	show_explain(pgtctx->values, newval, EXPLAIN_ANALYZE);
	display_pagerend();
	return No;
}

int
cmd_help(struct pg_top_context *pgtctx)
{
	reset_display(pgtctx);
	display_pagerstart();
	show_help(&pgtctx->statics);
	display_pagerend();
	return No;
}

int
cmd_idletog(struct pg_top_context *pgtctx)
{
	pgtctx->ps.idle = !pgtctx->ps.idle;
	new_message(MT_standout | MT_delayed, " %sisplaying idle processes.",
			pgtctx->ps.idle ? "D" : "Not d");
	putchar('\r');
	return No;
}

int
cmd_indexes(struct pg_top_context *pgtctx)
{
	if (pgtctx->mode == MODE_INDEX_STATS)
	{
		pgtctx->mode = MODE_PROCESSES;
		pgtctx->header_text = pgtctx->header_processes;
	}
	else
	{
		pgtctx->mode = MODE_INDEX_STATS;
		pgtctx->header_text = header_index_stats;
	}

	/* Reset display to show changed header text. */
	reset_display(pgtctx);
	return No;
}

int
cmd_io(struct pg_top_context *pgtctx)
{
	if (pgtctx->mode == MODE_IO_STATS)
	{
		pgtctx->mode = MODE_PROCESSES;
		pgtctx->header_text =
				pgtctx->header_processes;
	}
	else
	{
		pgtctx->mode = MODE_IO_STATS;
		pgtctx->header_text = header_io_stats;
	}
	reset_display(pgtctx);
	return No;
}

#ifdef ENABLE_KILL
int
cmd_kill(struct pg_top_context *pgtctx)
{
	char *errmsg;
	char tempbuf[50];

	if (pgtctx->mode_remote == Yes)
	{
		new_message(MT_standout, "Cannot kill when accessing a remote database.");
		putchar('\r');
		return Yes;
	}
	new_message(0, "kill ");
	if (readline(tempbuf, sizeof(tempbuf), No) > 0)
	{
		if ((errmsg = kill_procs(tempbuf)) != NULL)
		{
			new_message(MT_standout, "%s", errmsg);
			putchar('\r');
			return Yes;
		}
	}
	else
	{
		clear_message();
	}
	return No;
}
#endif /* ENABLE_KILL */

int
cmd_locks(struct pg_top_context *pgtctx)
{
	int newval;
	char tempbuf1[50];

	new_message(MT_standout, "Show locks held by process: ");
	newval = readline(tempbuf1, 8, Yes);
	reset_display(pgtctx);
	display_pagerstart();
	show_locks(pgtctx->values, newval);
	display_pagerend();
	return No;
}

int
cmd_number(struct pg_top_context *pgtctx)
{
	int newval;
	char tempbuf[50];

	new_message(MT_standout, "Number of processes to show: ");
	newval = readline(tempbuf, 8, Yes);
	if (newval > -1)
	{
		if (newval > max_topn)
		{
			new_message(MT_standout | MT_delayed,
						" This terminal can only display %d processes.",
						max_topn);
			putchar('\r');
		}

		if (newval == 0)
		{
			/* inhibit the header */
			display_header(No);
		}
		else if (newval > pgtctx->topn && pgtctx->topn == 0)
		{
			/* redraw the header */
			display_header(Yes);
			pgtctx->d_header = i_header;
		}
		pgtctx->topn = newval;
	}
	return No;
}

int
cmd_quit(struct pg_top_context *pgtctx)
{
	quit(0);
	/* NOT REACHED */
	return No;
}

int
cmd_order(struct pg_top_context *pgtctx)
{
	int i;
	int no_command = No;
	char tempbuf[50];

	switch (pgtctx->mode)
	{
	case MODE_INDEX_STATS:
		new_message(MT_standout, "Order to sort: ");
		if (readline(tempbuf, sizeof(tempbuf), No) > 0)
		{
			if ((i = string_index(tempbuf, index_ordernames)) == -1)
			{
				new_message(MT_standout, " %s: unrecognized sorting order",
						tempbuf);
				no_command = Yes;
			}
			else
			{
				pgtctx->index_order_index = i;
			}
			putchar('\r');
		}
		else
		{
			clear_message();
		}
		break;
	case MODE_IO_STATS:
		new_message(MT_standout, "Order to sort: ");
		if (readline(tempbuf, sizeof(tempbuf), No) > 0)
		{
			if ((i = string_index(tempbuf,
					pgtctx->statics.order_names_io)) == -1)
			{
				new_message(MT_standout, " %s: unrecognized sorting order",
							tempbuf);
				no_command = Yes;
			}
			else
			{
				pgtctx->io_order_index = i;
			}
		}
		else
		{
			clear_message();
		}
		break;
	case MODE_STATEMENTS:
		new_message(MT_standout, "Order to sort: ");
		if (readline(tempbuf, sizeof(tempbuf), No) > 0)
		{
			if ((i = string_index(tempbuf, statement_ordernames)) == -1)
			{
				new_message(MT_standout, " %s: unrecognized sorting order",
							tempbuf);
				no_command = Yes;
			}
			else
			{
				pgtctx->statement_order_index = i;
			}
		}
		else
		{
			clear_message();
		}
		break;
	case MODE_PROCESSES:
	default:
		if (pgtctx->statics.order_names == NULL)
		{
			new_message(MT_standout, " Ordering not supported.");
			putchar('\r');
			no_command = Yes;
		}
		else
		{
			new_message(MT_standout, "Order to sort: ");
			if (readline(tempbuf, sizeof(tempbuf), No) > 0)
			{
				i = string_index(tempbuf, pgtctx->statics.order_names) == -1;
				if (i == -1)
				{
					new_message(MT_standout, " %s: unrecognized sorting order",
							tempbuf);
					no_command = Yes;
				}
				else
				{
					pgtctx->order_index = i;
				}
				putchar('\r');
			}
			else
			{
				clear_message();
			}
		}
	}
	return no_command;
}

int
cmd_order_cpu(struct pg_top_context *pgtctx)
{
	int i;

	if ((i = string_index("cpu", pgtctx->statics.order_names)) == -1)
	{
		new_message(MT_standout, " Unrecognized sorting order");
		putchar('\r');
		/* no_command = Yes; */
	}
	else
	{
		pgtctx->order_index = i;
	}
	return No;
}

int
cmd_order_mem(struct pg_top_context *pgtctx)
{
	int i;

	if ((i = string_index("size", pgtctx->statics.order_names)) == -1)
	{
		new_message(MT_standout, " Unrecognized sorting order");
		putchar('\r');
		return Yes;
	}
	else
	{
		pgtctx->order_index = i;
	}
	return No;
}

int
cmd_order_pid(struct pg_top_context *pgtctx)
{
	int i;

	if ((i = string_index("pid", pgtctx->statics.order_names)) == -1)
	{
		new_message(MT_standout, " Unrecognized sorting order");
		putchar('\r');
		return Yes;
	}
	else
	{
		pgtctx->order_index = i;
	}
	return No;
}

int
cmd_order_time(struct pg_top_context *pgtctx)
{
	int i;

	if ((i = string_index("time", pgtctx->statics.order_names)) == -1)
	{
		new_message(MT_standout, " Unrecognized sorting order");
		putchar('\r');
		return Yes;
	}
	else
	{
		pgtctx->order_index = i;
	}
	return No;
}

int
cmd_redraw(struct pg_top_context *pgtctx)
{
	reset_display(pgtctx);
	return No;
}

#ifdef ENABLE_KILL
int
cmd_renice(struct pg_top_context *pgtctx)
{
	char *errmsg;
	char tempbuf[50];

	if (pgtctx->mode_remote == Yes)
	{
		new_message(MT_standout,
				"Cannot renice when accessing a remote database.");
		putchar('\r');
		return Yes;
	}
	new_message(0, "renice ");
	if (readline(tempbuf, sizeof(tempbuf), No) > 0)
	{
		if ((errmsg = renice_procs(tempbuf)) != NULL)
		{
			new_message(MT_standout, "%s", errmsg);
			putchar('\r');
			/* no_command = Yes; */
		}
	}
	else
	{
		clear_message();
	}
	return No;
}
#endif /* ENABLE_KILL */

int
cmd_statements(struct pg_top_context *pgtctx)
{
	if (pgtctx->mode == MODE_STATEMENTS)
	{
		pgtctx->mode = MODE_PROCESSES;
		pgtctx->header_text =
				pgtctx->header_processes;
	}
	else
	{
		pgtctx->mode = MODE_STATEMENTS;
		pgtctx->header_text = header_statements;
	}
	reset_display(pgtctx);
	return No;
}

int
cmd_toggle(struct pg_top_context *pgtctx)
{
	if (mode_stats == STATS_DIFF)
	{
		mode_stats = STATS_CUMULATIVE;
		new_message(MT_standout | MT_delayed,
					" Displaying cumulative statistics.");
		putchar('\r');
	}
	else
	{
		mode_stats = STATS_DIFF;
		new_message(MT_standout | MT_delayed,
					" Displaying differential statistics.");
		putchar('\r');
	}
	return No;
}

int
cmd_update(struct pg_top_context *pgtctx)
{
	/* go home for visual feedback */
	go_home();
	fflush(stdout);
	return No;
}

int
cmd_user(struct pg_top_context *pgtctx)
{
	int i;
	int no_command = No;
	char tempbuf[50];

	new_message(MT_standout, "Username to show: ");
	if (readline(tempbuf, sizeof(tempbuf), No) > 0)
	{
		if (tempbuf[0] == '+' && tempbuf[1] == '\0')
		{
			pgtctx->ps.uid = -1;
		}
		else if ((i = userid(tempbuf)) == -1)
		{
			new_message(MT_standout, " %s: unknown user", tempbuf);
			no_command = Yes;
		}
		else
		{
			pgtctx->ps.uid = i;
		}
		putchar('\r');
	}
	else
	{
		clear_message();
	}
	return no_command;
}

/*
 *	err_compar(p1, p2) - comparison routine used by "qsort"
 *	for sorting errors.
 */

int
err_compar(const void *p1, const void *p2)

{
	register int result;

	if ((result = ((struct errs *) p1)->errnum -
		 ((struct errs *) p2)->errnum) == 0)
	{
		return (strcmp(((struct errs *) p1)->arg,
					   ((struct errs *) p2)->arg));
	}
	return (result);
}

int
execute_command(struct pg_top_context *pgtctx, char ch)
{
	struct cmd *cmap;
	cmap = cmd_map;

	while (cmap->func != NULL)
	{
		if (cmap->ch == ch)
		{
			return (cmap->func)(pgtctx);
		}
		++cmap;
	}
	return No;
}

/*
 *	str_adderr(str, len, err) - add an explanation of error "err" to
 *	the string "str".
 */

int
str_adderr(char *str, int len, int err)

{
	register char *msg;
	register int msglen;

	msg = err == 0 ? "Not a number" : errmsg(err);
	msglen = strlen(msg) + 2;
	if (len <= msglen)
	{
		return (0);
	}
	(void) strcat(str, ": ");
	(void) strcat(str, msg);
	return (len - msglen);
}

/*
 *	str_addarg(str, len, arg, first) - add the string argument "arg" to
 *	the string "str".  This is the first in the group when "first"
 *	is set (indicating that a comma should NOT be added to the front).
 */

int
str_addarg(char *str, int len, char *arg, int first)

{
	register int arglen;

	arglen = strlen(arg);
	if (!first)
	{
		arglen += 2;
	}
	if (len <= arglen)
	{
		return (0);
	}
	if (!first)
	{
		(void) strcat(str, ", ");
	}
	(void) strcat(str, arg);
	return (len - arglen);
}

/*
 *	err_string() - return an appropriate error string.	This is what the
 *	command will return for displaying.  If no errors were logged, then
 *	return NULL.  The maximum length of the error string is defined by
 *	"STRMAX".
 */

#define STRMAX 80

char *
err_string()

{
	register struct errs *errp;
	register int cnt = 0;
	register int first = Yes;
	register int currerr = -1;
	int			stringlen;		/* characters still available in "string" */
	static char string[STRMAX];

	/* if there are no errors, return NULL */
	if (errcnt == 0)
	{
		return (NULL);
	}

	/* sort the errors */
	qsort((char *) errs, errcnt, sizeof(struct errs), err_compar);

	/* need a space at the front of the error string */
	string[0] = ' ';
	string[1] = '\0';
	stringlen = STRMAX - 2;

	/* loop thru the sorted list, building an error string */
	while (cnt < errcnt)
	{
		errp = &(errs[cnt++]);
		if (errp->errnum != currerr)
		{
			if (currerr != -1)
			{
				if ((stringlen = str_adderr(string, stringlen, currerr)) < 2)
				{
					return (err_listem);
				}
				(void) strcat(string, "; ");	/* we know there's more */
			}
			currerr = errp->errnum;
			first = Yes;
		}
		if ((stringlen = str_addarg(string, stringlen, errp->arg, first)) == 0)
		{
			return (err_listem);
		}
		first = No;
	}

	/* add final message */
	stringlen = str_adderr(string, stringlen, currerr);

	/* return the error string */
	return (stringlen == 0 ? err_listem : string);
}

/*
 *	show_help() - display the help screen; invoked in response to
 *		either 'h' or '?'.
 */

void
show_help(struct statics * stp)

{
	static char *fullhelp;
	char	   *p = NULL;
	char	   *q = NULL;

	if (fullhelp == NULL)
	{
		/* set it up first time thru */
		if (stp->order_names != NULL)
		{
			p = string_list(stp->order_names);
		}
		if (p == NULL)
		{
			p = "not supported";
		}
		if (stp->order_names != NULL)
		{
			q = string_list(stp->order_names_io);
		}
		if (q == NULL)
		{
			q = "not supported";
		}
		fullhelp = (char *) malloc(strlen(help_text) + strlen(p) + strlen(q) +
				2);
		sprintf(fullhelp, help_text, p, q);
	}

	display_pager("pg_top version ");
	display_pager(version_string());
	display_pager(", ");
	display_pager(copyright);
	display_pager("\n");
	display_pager(fullhelp);
}

/*
 *	Utility routines that help with some of the commands.
 */

char *
next_field(char *str)


{
	if ((str = strchr(str, ' ')) == NULL)
	{
		return (NULL);
	}
	*str = '\0';
	while (*++str == ' ') /* loop */ ;

	/* if there is nothing left of the string, return NULL */
	/* This fix is dedicated to Greg Earle */
	return (*str == '\0' ? NULL : str);
}

int
scanint(char *str, int *intp)

{
	register int val = 0;
	register char ch;

	/* if there is nothing left of the string, flag it as an error */
	/* This fix is dedicated to Greg Earle */
	if (*str == '\0')
	{
		return (-1);
	}

	while ((ch = *str++) != '\0')
	{
		if (isdigit(ch))
		{
			val = val * 10 + (ch - '0');
		}
		else if (isspace(ch))
		{
			break;
		}
		else
		{
			return (-1);
		}
	}
	*intp = val;
	return (0);
}

/*
 *	error_count() - return the number of errors currently logged.
 */

int
error_count()

{
	return (errcnt);
}

/*
 *	show_errors() - display on stdout the current log of errors.
 */

void
show_errors()

{
	register int cnt = 0;
	register struct errs *errp = errs;

	printf("%d error%s:\n\n", errcnt, errcnt == 1 ? "" : "s");
	while (cnt++ < errcnt)
	{
		printf("%5s: %s\n", errp->arg,
			   errp->errnum == 0 ? "Not a number" : errmsg(errp->errnum));
		errp++;
	}
}

/*
 *	kill_procs(str) - send signals to processes, much like the "kill"
 *		command does; invoked in response to 'k'.
 */

char *
kill_procs(char *str)

{
	register char *nptr;
	int			signum = SIGTERM;		/* default */
	int			procnum;
	struct sigdesc *sigp;
	int			uid;

	/* reset error array */
	ERR_RESET;

	/* remember our uid */
	uid = getuid();

	/* skip over leading white space */
	while (isspace(*str))
		str++;

	if (str[0] == '-')
	{
		/* explicit signal specified */
		if ((nptr = next_field(str)) == NULL)
		{
			return (" kill: no processes specified");
		}

		if (isdigit(str[1]))
		{
			(void) scanint(str + 1, &signum);
			if (signum <= 0 || signum >= NSIG)
			{
				return (" invalid signal number");
			}
		}
		else
		{
			/* translate the name into a number */
			for (sigp = sigdesc; sigp->name != NULL; sigp++)
			{
				if (strcmp(sigp->name, str + 1) == 0)
				{
					signum = sigp->number;
					break;
				}
			}

			/* was it ever found */
			if (sigp->name == NULL)
			{
				return (" bad signal name");
			}
		}
		/* put the new pointer in place */
		str = nptr;
	}

	/* loop thru the string, killing processes */
	do
	{
		if (scanint(str, &procnum) == -1)
		{
			ERROR(str, 0);
		}
		else
		{
			/* check process owner if we're not root */
			if (uid && (uid != proc_owner(procnum)))
			{
				ERROR(str, EACCES);
			}
			/* go in for the kill */
			else if (kill(procnum, signum) == -1)
			{
				/* chalk up an error */
				ERROR(str, errno);
			}
		}
	} while ((str = next_field(str)) != NULL);

	/* return appropriate error string */
	return (err_string());
}

/*
 *	renice_procs(str) - change the "nice" of processes, much like the
 *		"renice" command does; invoked in response to 'r'.
 */

char *
renice_procs(char *str)

{
	register char negate;
	int			prio;
	int			procnum;
	int			uid;

	ERR_RESET;
	uid = getuid();

	/* allow for negative priority values */
	if ((negate = (*str == '-')) != 0)
	{
		/* move past the minus sign */
		str++;
	}

	/* use procnum as a temporary holding place and get the number */
	procnum = scanint(str, &prio);

	/* negate if necessary */
	if (negate)
	{
		prio = -prio;
	}

#if defined(PRIO_MIN) && defined(PRIO_MAX)
	/* check for validity */
	if (procnum == -1 || prio < PRIO_MIN || prio > PRIO_MAX)
	{
		return (" bad priority value");
	}
#endif

	/* move to the first process number */
	if ((str = next_field(str)) == NULL)
	{
		return (" no processes specified");
	}

#ifdef HAVE_SETPRIORITY
	/* loop thru the process numbers, renicing each one */
	do
	{
		if (scanint(str, &procnum) == -1)
		{
			ERROR(str, 0);
		}

		/* check process owner if we're not root */
		else if (uid && (uid != proc_owner(procnum)))
		{
			ERROR(str, EACCES);
		}
		else if (setpriority(PRIO_PROCESS, procnum, prio) == -1)
		{
			ERROR(str, errno);
		}
	} while ((str = next_field(str)) != NULL);

	/* return appropriate error string */
	return (err_string());
#else
	return (" operation not supported");
#endif
}

void
show_current_query(const char *values[], int procpid)
{
	int			i;
	int			rows;
	char		info[64];
	PGconn	   *pgconn;
	PGresult   *pgresult = NULL;

	sprintf(info, "Current query for procpid %d:\n\n", procpid);
	display_pager(info);

	/* Get the currently running query. */
	pgconn = connect_to_db(values);
	if (pgconn != NULL)
	{
		pgresult = pg_query(pgconn, procpid);
		rows = PQntuples(pgresult);
	}
	else
	{
		rows = 0;
	}
	for (i = 0; i < rows; i++)
	{
		display_pager(PQgetvalue(pgresult, i, 0));
	}
	display_pager("\n\n");

	if (pgresult != NULL)
		PQclear(pgresult);
	PQfinish(pgconn);
}

void
show_explain(const char *values[], int procpid, int analyze)
{
	int			i,
				j;
	int			rows,
				r;
	char		sql[4096];
	char		info[1024];
	PGconn	   *pgconn;
	PGresult   *pgresult_query = NULL;
	PGresult   *pgresult_explain = NULL;

	sprintf(info,
			"Current query plan for procpid %d:\n\n Statement:\n\n",
			procpid);
	display_pager(info);

	/* Get the currently running query. */
	pgconn = connect_to_db(values);
	if (pgconn != NULL)
	{
		pgresult_query = pg_query(pgconn, procpid);
		rows = PQntuples(pgresult_query);
	}
	else
	{
		rows = 0;
	}
	for (i = 0; i < rows; i++)
	{
		/* Display the query before the query plan. */
		display_pager(PQgetvalue(pgresult_query, i, 0));

		/* Execute the EXPLAIN. */
		if (analyze == EXPLAIN_ANALYZE)
		{
			sprintf(sql, "EXPLAIN ANALYZE\n%s",
					PQgetvalue(pgresult_query, i, 0));
		}
		else
		{
			sprintf(sql, "EXPLAIN\n%s", PQgetvalue(pgresult_query, i, 0));
		}
		PQexec(pgconn, BEGIN);
		pgresult_explain = PQexec(pgconn, sql);
		PQexec(pgconn, ROLLBACK);
		r = PQntuples(pgresult_explain);
		/* This will display an error if the EXPLAIN fails. */
		display_pager("\n\nQuery Plan:\n\n");
		display_pager(PQresultErrorMessage(pgresult_explain));
		for (j = 0; j < r; j++)
		{
			display_pager(PQgetvalue(pgresult_explain, j, 0));
			display_pager("\n");
		}
		if (pgresult_explain != NULL)
			PQclear(pgresult_explain);
	}
	display_pager("\n\n");

	if (pgresult_query != NULL)
		PQclear(pgresult_query);
	PQfinish(pgconn);
}

void
show_locks(const char *values[], int procpid)
{
	int			i,
				j,
				k;
	int			rows;
	char		info[64];
	int			width[5] = {1, 8, 5, 4, 7};
	PGconn	   *pgconn;
	PGresult   *pgresult = NULL;
	char		header_format[1024];
	char		line_format[1024];
	char		prefix[21];		/* Should hold any 64 bit integer. */
	char		line[1024];

	sprintf(info, "Locks held by procpid %d:\n\n", procpid);
	display_pager(info);

	/* Get the locks helf by the process. */
	pgconn = connect_to_db(values);
	if (pgconn == NULL)
	{
		PQfinish(pgconn);
		return;
	}

	pgresult = pg_locks(pgconn, procpid);
	rows = PQntuples(pgresult);

	/* Determine column sizes. */
	sprintf(prefix, "%d", rows);
	width[0] = strlen(prefix);
	for (i = 0; i < rows; i++)
	{
		if (strlen(PQgetvalue(pgresult, i, 0)) > width[1])
			width[1] = strlen(PQgetvalue(pgresult, i, 0));
		if (strlen(PQgetvalue(pgresult, i, 1)) > width[2])
			width[2] = strlen(PQgetvalue(pgresult, i, 1));
		if (strlen(PQgetvalue(pgresult, i, 2)) > width[3])
			width[3] = strlen(PQgetvalue(pgresult, i, 2));
		if (strlen(PQgetvalue(pgresult, i, 3)) > width[4])
			width[4] = strlen(PQgetvalue(pgresult, i, 3));
	}
	sprintf(header_format, "%%-%ds | %%-%ds | %%-%ds | %%-%ds | %%-%ds\n",
			width[0], width[1], width[2], width[3], width[4]);
	sprintf(line_format, "%%%dd | %%-%ds | %%-%ds | %%-%ds | %%-%ds\n",
			width[0], width[1], width[2], width[3], width[4]);

	/* Display the header. */
	sprintf(line, header_format, "", "database", "table", "type", "granted");
	display_pager(line);
	for (i = 0, k = 0; i < 5; i++)
	{
		for (j = 0; j < width[i]; j++, k++)
		{
			line[k] = '-';
		}
		line[k++] = '-';
		line[k++] = '+';
		line[k++] = '-';
	}
	line[k - 3] = '\n';
	line[k - 2] = '\0';
	display_pager(line);

	/* Display data. */
	for (i = 0; i < rows; i++)
	{
		sprintf(line, line_format, i + 1, PQgetvalue(pgresult, i, 0),
				PQgetvalue(pgresult, i, 1), PQgetvalue(pgresult, i, 2),
				PQgetvalue(pgresult, i, 3));
		display_pager(line);
	}
	display_pager("\n");

	PQclear(pgresult);
	PQfinish(pgconn);
}
