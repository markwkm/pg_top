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

extern int	errno;

extern char *copyright;

/* imported from screen.c */
extern int	overstrike;

extern int	max_topn;

#define BEGIN "BEGIN;"
#define ROLLBACK "ROLLBACK;"

struct cmd	cmd_map[] = {
	{'\014', cmd_redraw},
	{'#', cmd_number},
	{' ', cmd_update},
	{'?', cmd_help},
	{'A', cmd_explain_analyze},
	{'a', cmd_activity},
	{'c', cmd_cmdline},
#ifdef ENABLE_COLOR
	{'C', cmd_color},
#endif							/* ENABLE_COLOR */
	{'d', cmd_displays},
	{'E', cmd_explain},
	{'h', cmd_help},
	{'i', cmd_idletog},
	{'I', cmd_io},
	{'L', cmd_locks},
	{'n', cmd_number},
	{'o', cmd_order},
	{'q', cmd_quit},
	{'R', cmd_replication},
	{'Q', cmd_current_query},
	{'s', cmd_delay},
	{'u', cmd_user},
	{'\0', NULL},
};

int
cmd_activity(struct pg_top_context *pgtctx)
{
	pgtctx->mode = MODE_PROCESSES;
	pgtctx->header_text =
		pgtctx->header_options[pgtctx->mode_remote][pgtctx->mode];
	reset_display(pgtctx);
	return No;
}

#ifdef ENABLE_COLOR
int
cmd_color(struct pg_top_context *pgtctx)
{
	reset_display(pgtctx);
	if (pgtctx->color_on)
	{
		pgtctx->color_on = 0;
		display_resize();		/* To realloc screenbuf */
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
#endif							/* ENABLE_COLOR */

int
cmd_cmdline(struct pg_top_context *pgtctx)
{
	if (pgtctx->statics.flags.fullcmds)
	{
		pgtctx->ps.fullcmd = (pgtctx->ps.fullcmd + 1) % 3;
		switch (pgtctx->ps.fullcmd)
		{
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
	int			newval;
	char		tempbuf1[50];

	new_message(MT_standout, "Current query of process: ");
	newval = readline(tempbuf1, 8, Yes);
	reset_display(pgtctx);
	display_pagerstart();
	show_current_query(&pgtctx->conninfo, newval);
	display_pagerend();
	return No;
}

int
cmd_delay(struct pg_top_context *pgtctx)
{
	int			i;
	char		tempbuf[50];

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
	int			i;
	char		tempbuf[50];

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
cmd_explain(struct pg_top_context *pgtctx)
{
	int			newval;
	char		tempbuf1[50];

	new_message(MT_standout, "Re-determine execution plan: ");
	newval = readline(tempbuf1, 8, Yes);
	reset_display(pgtctx);
	display_pagerstart();
	show_explain(&pgtctx->conninfo, newval, EXPLAIN);
	display_pagerend();
	return No;
}

int
cmd_explain_analyze(struct pg_top_context *pgtctx)
{
	int			newval;
	char		tempbuf1[50];

	new_message(MT_standout, "Re-run SQL for analysis: ");
	newval = readline(tempbuf1, 8, Yes);
	reset_display(pgtctx);
	display_pagerstart();
	show_explain(&pgtctx->conninfo, newval, EXPLAIN_ANALYZE);
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
cmd_io(struct pg_top_context *pgtctx)
{
	pgtctx->mode = MODE_IO_STATS;
	pgtctx->header_text =
		pgtctx->header_options[pgtctx->mode_remote][pgtctx->mode];
	reset_display(pgtctx);
	return No;
}

int
cmd_locks(struct pg_top_context *pgtctx)
{
	int			newval;
	char		tempbuf1[50];

	new_message(MT_standout, "Show locks held by process: ");
	newval = readline(tempbuf1, 8, Yes);
	reset_display(pgtctx);
	display_pagerstart();
	show_locks(&pgtctx->conninfo, newval);
	display_pagerend();
	return No;
}

int
cmd_number(struct pg_top_context *pgtctx)
{
	int			newval;
	char		tempbuf[50];

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
cmd_replication(struct pg_top_context *pgtctx)
{
	pgtctx->mode = MODE_REPLICATION;
	pgtctx->header_text =
		pgtctx->header_options[pgtctx->mode_remote][pgtctx->mode];
	reset_display(pgtctx);
	return No;
}

int
cmd_order(struct pg_top_context *pgtctx)
{
	int			i;
	int			no_command = No;
	char		tempbuf[50];

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
			i = string_index(tempbuf, pgtctx->statics.order_names);
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
	return no_command;
}

int
cmd_order_cpu(struct pg_top_context *pgtctx)
{
	int			i;

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
	int			i;

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
cmd_redraw(struct pg_top_context *pgtctx)
{
	reset_display(pgtctx);
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
	new_message(MT_standout, "Username to show: ");
	if (readline(pgtctx->ps.usename, sizeof(pgtctx->ps.usename), No) > 0)
	{
		putchar('\r');
	}
	else
	{
		clear_message();
	}
	return No;
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
			return (cmap->func) (pgtctx);
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
 *	show_help() - display the help screen; invoked in response to
 *		either 'h' or '?'.
 */

void
show_help(struct statics *stp)
{
	static char *fullhelp;
	char	   *p = NULL;

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
		fullhelp = (char *) malloc(strlen(help_text) + strlen(p) + 2);
		sprintf(fullhelp, help_text, p);
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

void
show_current_query(struct pg_conninfo_ctx *conninfo, int procpid)
{
	int			i;
	int			rows;
	char		info[64];
	PGresult   *pgresult = NULL;

	sprintf(info, "Current query for procpid %d:\n\n", procpid);
	display_pager(info);

	/* Get the currently running query. */
	connect_to_db(conninfo);
	if (conninfo->connection != NULL)
	{
		pgresult = pg_query(conninfo->connection, procpid);
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
	disconnect_from_db(conninfo);
}

void
show_explain(struct pg_conninfo_ctx *conninfo, int procpid, int analyze)
{
	int			i,
				j;
	int			rows,
				r;
	char		sql[4096];
	char		info[1024];
	PGresult   *pgresult_query = NULL;
	PGresult   *pgresult_explain = NULL;

	sprintf(info,
			"Current query plan for procpid %d:\n\n Statement:\n\n",
			procpid);
	display_pager(info);

	/* Get the currently running query. */
	connect_to_db(conninfo);
	if (conninfo->connection != NULL)
	{
		pgresult_query = pg_query(conninfo->connection, procpid);
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
			sprintf(sql, "EXPLAIN (ANALYZE, VERBOSE, BUFFERS)\n%s",
					PQgetvalue(pgresult_query, i, 0));
		}
		else
		{
			sprintf(sql, "EXPLAIN\n%s", PQgetvalue(pgresult_query, i, 0));
		}
		PQexec(conninfo->connection, BEGIN);
		pgresult_explain = PQexec(conninfo->connection, sql);
		PQexec(conninfo->connection, ROLLBACK);
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
	disconnect_from_db(conninfo);
}

void
show_locks(struct pg_conninfo_ctx *conninfo, int procpid)
{
	int			i,
				j,
				k;
	int			rows;
	char		info[64];
	int			width[7] = {1, 8, 6, 5, 5, 4, 7};
	PGresult   *pgresult = NULL;
	char		header_format[1024];
	char		line_format[1024];
	char		prefix[21];		/* Should hold any 64 bit integer. */
	char		line[1024];

	sprintf(info, "Locks held by procpid %d:\n\n", procpid);
	display_pager(info);

	/* Get the locks helf by the process. */
	connect_to_db(conninfo);
	if (conninfo->connection == NULL)
	{
		disconnect_from_db(conninfo);
		return;
	}

	pgresult = pg_locks(conninfo->connection, procpid);
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
		if (strlen(PQgetvalue(pgresult, i, 4)) > width[5])
			width[5] = strlen(PQgetvalue(pgresult, i, 4));
		if (strlen(PQgetvalue(pgresult, i, 5)) > width[6])
			width[6] = strlen(PQgetvalue(pgresult, i, 5));
	}
	sprintf(header_format,
			"%%-%ds | %%-%ds | %%-%ds | %%-%ds | %%-%ds | %%-%ds | %%-%ds\n",
			width[0], width[1], width[2], width[3], width[4], width[5],
			width[6]);
	sprintf(line_format,
			"%%%dd | %%-%ds | %%-%ds | %%-%ds | %%-%ds | %%-%ds | %%-%ds\n",
			width[0], width[1], width[2], width[3], width[4], width[5],
			width[6]);

	/* Display the header. */
	sprintf(line, header_format, "", "database", "schema", "table", "index",
			"type", "granted");
	display_pager(line);
	for (i = 0, k = 0; i < 7; i++)
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
				PQgetvalue(pgresult, i, 3), PQgetvalue(pgresult, i, 4),
				PQgetvalue(pgresult, i, 5));
		display_pager(line);
	}
	display_pager("\n");

	PQclear(pgresult);
	disconnect_from_db(conninfo);
}
