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
 *	Copyright (c) 2007-2008, Mark Wong
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
#include "machine.h"
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
char	   *myname = "top";
jmp_buf		jmp_int;

/* pointers to display routines */
void		(*d_loadave) (int, double *) = i_loadave;
void		(*d_minibar) (
						  int (*) (char *, int)) = i_minibar;
void		(*d_uptime) (time_t *, time_t *) = i_uptime;
void		(*d_procstates) (int, int *) = i_procstates;
void		(*d_cpustates) (int64_t *) = i_cpustates;
void		(*d_memory) (long *) = i_memory;
void		(*d_swap) (long *) = i_swap;
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

RETSIGTYPE
onalrm(int i)					/* SIGALRM handler */

{
	/* this is only used in batch mode to break out of the pause() */
	/* return; */
}

int
main(int argc, char *argv[])

{
	register int i;
	register int active_procs;
	register int change;

	struct system_info system_info;
	struct statics statics;
	caddr_t		processes;

	static char tempbuf1[50];
	static char tempbuf2[50];

#ifdef BSD_SIGNALS
	int			old_sigmask;	/* only used for BSD-style signals */
#endif   /* BSD_SIGNALS */
	int			topn = 0;
	int			delay = Default_DELAY;
	int			displays = 0;	/* indicates unspecified */
	time_t		curr_time;
	char	   *(*get_userid) (uid_t) = username;
	char	   *uname_field = "USERNAME";
	char	   *header_text;
	char	   *env_top;
	char	  **preset_argv;
	int			preset_argc = 0;
	char	  **av;
	int			ac;
	char		dostates = No;
	char		do_unames = Yes;
	char		interactive = Maybe;
	char		show_tags = No;

	char		ch;
	char	   *iptr;
	char		no_command = 1;
	struct timeval timeout;
	struct process_select ps;
	char	   *order_name = NULL;
	int			order_index = 0;
	int			index_order_index = 0;
	int			io_order_index = 0;
	int			table_order_index = 0;

#ifndef FD_SET
	/* FD_SET and friends are not present:	fake it */
	typedef int fd_set;

#define FD_ZERO(x)	   (*(x) = 0)
#define FD_SET(f, x)   (*(x) = 1<<f)
#endif
	fd_set		readfds;
	static struct ext_decl exts = {NULL, NULL};

#ifdef ENABLE_COLOR
	int			color_on = 1;
#endif
#ifdef HAVE_SIGPROCMASK
	sigset_t	signalset;
#endif

	char		conninfo[4096];
	char		dbname[1024] = "";
	char		dbusername[1024] = "";
	char		password[1001] = "";
	char		socket[1024] = "";
	char	   *password_tmp;
	int			dbport = 5432;

	int			mode = MODE_PROCESSES;
	char	   *header_processes;
	char		header_index_stats[80] =
	"  I_SCANS   I_READS I_FETCHES INDEXRELNAME";
	char		header_table_stats[80] =
	"SEQ_SCANS SEQ_READS   I_SCANS I_FETCHES   INSERTS   UPDATES   DELETES RELNAME";
	char		header_io_stats[80] = "  PID RCHAR WCHAR   SYSCR   SYSCW READS WRITES CWRITES COMMAND";

	static char command_chars[] = "\f qh?en#sdkriIucoCNPMTQLERXAt";

/* these defines enumerate the "strchr"s of the commands in command_chars */
#define CMD_redraw	0
#define CMD_update	1
#define CMD_quit	2
#define CMD_help1	3
#define CMD_help2	4
#define CMD_OSLIMIT 4			/* terminals with OS can only handle commands */
#define CMD_errors	5			/* less than or equal to CMD_OSLIMIT	  */
#define CMD_number1 6
#define CMD_number2 7
#define CMD_delay	8
#define CMD_displays	9
#define CMD_kill	10
#define CMD_renice	11
#define CMD_idletog		12
#define CMD_io	13
#define CMD_user	14
#define CMD_cmdline		15
#define CMD_order		16
#define CMD_color		17
#define CMD_order_pid	18
#define CMD_order_cpu	19
#define CMD_order_mem	20
#define CMD_order_time	21
#define CMD_current_query 22
#define CMD_locks 23
#define CMD_explain 24
#define CMD_tables 25
#define CMD_indexes 26
#define CMD_explain_analyze 27
#define CMD_toggle 28

	/* set the buffer for stdout */
	setbuffer(stdout, stdoutbuf, Buffersize);

	/* Get default values from the environment. */
	env_top = getenv("PGDATABASE");
	if (env_top != NULL)
		sprintf(dbname, "dbname=%s", getenv("PGDATABASE"));

	env_top = getenv("PGHOST");
	if (env_top != NULL)
		sprintf(socket, "host=%s", getenv("PGHOST"));

	env_top = getenv("PGPORT");
	if (env_top != NULL)
		dbport = atoi(getenv("PGPORT"));

	env_top = getenv("PGUSER");
	if (env_top != NULL)
		sprintf(dbusername, "user=%s", getenv("PGUSER"));

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

	/* initialize some selection options */
	ps.idle = Yes;
	ps.fullcmd = Yes;
	ps.uid = -1;
	ps.command = NULL;

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

		while ((i = getopt(ac, av, "CDITbcinqruvh:s:d:U:o:Wp:x:z:")) != EOF)
		{
			switch (i)
			{
#ifdef ENABLE_COLOR
				case 'C':
					color_on = !color_on;
					break;
#endif

				case 'D':
					debug_set(1);
					break;

				case 'v':		/* show version number */
					fprintf(stderr, "%s: version %s\n",
							myname, version_string());
					exit(1);
					break;

				case 'u':		/* toggle uid/username display */
					do_unames = !do_unames;
					break;

				case 'z':		/* display only username's processes */
					if ((ps.uid = userid(optarg)) == -1)
					{
						fprintf(stderr, "%s: unknown user\n", optarg);
						exit(1);
					}
					break;

				case 'I':		/* show idle processes */
					ps.idle = !ps.idle;
					break;

				case 'T':		/* show color tags */
					show_tags = 1;
					break;

				case 'i':		/* go interactive regardless */
					interactive = Yes;
					break;

				case 'c':
					ps.fullcmd = No;
					break;

				case 'n':		/* batch, or non-interactive */
				case 'b':
					interactive = No;
					break;

				case 'x':		/* number of displays to show */
					if ((i = atoiwi(optarg)) == Invalid || i == 0)
					{
						new_message(MT_standout | MT_delayed,
									" Bad display count (ignored)");
					}
					else
					{
						displays = i;
					}
					break;

				case 's':
					if ((delay = atoi(optarg)) < 0 ||
							(delay == 0 && getuid() != 0))
					{
						new_message(MT_standout | MT_delayed,
									" Bad seconds delay (ignored)");
						delay = Default_DELAY;
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
					order_name = optarg;
					break;

				case 'p':		/* database port */
					if ((i = atoiwi(optarg)) == Invalid || i == 0)
					{
						new_message(MT_standout | MT_delayed,
									" Bad port number (ignored)");
					}
					else
					{
						dbport = i;
					}
					break;

				case 'W':		/* prompt for database password */
					password_tmp = simple_prompt("Password: ", 1000, 0);

					/*
					 * get the password in the format we want for the connect
					 * string
					 */
					sprintf(password, "password=%s", password_tmp);
					break;

				case 'U':		/* database user name */
					sprintf(dbusername, "user=%s", optarg);
					break;

				case 'd':		/* database name */
					sprintf(dbname, "dbname=%s", optarg);
					break;

				case 'h':		/* socket location */
					sprintf(socket, "host=%s", optarg);
					break;

				case 'r':		/* remote mode */
					mode_remote = 1;
					break;

				default:
					fprintf(stderr, "\
pg_top version %s\n\
Usage: %s [-ITWbcinqru] [-x x] [-s x] [-o field] [-z username]\n\
          [-p PORT] [-U USER] [-d DBNAME] [-h HOSTNAME] [number]\n",
							version_string(), myname);
					exit(1);
			}
		}

		/* connect to the database */
		sprintf(conninfo, "port=%d %s %s %s %s",
				dbport, dbname, socket, dbusername, password);

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
				topn = i;
			}
		}

		/* tricky:	remember old value of preset_argc & set preset_argc = 0 */
		i = preset_argc;
		preset_argc = 0;

		/* repeat only if we really did the preset arguments */
	} while (i != 0);

	/* set constants for username/uid display correctly */
	if (!do_unames)
	{
		uname_field = "   UID  ";
		get_userid = itoa7;
	}

	/*
	 * in order to support forward compatability, we have to ensure that the
	 * entire statics structure is set to a known value before we call
	 * machine_init.  This way fields that a module does not know about will
	 * retain their default values
	 */
	memzero((void *) &statics, sizeof(statics));
	statics.boottime = -1;

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
		i = machine_init(&statics);
	else
		i = machine_init_r(&statics, conninfo);

	if (i == -1)
		exit(1);

	/* determine sorting order index, if necessary */
	if (order_name != NULL)
	{
		if (statics.order_names == NULL)
		{
			new_message(MT_standout | MT_delayed,
						" This platform does not support arbitrary ordering");
		}
		else if ((order_index = string_index(order_name,
				statics.order_names)) == -1)
		{
			char	  **pp;

			fprintf(stderr, "%s: '%s' is not a recognized sorting order.\n",
					myname, order_name);
			fprintf(stderr, "\tTry one of these:");
			pp = statics.order_names;
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
	init_termcap(interactive);

	/* get the string to use for the process area header */
	if (mode_remote == 0)
		header_text = header_processes = format_header(uname_field);
	else
		header_text = header_processes = format_header_r(uname_field);

#ifdef ENABLE_COLOR
	/* Disable colours on non-smart terminals */
	if (!smart_terminal)
	{
		color_on = 0;
	}
#endif

	/* initialize display interface */
	if ((max_topn = display_init(&statics)) == -1)
	{
		fprintf(stderr, "%s: can't allocate sufficient memory\n", myname);
		exit(4);
	}

	/* handle request for color tags */
	if (show_tags)
	{
		color_dump(stdout);
		exit(0);
	}

	/*
	 * Set topn based on the current screensize when starting up if it was not
	 * specified on the command line.
	 */
	if (topn == 0)
	{
		get_screensize();
		topn = display_resize();
	}

	/* print warning if user requested more processes than we can display */
	if (topn > max_topn)
	{
		new_message(MT_standout | MT_delayed,
					" This terminal can only display %d processes.",
					max_topn);
	}

	/* set header display accordingly */
	display_header(topn > 0);

	/* determine interactive state */
	if (interactive == Maybe)
	{
		interactive = smart_terminal;
	}

	/* if # of displays not specified, fill it in */
	if (displays == 0)
	{
		displays = smart_terminal ? Infinity : 1;
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
	if (statics.flags.warmup)
	{
		if (mode_remote == 0)
		{
			get_system_info(&system_info);
			(void) get_process_info(&system_info, &ps, 0, conninfo, mode);
		}
		else
		{
			get_system_info_r(&system_info, conninfo);
			(void) get_process_info_r(&system_info, &ps, 0, conninfo);
		}
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		select(0, NULL, NULL, NULL, &timeout);

		/* if we've warmed up, then we can show good states too */
		dostates = Yes;
	}

	/*
	 * main loop -- repeat while display count is positive or while it
	 * indicates infinity (by being -1)
	 */

	while ((displays == -1) || (displays-- > 0))
	{
		int tmp_index = 0;
		switch (mode) {
		case MODE_IO_STATS:
			tmp_index = io_order_index;
			break;
		default:
			tmp_index = 0;
		}
		/* get the current stats and processes */
		if (mode_remote == 0)
		{
			get_system_info(&system_info);
			processes = get_process_info(&system_info, &ps, tmp_index,
					conninfo, mode);
		}
		else
		{
			get_system_info_r(&system_info, conninfo);
			processes = get_process_info_r(&system_info, &ps, order_index,
					conninfo);
		}

		/* display the load averages */
		(*d_loadave) (system_info.last_pid,
					  system_info.load_avg);

		/* this method of getting the time SHOULD be fairly portable */
		time(&curr_time);

		/* if we have a minibar extension, use it, otherwise show uptime */
		if (exts.f_minibar != NULL)
		{
			(*d_minibar) (exts.f_minibar);
		}
		else
		{
			(*d_uptime) (&statics.boottime, &curr_time);
		}

		/* display the current time */
		i_timeofday(&curr_time);

		/* display process state breakdown */
		(*d_procstates) (system_info.p_total,
						 system_info.procstates);

		/* display the cpu state percentage breakdown */
		if (dostates)			/* but not the first time */
		{
			(*d_cpustates) (system_info.cpustates);
		}
		else
		{
			/* we'll do it next time */
			if (smart_terminal)
			{
				z_cpustates();
			}
			dostates = Yes;
		}

		/* display memory stats */
		(*d_memory) (system_info.memory);

		/* display swap stats */
		(*d_swap) (system_info.swap);

		/* handle message area */
		(*d_message) ();

		/* update the header area */
		(*d_header) (header_text);

		if (topn > 0)
		{
			/* determine number of processes to actually display */

			/*
			 * this number will be the smallest of:  active processes, number
			 * user requested, number current screen accomodates
			 */
			active_procs = system_info.P_ACTIVE;
			if (active_procs > topn)
			{
				active_procs = topn;
			}
			if (active_procs > max_topn)
			{
				active_procs = max_topn;
			}

			/* Now show the top "n" processes or other statistics. */
			switch (mode)
			{
				case MODE_INDEX_STATS:
					pg_display_index_stats(conninfo, index_order_index,
							max_topn);
					break;
				case MODE_TABLE_STATS:
					pg_display_table_stats(conninfo, table_order_index,
							max_topn);
					break;
				case MODE_IO_STATS:
					for (i = 0; i < active_procs; i++)
					{
						if (mode_remote == 0)
							(*d_process) (i, format_next_io(processes,
									get_userid));
						else
							(*d_process) (i, format_next_io_r(processes));
					}
					break;
				case MODE_PROCESSES:
				default:
					for (i = 0; i < active_procs; i++)
					{
						if (mode_remote == 0)
							(*d_process) (i, format_next_process(processes,
									get_userid));
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
		if (displays)
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
					d_swap = u_swap;
					d_message = u_message;
					d_header = u_header;
					d_process = u_process;
				}
			}

			no_command = Yes;
			if (!interactive)
			{
				/* set up alarm */
				(void) signal(SIGALRM, onalrm);
				(void) alarm((unsigned) delay);

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
					timeout.tv_sec = delay;
					timeout.tv_usec = 0;

					/* wait for either input or the end of the delay period */
					if (select(32, &readfds, (fd_set *) NULL, (fd_set *) NULL,
							&timeout) > 0)
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
										show_help(&statics);
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
											else if (newval > topn && topn == 0)
											{
												/* redraw the header */
												display_header(Yes);
												d_header = i_header;
											}
											topn = newval;
										}
										break;

									case CMD_delay:		/* new seconds delay */
										new_message(MT_standout, "Seconds to delay: ");
										if ((i = readline(tempbuf1, 8, Yes)) > -1)
										{
											if ((delay = i) == 0 && getuid() != 0)
											{
												delay = 1;
											}
										}
										clear_message();
										break;

									case CMD_displays:	/* change display count */
										new_message(MT_standout,
										 "Displays to show (currently %s): ",
												displays == -1 ? "infinite" :
													itoa(displays));
										if ((i = readline(tempbuf1, 10, Yes)) > 0)
										{
										displays = i;
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
										ps.idle = !ps.idle;
										new_message(MT_standout | MT_delayed,
											  " %sisplaying idle processes.",
													ps.idle ? "D" : "Not d");
										putchar('\r');
										break;

									case CMD_cmdline:
										if (statics.flags.fullcmds)
										{
											ps.fullcmd = (ps.fullcmd + 1) % 3;
											switch (ps.fullcmd) {
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
										if (readline(tempbuf2, sizeof(tempbuf2), No) > 0)
										{
											if (tempbuf2[0] == '+' &&
												tempbuf2[1] == '\0')
											{
												ps.uid = -1;
											}
											else if ((i = userid(tempbuf2)) == -1)
											{
												new_message(MT_standout,
															" %s: unknown user", tempbuf2);
												no_command = Yes;
											}
											else
											{
												ps.uid = i;
											}
											putchar('\r');
										}
										else
										{
											clear_message();
										}
										break;

									case CMD_order:
										switch (mode)
										{
											case MODE_INDEX_STATS:
												new_message(MT_standout,
														  "Order to sort: ");
												if (readline(tempbuf2, sizeof(tempbuf2), No) > 0)
												{
													if ((i = string_index(tempbuf2, index_ordernames)) == -1)
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
												if (readline(tempbuf2, sizeof(tempbuf2), No) > 0)
												{
													if ((i = string_index(tempbuf2, table_ordernames)) == -1)
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
													if ((i = string_index(tempbuf2, statics.order_names_io)) == -1)
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
												if (statics.order_names == NULL)
												{
													new_message(MT_standout, " Ordering not supported.");
													putchar('\r');
													no_command = Yes;
												}
												else
												{
													new_message(MT_standout,
														  "Order to sort: ");
													if (readline(tempbuf2, sizeof(tempbuf2), No) > 0)
													{
														if ((i = string_index(tempbuf2, statics.order_names)) == -1)
														{
															new_message(MT_standout,
																		" %s: unrecognized sorting order", tempbuf2);
															no_command = Yes;
														}
														else
														{
															order_index = i;
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
										if ((i = string_index("pid", statics.order_names)) == -1)
										{
											new_message(MT_standout,
											  " Unrecognized sorting order");
											putchar('\r');
											no_command = Yes;
										}
										else
										{
											order_index = i;
										}
										break;
									case CMD_order_cpu:
										if ((i = string_index("cpu", statics.order_names)) == -1)
										{
											new_message(MT_standout,
											  " Unrecognized sorting order");
											putchar('\r');
											no_command = Yes;
										}
										else
										{
											order_index = i;
										}
										break;
									case CMD_order_mem:
										if ((i = string_index("size", statics.order_names)) == -1)
										{
											new_message(MT_standout,
											  " Unrecognized sorting order");
											putchar('\r');
											no_command = Yes;
										}
										else
										{
											order_index = i;
										}
										break;
									case CMD_order_time:
										if ((i = string_index("time", statics.order_names)) == -1)
										{
											new_message(MT_standout,
											  " Unrecognized sorting order");
											putchar('\r');
											no_command = Yes;
										}
										else
										{
											order_index = i;
										}
										break;

#ifdef ENABLE_COLOR
									case CMD_color:
										reset_display();
										if (color_on)
										{
											color_on = 0;
											display_resize();	/* To realloc screenbuf */
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
												color_on = 1;
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
										show_current_query(conninfo, newval);
										display_pagerend();
										break;

									case CMD_locks:
										new_message(MT_standout,
											 "Show locks held by process: ");
										newval = readline(tempbuf1, 8, Yes);
										reset_display();
										display_pagerstart();
										show_locks(conninfo, newval);
										display_pagerend();
										break;

									case CMD_explain:
										new_message(MT_standout,
												 "Re-determine execution plan: ");
										newval = readline(tempbuf1, 8, Yes);
										reset_display();
										display_pagerstart();
										show_explain(conninfo, newval, EXPLAIN);
										display_pagerend();
										break;

									case CMD_tables:
										if (mode == MODE_TABLE_STATS)
										{
											mode = MODE_PROCESSES;
											header_text = header_processes;
										}
										else
										{
											mode = MODE_TABLE_STATS;
											header_text = header_table_stats;
										}
										reset_display();
										break;

									case CMD_indexes:
										if (mode == MODE_INDEX_STATS)
										{
											mode = MODE_PROCESSES;
											header_text = header_processes;
										}
										else
										{
											mode = MODE_INDEX_STATS;
											header_text = header_index_stats;
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
										show_explain(conninfo, newval, EXPLAIN_ANALYZE);
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
										if (mode == MODE_IO_STATS)
										{
											mode = MODE_PROCESSES;
											header_text = header_processes;
										}
										else
										{
											mode = MODE_IO_STATS;
											header_text = header_io_stats;
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

	quit(0);
	/* NOTREACHED */
	return 0;
}
