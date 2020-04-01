/*
 *	This file defines the interface between top and the machine-dependent
 *	module.  It is NOT machine dependent and should not need to be changed
 *	for any specific machine.
 *
 *	Copyright (c) 2007-2019, Mark Wong
 */

#ifndef _MACHINE_H_
#define _MACHINE_H_

#include <time.h>
#include <sys/types.h>

#include "pg.h"
#include "pg_config_manual.h"

/*
#ifdef CLK_TCK
#	define HZ CLK_TCK
# else
#	define HZ 60
# endif
*/

#ifndef HZ
#define HZ sysconf(_SC_CLK_TCK)
#endif

/* Display modes. */

enum DisplayMode
{
	MODE_PROCESSES,
	MODE_IO_STATS,
	MODE_REPLICATION,
	MODE_TYPES					/* number of modes */
};

/* Display modes for table and index statistics. */
#define STATS_DIFF 0
#define STATS_CUMULATIVE 1

/* Maximum number of columns allowed for display */
#define MAX_COLS	255

/*
 * The entire display is based on these next numbers being defined as is.
 */

#define NUM_AVERAGES	3

#define NPROCSTATES 7

/*
 * The statics struct is filled in by machine_init.  Fields marked as
 * "optional" are not filled in by every module.
 */
struct statics
{
	char	  **procstate_names;
	char	  **cpustate_names;
	char	  **memory_names;
	char	  **swap_names;		/* optional */
	char	  **order_names;	/* optional */
	char	  **color_names;	/* optional */
	time_t		boottime;		/* optional */
	int			ncpus;
	struct
	{
		unsigned int fullcmds:1;
		unsigned int idle:1;
		unsigned int warmup:1;
	}			flags;
};

/*
 * the system_info struct is filled in by a machine dependent routine.
 */

#ifdef p_active					/* uw7 define macro p_active */
#define P_ACTIVE p_pactive
#else
#define P_ACTIVE p_active
#endif

struct swap_t
{
	long	   *swap;

	unsigned long	prev_swapin;
	unsigned long	prev_swapout;
	unsigned long	swapin;
	unsigned long	swapout;
};

struct system_info
{
	int			last_pid;
	double		load_avg[NUM_AVERAGES];
	int			p_total;
	int			P_ACTIVE;		/* number of procs considered "active" */
	int		   *procstates;
	int64_t    *cpustates;
	long	   *memory;

	struct swap_t swap;
};

/* cpu_states is an array of percentages * 10.	For example,
   the (integer) value 105 is 10.5% (or .105).
 */

/*
 * the process_select struct tells get_process_info what processes we
 * are interested in seeing
 */

struct process_select
{
	int			idle;			/* show idle processes */
	int			fullcmd;		/* show full command */
	char	   *command;		/* only this command (unless == NULL) */
	char		usename[NAMEDATALEN + 1];	/* only this postgres usename */
};

/* routines defined by the machine dependent module */
int			machine_init(struct statics *);
void		get_system_info(struct system_info *);
#ifdef __linux__
caddr_t		get_process_info(struct system_info *, struct process_select *, int,
							 struct pg_conninfo_ctx *, int);
#else
caddr_t		get_process_info(struct system_info *, struct process_select *, int,
							 char *);
#endif							/* __linux__ */
char	   *format_header(char *);
char	   *format_next_io(caddr_t);
char	   *format_next_process(caddr_t);
char	   *format_next_replication(caddr_t);
uid_t		proc_owner(pid_t);
void		update_state(int *pgstate, char *state);
void		update_str(char **, char *);

extern int	mode_stats;

extern char *backendstatenames[];
extern char *procstatenames[];
extern char fmt_header_io[];
extern char fmt_header_replication[];

#endif							/* _MACHINE_H_ */
