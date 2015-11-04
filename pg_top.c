char	   *copyright =
"Copyright (c) 1984 through 2007, William LeFebvre";

/*
 *	Top users/processes display for Unix
 *
 *	This program may be freely redistributed,
 *	but this entire comment MUST remain intact.
 *
 *	Copyright (c) 1984, 1989, William LeFebvre, Rice University
 *	Copyright (c) 1989 - 1994, William LeFebvre, Northwestern University
 *	Copyright (c) 1994, 1995, William LeFebvre, Argonne National Laboratory
 *	Copyright (c) 1996, William LeFebvre, Group sys Consulting
 *	Copyright (c) 2007-2015, Mark Wong
 *	Portions Copyright (c) 2013 VMware, Inc. All Rights Reserved.
 */

/*
 *	See the file "HISTORY" for information on version-to-version changes.
 */

/*
 *	This file contains "main" and other high-level routines.
 */

/*
 * The following preprocessor variables, when defined, are used to
 * distinguish between different Unix implementations:
 *
 *	FD_SET	 - macros FD_SET and FD_ZERO are used when defined
 */

#include "os.h"
#include <signal.h>
#include <setjmp.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>

/* determine which type of signal functions to use */
#ifdef HAVE_SIGACTION
#undef HAVE_SIGHOLD
#else
#if !defined(HAVE_SIGHOLD) || !defined(HAVE_SIGRELSE)
#define BSD_SIGNALS
#endif
#endif

/* includes specific to top */

#include "pg_top.h"
#include "remote.h"
#include "commands.h"
#include "display.h"			/* interface to display package */
#include "screen.h"				/* interface to screen package */
#include "boolean.h"
#include "username.h"
#include "utils.h"
#include "version.h"
#ifdef ENABLE_COLOR
#include "color.h"
#endif
#include "port.h"

/* Size of the stdio buffer given to stdout */
#define Buffersize	2048

/* The buffer that stdio will use */
char		stdoutbuf[Buffersize];

/* build signal masks */
#ifndef sigmask
#define sigmask(s)	(1 << ((s) - 1))
#endif

/* for getopt: */
extern int	optind;
extern char *optarg;

/* imported from screen.c */
extern int	overstrike;

/* values which need to be accessed by signal handlers */
static int	max_topn;			/* maximum displayable processes */

/* miscellaneous things */
char	   *myname = "pg_top";
jmp_buf		jmp_int;

/* internal variables */
static const char *progname = "pg_top";

static void usage(const char *progname);

/* List of all the options available */
static struct option long_options[] = {
	{"batch", no_argument, NULL, 'b'},
	{"show-command", no_argument, NULL, 'c'},
	{"color-mode", no_argument, NULL, 'C'},
	{"interactive", no_argument, NULL, 'i'},
	{"hide-idle", no_argument, NULL, 'I'},
	{"non-interactive", no_argument, NULL, 'n'},
	{"order-field", required_argument, NULL, 'o'},
	{"quick-mode", no_argument, NULL, 'q'},
	{"remote-mode", no_argument, NULL, 'r'},
	{"set-delay", required_argument, NULL, 's'},
	{"show-tags", no_argument, NULL, 'T'},
	{"show-uid", no_argument, NULL, 'u'},
	{"version", no_argument, NULL, 'V'},
	{"set-display", required_argument, NULL, 'x'},
	{"show-username", required_argument, NULL, 'z'},
	{"help", no_argument, NULL, '?'},
	{"dbname",  required_argument, NULL, 'd'},
	{"host", required_argument, NULL, 'h'},
	{"port", required_argument, NULL, 'p'},
	{"username", required_argument, NULL, 'U'},
	{"password",  no_argument, NULL, 'W'},
	{NULL, 0, NULL, 0}
};

/* pointers to display routines */
void		(*d_loadave) (int, double *) = i_loadave;
void		(*d_minibar) (
						  int (*) (char *, int)) = i_minibar;
void		(*d_uptime) (time_t *, time_t *) = i_uptime;
void		(*d_procstates) (int, int *) = i_procstates;
void		(*d_cpustates) (int64_t *) = i_cpustates;
void		(*d_memory) (long *) = i_memory;
void		(*d_swap) (long *) = i_swap;
void		(*d_db) (struct db_info *) = i_db;
void		(*d_io) (struct io_info *) = i_io;
void		(*d_disk) (struct disk_info *) = i_disk;
void		(*d_message) () = i_message;
void		(*d_header) (char *) = i_header;
void		(*d_process) (int, char *) = i_process;

/*
 * Mode for display cumulutive or differential stats when displaying table or
 * index statistics.
 */
int			mode_stats = STATS_DIFF;

/*
 * Mode for monitoring a remote database system.
 */
int mode_remote = 0;

/*
 *	usage - print help message with details about commands
 */
static void
usage(const char *progname)
{
	printf("%s monitors a PostgreSQL database cluster.\n\n", progname);
	printf("Usage:\n");
	printf("  %s [OPTION]... [NUMBER]\n", progname);
	printf("\nOptions:\n");
	printf("  -b, --batch               use batch mode\n");
	printf("  -c, --show-command        display command name of each process\n");
	printf("  -C, --color-mode          turn off color mode\n");
	printf("  -i, --interactive         use interactive mode\n");
	printf("  -I, --hide-idle           hide idle processes\n");
	printf("  -n, --non-interactive     use non-interactive mode\n");
	printf("  -o, --order-field=FIELD   select sort order\n");
	printf("  -q, --quick-mode          modify schedule priority\n");
	printf("                            usable only by root\n");
	printf("  -r, --remote-mode         activate remote mode\n");
	printf("  -s, --set-delay=SECOND    set delay between screen updates\n");
	printf("  -T, --show-tags           show color tags\n");
	printf("  -u, --show-uid            show UID instead of username\n");
	printf("  -V, --version             output version information, then exit\n");
	printf("  -x, --set-display=COUNT   set maximum number of displays\n");
	printf("                            exit once this number is reached\n");
	printf("  -z, --show-username=NAME  display only processes owned by given\n");
	printf("                            username\n");
	printf("  -?, --help                show this help, then exit\n");
	printf("\nConnection options:\n");
	printf("  -d, --dbname=DBNAME       database to connect to\n");
	printf("  -h, --host=HOSTNAME       database server host or socket directory\n");
	printf("  -p, --port=PORT           database server port\n");
	printf("  -U, --username=USERNAME   user name to connect as\n");
	printf("  -W, --password            force password prompt\n");
}

RETSIGTYPE
onalrm(int i)					/* SIGALRM handler */

{
	/* this is only used in batch mode to break out of the pause() */
	/* return; */
}

void
do_display(struct pg_top_context *pgtctx)
{
	register int i;
	register int active_procs;
	register int change;

	int io_order_index = 0;
	int index_order_index = 0;
	int table_order_index = 0;
	int tmp_index = 0;

	caddr_t processes;
	time_t curr_time;
	static struct ext_decl exts = {NULL, NULL};
	char no_command = 1;
	fd_set readfds;
	char ch;
	char *iptr;
	static char tempbuf1[50];
	static char tempbuf2[50];

	char header_index_stats[43] =
	"  I_SCANS   I_READS I_FETCHES INDEXRELNAME";
	char header_table_stats[78] =
	"SEQ_SCANS SEQ_READS   I_SCANS I_FETCHES   INSERTS   UPDATES   DELETES RELNAME";
	char header_io_stats[64] = "  PID RCHAR WCHAR   SYSCR   SYSCW READS WRITES CWRITES COMMAND";
	char header_statements[44] = "CALLS CALLS%   TOTAL_TIME     AVG_TIME QUERY";

	static char command_chars[] = "\f qh?en#sdkriIucoCNPMTQLERXAtS";

	switch (pgtctx->mode) {
	case MODE_IO_STATS:
		tmp_index = io_order_index;
		break;
	default:
		tmp_index = 0;
	}
	/* get the current stats and processes */
	if (mode_remote == 0)
	{
		get_system_info(&pgtctx->system_info);
#ifdef __linux__
		processes = get_process_info(&pgtctx->system_info, &pgtctx->ps, tmp_index,
				pgtctx->conninfo, pgtctx->mode);
#else
		processes = get_process_info(&pgtctx->system_info, &pgtctx->ps, tmp_index,
				pgtctx->conninfo);
#endif /* __linux__ */
	}
	else
	{
		get_system_info_r(&pgtctx->system_info, pgtctx->conninfo);
		processes = get_process_info_r(&pgtctx->system_info, &pgtctx->ps,
				pgtctx->order_index, pgtctx->conninfo);
	}

	/* Get database activity information */
	get_database_info(&pgtctx->db_info, pgtctx->conninfo);

	/* Get database I/O information */
	get_io_info(&pgtctx->io_info);

	/* display database disk info */
	get_disk_info(&pgtctx->disk_info, get_data_directory(pgtctx->conninfo));

	/* display the load averages */
	(*d_loadave) (pgtctx->system_info.last_pid, pgtctx->system_info.load_avg);

	/* this method of getting the time SHOULD be fairly portable */
	time(&curr_time);

	/* if we have a minibar extension, use it, otherwise show uptime */
	if (exts.f_minibar != NULL)
	{
		(*d_minibar) (exts.f_minibar);
	}
	else
	{
		(*d_uptime) (&pgtctx->statics.boottime, &curr_time);
	}

	/* display the current time */
	i_timeofday(&curr_time);

	/* display process state breakdown */
	(*d_procstates) (pgtctx->system_info.p_total, pgtctx->system_info.procstates);

	/* display the cpu state percentage breakdown */
	if (pgtctx->dostates)			/* but not the first time */
	{
		(*d_cpustates) (pgtctx->system_info.cpustates);
	}
	else
	{
		/* we'll do it next time */
		if (smart_terminal)
		{
			z_cpustates();
		}
		pgtctx->dostates = Yes;
	}

	/* display memory stats */
	(*d_memory) (pgtctx->system_info.memory);

	/* display database activity */
	(*d_db) (&pgtctx->db_info);

	/* display database I/O */
	(*d_io) (&pgtctx->io_info);

	/* display database disk info */
	(*d_disk) (&pgtctx->disk_info);

	/* display swap stats */
	(*d_swap) (pgtctx->system_info.swap);

	/* handle message area */
	(*d_message) ();

	/* update the header area */
	(*d_header) (pgtctx->header_text);

	if (pgtctx->topn > 0)
	{
		/* determine number of processes to actually display */

		/*
		 * this number will be the smallest of:  active processes, number
		 * user requested, number current screen accomodates
		 */
		active_procs = pgtctx->system_info.P_ACTIVE;
		if (active_procs > pgtctx->topn)
		{
			active_procs = pgtctx->topn;
		}
		if (active_procs > max_topn)
		{
			active_procs = max_topn;
		}

		/* Now show the top "n" processes or other statistics. */
		switch (pgtctx->mode)
		{
		case MODE_STATEMENTS:
			if (pg_display_statements(pgtctx->conninfo, max_topn) != 0)
				new_message(MT_standout | MT_delayed,
						" Extension pg_stat_statments not found");
			break;
		case MODE_INDEX_STATS:
			pg_display_index_stats(pgtctx->conninfo, index_order_index, max_topn);
			break;
		case MODE_TABLE_STATS:
			pg_display_table_stats(pgtctx->conninfo, table_order_index, max_topn);
			break;
#ifdef __linux__
		case MODE_IO_STATS:
			for (i = 0; i < active_procs; i++)
			{
				if (mode_remote == 0)
					(*d_process) (i, format_next_io(processes,
							pgtctx->get_userid));
				else
					(*d_process) (i, format_next_io_r(processes));
			}
			break;
#endif /* __linux__ */
		case MODE_PROCESSES:
		default:
			for (i = 0; i < active_procs; i++)
			{
				if (mode_remote == 0)
					(*d_process) (i, format_next_process(processes,
							pgtctx->get_userid));
				else
					(*d_process) (i, format_next_process_r(processes));
			}
		}
	}
	else
	{
		i = 0;
	}

	/* do end-screen processing */
	u_endscreen(i);

	/* now, flush the output buffer */
	if (fflush(stdout) != 0)
	{
		new_message(MT_standout, " Write error on stdout");
		putchar('\r');
		quit(1);
		/* NOTREACHED */
	}

	/* only do the rest if we have more displays to show */
	if (pgtctx->displays)
	{
		/* switch out for new display on smart terminals */
		if (smart_terminal)
		{
			if (overstrike)
			{
				reset_display();
			}
			else
			{
				d_loadave = u_loadave;
				d_minibar = u_minibar;
				d_uptime = u_uptime;
				d_procstates = u_procstates;
				d_cpustates = u_cpustates;
				d_memory = u_memory;
				d_db = u_db;
				d_io = u_io;
				d_disk = u_disk;
				d_swap = u_swap;
				d_message = u_message;
				d_header = u_header;
				d_process = u_process;
			}
		}

		no_command = Yes;
		if (!pgtctx->interactive)
		{
			/* set up alarm */
			(void) signal(SIGALRM, onalrm);
			(void) alarm((unsigned) pgtctx->delay);

			/* wait for the rest of it .... */
			pause();
		}
		else
			while (no_command)
			{
				/* assume valid command unless told otherwise */
				no_command = No;

				/* set up arguments for select with timeout */
				FD_ZERO(&readfds);
				FD_SET(0, &readfds);		/* for standard input */
				pgtctx->timeout.tv_sec = pgtctx->delay;
				pgtctx->timeout.tv_usec = 0;

				/* wait for either input or the end of the delay period */
				if (select(32, &readfds, (fd_set *) NULL, (fd_set *) NULL,
						&pgtctx->timeout) > 0)
				{
					int			newval;
					char	   *errmsg;

					/* something to read -- clear the message area first */
					clear_message();

					/* now read it and convert to command strchr */
					/* (use "change" as a temporary to hold strchr) */
					if (read(0, &ch, 1) != 1)
					{
						/* read error: either 0 or -1 */
						new_message(MT_standout, " Read error on stdin");
						putchar('\r');
						quit(1);
						/* NOTREACHED */
					}
					if ((iptr = strchr(command_chars, ch)) == NULL)
					{
						/* illegal command */
						new_message(MT_standout, " Command not understood");
						putchar('\r');
						no_command = Yes;
					}
					else
					{
						change = iptr - command_chars;
						if (overstrike && change > CMD_OSLIMIT)
						{
							/* error */
							new_message(MT_standout,
										" Command cannot be handled by this terminal");
							putchar('\r');
							no_command = Yes;
						}
						else
							switch (change)
							{
							case CMD_redraw:	/* redraw screen */
								reset_display();
								break;

							case CMD_update:	/* merely update display */
								/* go home for visual feedback */
								go_home();
								fflush(stdout);
								break;

							case CMD_quit:		/* quit */
								quit(0);
								/* NOTREACHED */
								break;

							case CMD_help1:		/* help */
							case CMD_help2:
								reset_display();
								display_pagerstart();
								show_help(&pgtctx->statics);
								display_pagerend();
								break;

							case CMD_errors:	/* show errors */
								if (error_count() == 0)
								{
									new_message(MT_standout,
												" Currently no errors to report.");
									putchar('\r');
									no_command = Yes;
								}
								else
								{
									reset_display();
									clear();
									show_errors();
									standout("Hit any key to continue: ");
									fflush(stdout);
									(void) read(0, &ch, 1);
								}
								break;

							case CMD_number1:	/* new number */
							case CMD_number2:
								new_message(MT_standout,
									"Number of processes to show: ");
								newval = readline(tempbuf1, 8, Yes);
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
									else if (newval > pgtctx->topn &&
											pgtctx->topn == 0)
									{
										/* redraw the header */
										display_header(Yes);
										d_header = i_header;
									}
									pgtctx->topn = newval;
								}
								break;

							case CMD_delay:		/* new seconds delay */
								new_message(MT_standout, "Seconds to delay: ");
								if ((i = readline(tempbuf1, 8, Yes)) > -1)
								{
									if ((pgtctx->delay = i) == 0 &&
											getuid() != 0)
									{
										pgtctx->delay = 1;
									}
								}
								clear_message();
								break;

							case CMD_displays:	/* change display count */
								new_message(MT_standout,
								 "Displays to show (currently %s): ",
										pgtctx->displays == -1 ? "infinite" :
											itoa(pgtctx->displays));
								if ((i = readline(tempbuf1, 10, Yes)) > 0)
								{
									pgtctx->displays = i;
								}
								else if (i == 0)
								{
									quit(0);
								}
								clear_message();
								break;

#ifdef ENABLE_KILL
							case CMD_kill:		/* kill program */
								if (mode_remote == 1)
								{
									new_message(MT_standout, "Cannot kill when accessing a remote database.");
									putchar('\r');
									no_command = Yes;
									break;
								}
								new_message(0, "kill ");
								if (readline(tempbuf2, sizeof(tempbuf2), No) > 0)
								{
									if ((errmsg = kill_procs(tempbuf2)) != NULL)
									{
										new_message(MT_standout, "%s", errmsg);
										putchar('\r');
										no_command = Yes;
									}
								}
								else
								{
									clear_message();
								}
								break;

							case CMD_renice:	/* renice program */
								if (mode_remote == 1)
								{
									new_message(MT_standout, "Cannot renice when accessing a remote database.");
									putchar('\r');
									no_command = Yes;
									break;
								}
								new_message(0, "renice ");
								if (readline(tempbuf2, sizeof(tempbuf2), No) > 0)
								{
									if ((errmsg = renice_procs(tempbuf2)) != NULL)
									{
										new_message(MT_standout, "%s", errmsg);
										putchar('\r');
										no_command = Yes;
									}
								}
								else
								{
									clear_message();
								}
								break;
#endif

							case CMD_idletog:
								pgtctx->ps.idle = !pgtctx->ps.idle;
								new_message(MT_standout | MT_delayed,
									  " %sisplaying idle processes.",
											pgtctx->ps.idle ? "D" : "Not d");
								putchar('\r');
								break;

							case CMD_cmdline:
								if (pgtctx->statics.flags.fullcmds)
								{
									pgtctx->ps.fullcmd =
											(pgtctx->ps.fullcmd + 1) % 3;
									switch (pgtctx->ps.fullcmd) {
									case 2:
										new_message(MT_standout | MT_delayed,
												" Displaying current query.");
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
									no_command = Yes;
								}
								putchar('\r');
								break;

							case CMD_user:
								new_message(MT_standout,
											"Username to show: ");
								if (readline(tempbuf2,
										sizeof(tempbuf2), No) > 0)
								{
									if (tempbuf2[0] == '+' &&
										tempbuf2[1] == '\0')
									{
										pgtctx->ps.uid = -1;
									}
									else if ((i = userid(tempbuf2)) == -1)
									{
										new_message(MT_standout,
													" %s: unknown user", tempbuf2);
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
								break;

							case CMD_order:
								switch (pgtctx->mode)
								{
									case MODE_INDEX_STATS:
										new_message(MT_standout,
												  "Order to sort: ");
										if (readline(tempbuf2,
												sizeof(tempbuf2), No) > 0)
										{
											if ((i = string_index(tempbuf2,
													index_ordernames)) == -1)
											{
												new_message(MT_standout,
															" %s: unrecognized sorting order", tempbuf2);
												no_command = Yes;
											}
											else
											{
												index_order_index = i;
											}
											putchar('\r');
										}
										else
										{
											clear_message();
										}
										break;
									case MODE_TABLE_STATS:
										new_message(MT_standout,
												  "Order to sort: ");
										if (readline(tempbuf2,
												sizeof(tempbuf2), No) > 0)
										{
											if ((i = string_index(tempbuf2,
													table_ordernames)) == -1)
											{
												new_message(MT_standout,
															" %s: unrecognized sorting order", tempbuf2);
												no_command = Yes;
											}
											else
											{
												table_order_index = i;
											}
											putchar('\r');
										}
										else
										{
											clear_message();
										}
										break;
									case MODE_IO_STATS:
										new_message(MT_standout,
												  "Order to sort: ");
										if (readline(tempbuf2, sizeof(tempbuf2), No) > 0)
										{
											if ((i = string_index(tempbuf2,
													pgtctx->statics.order_names_io)) == -1)
											{
												new_message(MT_standout,
															" %s: unrecognized sorting order", tempbuf2);
												no_command = Yes;
											}
											else
											{
												io_order_index = i;
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
											new_message(MT_standout,
												  "Order to sort: ");
											if (readline(tempbuf2,
													sizeof(tempbuf2), No) > 0)
											{
												if ((i = string_index(tempbuf2, pgtctx->statics.order_names)) == -1)
												{
													new_message(MT_standout,
																" %s: unrecognized sorting order", tempbuf2);
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
								break;
							case CMD_order_pid:
								if ((i = string_index("pid",
										pgtctx->statics.order_names)) == -1)
								{
									new_message(MT_standout,
									  " Unrecognized sorting order");
									putchar('\r');
									no_command = Yes;
								}
								else
								{
									pgtctx->order_index = i;
								}
								break;
							case CMD_order_cpu:
								if ((i = string_index("cpu",
										pgtctx->statics.order_names)) == -1)
								{
									new_message(MT_standout,
									  " Unrecognized sorting order");
									putchar('\r');
									no_command = Yes;
								}
								else
								{
									pgtctx->order_index = i;
								}
								break;
							case CMD_order_mem:
								if ((i = string_index("size",
										pgtctx->statics.order_names)) == -1)
								{
									new_message(MT_standout,
									  " Unrecognized sorting order");
									putchar('\r');
									no_command = Yes;
								}
								else
								{
									pgtctx->order_index = i;
								}
								break;
							case CMD_order_time:
								if ((i = string_index("time",
										pgtctx->statics.order_names)) == -1)
								{
									new_message(MT_standout,
									  " Unrecognized sorting order");
									putchar('\r');
									no_command = Yes;
								}
								else
								{
									pgtctx->order_index = i;
								}
								break;

#ifdef ENABLE_COLOR
							case CMD_color:
								reset_display();
								if (pgtctx->color_on)
								{
									pgtctx->color_on = 0;
									display_resize(); /* To realloc screenbuf */
									new_message(MT_standout | MT_delayed,
												" Color off");
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
										new_message(MT_standout | MT_delayed,
													" Color on");
									}
								}

								break;
#endif
							case CMD_current_query:
								new_message(MT_standout,
									   "Current query of process: ");
								newval = readline(tempbuf1, 8, Yes);
								reset_display();
								display_pagerstart();
								show_current_query(pgtctx->conninfo, newval);
								display_pagerend();
								break;

							case CMD_locks:
								new_message(MT_standout,
									 "Show locks held by process: ");
								newval = readline(tempbuf1, 8, Yes);
								reset_display();
								display_pagerstart();
								show_locks(pgtctx->conninfo, newval);
								display_pagerend();
								break;

							case CMD_explain:
								new_message(MT_standout,
										 "Re-determine execution plan: ");
								newval = readline(tempbuf1, 8, Yes);
								reset_display();
								display_pagerstart();
								show_explain(pgtctx->conninfo, newval, EXPLAIN);
								display_pagerend();
								break;

							case CMD_tables:
								if (pgtctx->mode == MODE_TABLE_STATS)
								{
									pgtctx->mode = MODE_PROCESSES;
									pgtctx->header_text =
											pgtctx->header_processes;
								}
								else
								{
									pgtctx->mode = MODE_TABLE_STATS;
									pgtctx->header_text = header_table_stats;
								}
								reset_display();
								break;

							case CMD_indexes:
								if (pgtctx->mode == MODE_INDEX_STATS)
								{
									pgtctx->mode = MODE_PROCESSES;
									pgtctx->header_text =
											pgtctx->header_processes;
								}
								else
								{
									pgtctx->mode = MODE_INDEX_STATS;
									pgtctx->header_text = header_index_stats;
								}

								/*
								 * Reset display to show changed
								 * header text.
								 */
								reset_display();
								break;

							case CMD_explain_analyze:
								new_message(MT_standout,
										 "Re-run SQL for analysis: ");
								newval = readline(tempbuf1, 8, Yes);
								reset_display();
								display_pagerstart();
								show_explain(pgtctx->conninfo, newval,
										EXPLAIN_ANALYZE);
								display_pagerend();
								break;

							case CMD_toggle:
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
								break;

							case CMD_io:
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
								reset_display();
								break;

							case CMD_statements:
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
								reset_display();
								break;

							default:
								new_message(MT_standout, " Unsupported command");
								putchar('\r');
								no_command = Yes;
							}
					}

					/* flush out stuff that may have been written */
					fflush(stdout);
				}
			}
	}
}

void
process_arguments(struct pg_top_context *pgtctx, int ac, char **av)
{
	int i;
	int option_index;
	char *password_tmp;

	while ((i = getopt_long(ac, av, "CDITbcinqruVh:s:d:U:o:Wp:x:z:",
			long_options, &option_index)) != EOF)
	{
		switch (i)
		{
#ifdef ENABLE_COLOR
		case 'C':
			pgtctx->color_on = !pgtctx->color_on;
			break;
#endif

		case 'D':
			debug_set(1);
			break;

		case 'V':		/* show version number */
			printf("pg_top %s\n", version_string());
			exit(0);
			break;

		case 'u':		/* toggle uid/username display */
			pgtctx->do_unames = !pgtctx->do_unames;
			break;

		case 'z':		/* display only username's processes */
			if ((pgtctx->ps.uid = userid(optarg)) == -1)
			{
				fprintf(stderr, "%s: unknown user\n", optarg);
				exit(1);
			}
			break;

		case 'I':		/* show idle processes */
			pgtctx->ps.idle = !pgtctx->ps.idle;
			break;

		case 'T':		/* show color tags */
			pgtctx->show_tags = 1;
			break;

		case 'i':		/* go interactive regardless */
			pgtctx->interactive = Yes;
			break;

		case 'c':
			pgtctx->ps.fullcmd = No;
			break;

		case 'n':		/* batch, or non-interactive */
		case 'b':
			pgtctx->interactive = No;
			break;

		case 'x':		/* number of displays to show */
			if ((i = atoiwi(optarg)) == Invalid || i == 0)
			{
				new_message(MT_standout | MT_delayed,
							" Bad display count (ignored)");
			}
			else
			{
				pgtctx->displays = i;
			}
			break;

		case 's':
			if ((pgtctx->delay = atoi(optarg)) < 0 ||
					(pgtctx->delay == 0 && getuid() != 0))
			{
				new_message(MT_standout | MT_delayed,
							" Bad seconds delay (ignored)");
				pgtctx->delay = Default_DELAY;
			}
			break;

		case 'q':		/* be quick about it */
			/* only allow this if user is really root */
			if (getuid() == 0)
			{
				/* be very un-nice! */
				(void) nice(-20);
			}
			else
			{
				new_message(MT_standout | MT_delayed,
							" Option -q can only be used by root");
			}
			break;

		case 'o':		/* select sort order */
			pgtctx->order_name = optarg;
			break;

		case 'p':		/* database port */
			if ((i = atoiwi(optarg)) == Invalid || i == 0)
			{
				new_message(MT_standout | MT_delayed,
							" Bad port number (ignored)");
			}
			else
			{
				pgtctx->dbport = i;
			}
			break;

		case 'W':		/* prompt for database password */
			password_tmp = simple_prompt("Password: ", 1000, 0);

			/*
			 * get the password in the format we want for the connect string
			 */
			sprintf(pgtctx->password, "password=%s", password_tmp);
			break;

		case 'U':		/* database user name */
			sprintf(pgtctx->dbusername, "user=%s", optarg);
			break;

		case 'd':		/* database name */
			sprintf(pgtctx->dbname, "dbname=%s", optarg);
			break;

		case 'h':		/* socket location */
			sprintf(pgtctx->socket, "host=%s", optarg);
			break;

		case 'r':		/* remote mode */
			mode_remote = 1;
			break;

		default:
			fprintf(stderr, "Try \"%s --help\" for more information.\n",
					progname);
			exit(1);
		}
	}
}

/*
 *	reset_display() - reset all the display routine pointers so that entire
 *	screen will get redrawn.
 */

void
reset_display()

{
	d_loadave = i_loadave;
	d_minibar = i_minibar;
	d_uptime = i_uptime;
	d_procstates = i_procstates;
	d_cpustates = i_cpustates;
	d_memory = i_memory;
	d_swap = i_swap;
	d_db = i_db;
	d_io = i_io;
	d_disk = i_disk;
	d_message = i_message;
	d_header = i_header;
	d_process = i_process;
}

/*
 *	signal handlers
 */

void
set_signal(int sig, RETSIGTYPE(*handler) (int))

{
#ifdef HAVE_SIGACTION
	struct sigaction action;

	action.sa_handler = handler;
	action.sa_flags = 0;
	(void) sigaction(sig, &action, NULL);
#else
				(void) signal(sig, handler);
#endif
}

RETSIGTYPE
leave(int i)					/* exit under normal conditions -- INT handler */

{
	end_screen();
	exit(0);
}

RETSIGTYPE
tstop(int i)					/* SIGTSTP handler */

{
#ifdef HAVE_SIGACTION
	sigset_t	set;
#endif

	/* move to the lower left */
	end_screen();
	fflush(stdout);

	/* default the signal handler action */
	set_signal(SIGTSTP, SIG_DFL);

	/* unblock the TSTP signal */
#ifdef HAVE_SIGACTION
	sigemptyset(&set);
	sigaddset(&set, SIGTSTP);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
#endif

#ifdef HAVE_SIGHOLD
	sigrelse(SIGTSTP);
#endif

#ifdef BSD_SIGNALS
	(void) sigsetmask(sigblock(0) & ~(sigmask(SIGTSTP)));
#endif

	/* send ourselves a TSTP to stop the process */
	(void) kill(0, SIGTSTP);

	/* reset the signal handler */
	set_signal(SIGTSTP, tstop);

	/* reinit screen */
	reinit_screen();

	/* jump to appropriate place */
	longjmp(jmp_int, 1);

	/* NOTREACHED */
}

#ifdef SIGWINCH
RETSIGTYPE
winch(int i)					/* SIGWINCH handler */

{
	/* reascertain the screen dimensions */
	get_screensize();

	/* tell display to resize */
	max_topn = display_resize();

#ifndef HAVE_SIGACTION
	/* reset the signal handler */
	set_signal(SIGWINCH, winch);
#endif

	/* jump to appropriate place */
	longjmp(jmp_int, 1);
}
#endif

void
quit(int status)				/* exit under duress */

{
	end_screen();
	exit(status);
	/* NOTREACHED */
}

int
main(int argc, char *argv[])
{
	register int i;
	struct pg_top_context pgtctx;

#ifdef BSD_SIGNALS
	int			old_sigmask;	/* only used for BSD-style signals */
#endif   /* BSD_SIGNALS */
	char	   *uname_field = "USERNAME";
	char	   *env_top;
	char	  **preset_argv;
	int			preset_argc = 0;
	char	  **av;
	int			ac;

#ifndef FD_SET
	/* FD_SET and friends are not present:	fake it */
	typedef int fd_set;

#define FD_ZERO(x)	   (*(x) = 0)
#define FD_SET(f, x)   (*(x) = 1<<f)
#endif

#ifdef HAVE_SIGPROCMASK
	sigset_t	signalset;
#endif

	/* initialize some selection options */
	memset(&pgtctx, 0, sizeof(struct pg_top_context));
#ifdef ENABLE_COLOR
	pgtctx.color_on = 1;
#endif
	pgtctx.dbport = 5432;
	pgtctx.delay = Default_DELAY;
	pgtctx.displays = 0; /* indicates unspecified */
	pgtctx.dostates = No;
	pgtctx.do_unames = Yes;
	pgtctx.get_userid = username;
	pgtctx.index_order_index = 0;
	pgtctx.interactive = Maybe;
	pgtctx.mode = MODE_PROCESSES;
	pgtctx.order_index = 0;
	pgtctx.ps.idle = Yes;
	pgtctx.ps.fullcmd = Yes;
	pgtctx.ps.uid = -1;
	pgtctx.ps.command = NULL;
	pgtctx.show_tags = No;
	pgtctx.topn = 0;

	/* Show help or version number if necessary */
    if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
		{
			usage(progname);
			exit(0);
		}
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
		{
			printf("pg_top %s\n", version_string());
			exit(0);
		}
	}

	/* set the buffer for stdout */
#ifdef HAVE_SETVBUF
    setvbuf(stdout, stdoutbuf, _IOFBF, BUFFERSIZE);
#else
#ifdef HAVE_SETBUFFER
	setbuffer(stdout, stdoutbuf, Buffersize);
#endif /* HAVE_SETBUFFER */
#endif /* HAVE_SETVBUF */

	/* Get default values from the environment. */
	env_top = getenv("PGDATABASE");
	if (env_top != NULL)
		sprintf(pgtctx.dbname, "dbname=%s", getenv("PGDATABASE"));

	env_top = getenv("PGHOST");
	if (env_top != NULL)
		sprintf(pgtctx.socket, "host=%s", getenv("PGHOST"));

	env_top = getenv("PGPORT");
	if (env_top != NULL)
		pgtctx.dbport = atoi(getenv("PGPORT"));

	env_top = getenv("PGUSER");
	if (env_top != NULL)
		sprintf(pgtctx.dbusername, "user=%s", getenv("PGUSER"));

	/* get our name */
	if (argc > 0)
	{
		if ((myname = strrchr(argv[0], '/')) == 0)
		{
			myname = argv[0];
		}
		else
		{
			myname++;
		}
	}

	/* get preset options from the environment */
	if ((env_top = getenv("PG_TOP")) != NULL)
	{
		av = preset_argv = argparse(env_top, &preset_argc);
		ac = preset_argc;

		/*
		 * set the dummy argument to an explanatory message, in case getopt
		 * encounters a bad argument
		 */
		preset_argv[0] = "while processing environment";
	}

	/* process options */
	do
	{
		/* if we're done doing the presets, then process the real arguments */
		if (preset_argc == 0)
		{
			ac = argc;
			av = argv;

			/* this should keep getopt happy... */
			optind = 1;
		}
		process_arguments(&pgtctx, ac, av);

		/* connect to the database */
		sprintf(pgtctx.conninfo, "port=%d %s %s %s %s",
				pgtctx.dbport, pgtctx.dbname, pgtctx.socket, pgtctx.dbusername,
				pgtctx.password);

		/* get count of top processes to display (if any) */
		if (optind < ac && *av[optind])
		{
			if ((i = atoiwi(av[optind])) == Invalid)
			{
				new_message(MT_standout | MT_delayed,
							" Process count not a number (ignored)");
			}
			else
			{
				pgtctx.topn = i;
			}
		}

		/* tricky:	remember old value of preset_argc & set preset_argc = 0 */
		i = preset_argc;
		preset_argc = 0;

		/* repeat only if we really did the preset arguments */
	} while (i != 0);

	/* set constants for username/uid display correctly */
	if (!pgtctx.do_unames)
	{
		uname_field = "   UID  ";
		pgtctx.get_userid = itoa7;
	}

	/*
	 * in order to support forward compatability, we have to ensure that the
	 * entire statics structure is set to a known value before we call
	 * machine_init.  This way fields that a module does not know about will
	 * retain their default values
	 */
	memzero((void *) &pgtctx.statics, sizeof(pgtctx.statics));
	pgtctx.statics.boottime = -1;

#ifdef ENABLE_COLOR
	/* If colour has been turned on read in the settings. */
	env_top = getenv("PG_TOPCOLOURS");
	if (!env_top)
	{
		env_top = getenv("PG_TOPCOLORS");
	}
	/* must do something about error messages */
	color_env_parse(env_top);
#endif

	/* call the platform-specific init */
	if (mode_remote == 0)
		i = machine_init(&pgtctx.statics);
	else
		i = machine_init_r(&pgtctx.statics, pgtctx.conninfo);

	if (i == -1)
		exit(1);

	/* determine sorting order index, if necessary */
	if (pgtctx.order_name != NULL)
	{
		if (pgtctx.statics.order_names == NULL)
		{
			new_message(MT_standout | MT_delayed,
						" This platform does not support arbitrary ordering");
		}
		else if ((pgtctx.order_index = string_index(pgtctx.order_name,
				pgtctx.statics.order_names)) == -1)
		{
			char	  **pp;

			fprintf(stderr, "%s: '%s' is not a recognized sorting order.\n",
					myname, pgtctx.order_name);
			fprintf(stderr, "\tTry one of these:");
			pp = pgtctx.statics.order_names;
			while (*pp != NULL)
			{
				fprintf(stderr, " %s", *pp++);
			}
			fputc('\n', stderr);
			exit(1);
		}
	}

#ifdef WITH_EXT
	/* initialize extensions */
	init_ext(&exts);
#endif

	/* initialize termcap */
	init_termcap(pgtctx.interactive);

	/* get the string to use for the process area header */
	if (mode_remote == 0)
		pgtctx.header_text = pgtctx.header_processes = format_header(uname_field);
	else
		pgtctx.header_text = pgtctx.header_processes = format_header_r(uname_field);

#ifdef ENABLE_COLOR
	/* Disable colours on non-smart terminals */
	if (!smart_terminal)
	{
		pgtctx.color_on = 0;
	}
#endif

	/* initialize display interface */
	if ((max_topn = display_init(&pgtctx.statics)) == -1)
	{
		fprintf(stderr, "%s: can't allocate sufficient memory\n", myname);
		exit(4);
	}

	/* handle request for color tags */
	if (pgtctx.show_tags)
	{
		color_dump(stdout);
		exit(0);
	}

	/*
	 * Set topn based on the current screensize when starting up if it was not
	 * specified on the command line.
	 */
	if (pgtctx.topn == 0)
	{
		get_screensize();
		pgtctx.topn = display_resize();
	}

	/* print warning if user requested more processes than we can display */
	if (pgtctx.topn > max_topn)
	{
		new_message(MT_standout | MT_delayed,
					" This terminal can only display %d processes.",
					max_topn);
	}

	/* set header display accordingly */
	display_header(pgtctx.topn > 0);

	/* determine interactive state */
	if (pgtctx.interactive == Maybe)
	{
		pgtctx.interactive = smart_terminal;
	}

	/* if # of displays not specified, fill it in */
	if (pgtctx.displays == 0)
	{
		pgtctx.displays = smart_terminal ? Infinity : 1;
	}

	/* hold interrupt signals while setting up the screen and the handlers */
#ifdef HAVE_SIGPROCMASK
	sigemptyset(&signalset);
	sigaddset(&signalset, SIGINT);
	sigaddset(&signalset, SIGQUIT);
	sigaddset(&signalset, SIGTSTP);
#ifdef SIGWINCH
	sigaddset(&signalset, SIGWINCH);
#endif
	sigprocmask(SIG_BLOCK, &signalset, NULL);
#endif

#ifdef HAVE_SIGHOLD
	sighold(SIGINT);
	sighold(SIGQUIT);
	sighold(SIGTSTP);
#ifdef SIGWINCH
	sighold(SIGWINCH);
#endif
#endif

#ifdef BSD_SIGNALS
#ifdef SIGWINCH
	old_sigmask = sigblock(sigmask(SIGINT) | sigmask(SIGQUIT) |
						   sigmask(SIGTSTP) | sigmask(SIGWINCH));
#else
	old_sigmask = sigblock(sigmask(SIGINT) | sigmask(SIGQUIT) |
						   sigmask(SIGTSTP));
#endif
#endif

	init_screen();
	(void) set_signal(SIGINT, leave);
	(void) set_signal(SIGQUIT, leave);
	(void) set_signal(SIGTSTP, tstop);
#ifdef SIGWINCH
	(void) set_signal(SIGWINCH, winch);
#endif

	/* setup the jump buffer for stops */
	if (setjmp(jmp_int) != 0)
	{
		/* control ends up here after an interrupt */
		reset_display();
	}

	/*
	 * Ready to release the signals.  This will also happen (needlessly) after
	 * a longjmp, but that's okay.
	 */
#ifdef HAVE_SIGPROCMASK
	sigprocmask(SIG_UNBLOCK, &signalset, NULL);
#endif

#ifdef HAVE_SIGHOLD
	sigrelse(SIGINT);
	sigrelse(SIGQUIT);
	sigrelse(SIGTSTP);
#ifdef SIGWINCH
	sigrelse(SIGWINCH);
#endif
#endif

#ifdef BSD_SIGNALS
	(void) sigsetmask(old_sigmask);
#endif

	/* some systems require a warmup */
	if (pgtctx.statics.flags.warmup)
	{
		if (mode_remote == 0)
		{
			get_system_info(&pgtctx.system_info);
#ifdef __linux__
			(void) get_process_info(&pgtctx.system_info, &pgtctx.ps, 0,
					pgtctx.conninfo, pgtctx.mode);
#else
			(void) get_process_info(&pgtctx.system_info, &pgtctx.ps, 0,
					pgtctx.conninfo);
#endif /* __linux__ */
		}
		else
		{
			get_system_info_r(&pgtctx.system_info, pgtctx.conninfo);
			(void) get_process_info_r(&pgtctx.system_info, &pgtctx.ps, 0,
					pgtctx.conninfo);
		}

		/* Get database activity information */
		get_database_info(&pgtctx.db_info, pgtctx.conninfo);

		/* Get database I/O information */
		get_io_info(&pgtctx.io_info);

		/* Get database disk information */
		get_disk_info(&pgtctx.disk_info, get_data_directory(pgtctx.conninfo));

		pgtctx.timeout.tv_sec = 1;
		pgtctx.timeout.tv_usec = 0;
		select(0, NULL, NULL, NULL, &pgtctx.timeout);

		/* if we've warmed up, then we can show good states too */
		pgtctx.dostates = Yes;
	}

	/*
	 * main loop -- repeat while display count is positive or while it
	 * indicates infinity (by being -1)
	 */

	while ((pgtctx.displays == -1) || (pgtctx.displays-- > 0))
	{
		do_display(&pgtctx);
	}

	quit(0);
	/* NOTREACHED */
	return 0;
}
