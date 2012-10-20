/*
 * pg_top - a top PostgreSQL users display for Unix
 *
 * SYNOPSIS:  Linux 1.2.x, 1.3.x, 2.x, using the /proc filesystem
 *
 * DESCRIPTION:
 * This is the machine-dependent module for Linux 1.2.x, 1.3.x or 2.x.
 *
 * LIBS:
 *
 * CFLAGS: -DHAVE_GETOPT -DHAVE_STRERROR -DORDER
 *
 * TERMCAP: -lcurses
 *
 * AUTHOR: Richard Henderson <rth@tamu.edu>
 * Order support added by Alexey Klimkin <kad@klon.tme.mcst.ru>
 * Ported to 2.4 by William LeFebvre
 */

#include "config.h"

#include <sys/types.h>
#include <time.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/vfs.h>

#include <sys/param.h>			/* for HZ */

#if 0
#include <linux/proc_fs.h>		/* for PROC_SUPER_MAGIC */
#else
#define PROC_SUPER_MAGIC 0x9fa0
#endif

#define BUFFERLEN 255
#define GET_VALUE(v) \
		p = strchr(p, ':'); \
		++p; \
		++p; \
		q = strchr(p, '\n'); \
		len = q - p; \
		if (len >= BUFFERLEN) \
		{ \
			printf("ERROR - value is larger than the buffer: %d\n", __LINE__); \
			exit(1); \
		} \
		strncpy(value, p, len); \
		value[len] = '\0'; \
		v = atoll(value);

#include "machine.h"
#include "utils.h"

#define PROCFS "/proc"
extern char *myname;

/*=PROCESS INFORMATION==================================================*/

struct top_proc
{
	pid_t		pid;

	/* Data from /proc/<pid>/stat. */
	uid_t		uid;
	char	   *name;
	int			pri,
				nice;
	unsigned long size,
				rss;			/* in k */
	int			state;
	unsigned long time;
	unsigned long start_time;
	double		pcpu,
				wcpu;

	/* Data from /proc/<pid>/io. */
	long long rchar;
	long long wchar;
	long long syscr;
	long long syscw;
	long long read_bytes;
	long long write_bytes;
	long long cancelled_write_bytes;

	struct top_proc *next;
};

struct io_node
{
	pid_t pid;

	/* The change in the previous values and current values. */
	long long diff_rchar;
	long long diff_wchar;
	long long diff_syscr;
	long long diff_syscw;
	long long diff_read_bytes;
	long long diff_write_bytes;
	long long diff_cancelled_write_bytes;

	/* The previous values. */
	long long old_rchar;
	long long old_wchar;
	long long old_syscr;
	long long old_syscw;
	long long old_read_bytes;
	long long old_write_bytes;
	long long old_cancelled_write_bytes;

	struct io_node *next;
};

/*=STATE IDENT STRINGS==================================================*/

#define NPROCSTATES 7
static char *state_abbrev[NPROCSTATES + 1] =
{
	"", "run", "sleep", "disk", "zomb", "stop", "swap",
	NULL
};

static char *procstatenames[NPROCSTATES + 1] =
{
	"", " running, ", " sleeping, ", " uninterruptable, ",
	" zombie, ", " stopped, ", " swapping, ",
	NULL
};

#define NCPUSTATES 5
static char *cpustatenames[NCPUSTATES + 1] =
{
	"user", "nice", "system", "idle", "iowait",
	NULL
};
static int	show_iowait = 0;

#define MEMUSED    0
#define MEMFREE    1
#define MEMSHARED  2
#define MEMBUFFERS 3
#define MEMCACHED  4
#define NMEMSTATS  5
static char *memorynames[NMEMSTATS + 1] =
{
	"K used, ", "K free, ", "K shared, ", "K buffers, ", "K cached",
	NULL
};

#define SWAPUSED   0
#define SWAPFREE   1
#define SWAPCACHED 2
#define NSWAPSTATS 3
static char *swapnames[NSWAPSTATS + 1] =
{
	"K used, ", "K free, ", "K cached",
	NULL
};

static char fmt_header[] =
"  PID X        PRI NICE  SIZE   RES STATE   TIME   WCPU    CPU COMMAND";

/* these are names given to allowed sorting orders -- first is default */
static char *ordernames[] = {"cpu", "size", "res", "time", "command", NULL};

static char *ordernames_io[] = {
		"pid", "rchar", "wchar", "syscr", "syscw", "reads", "writes",
		"cwrites", "command", NULL
};

/* forward definitions for comparison functions */
int			compare_cpu();
int			compare_size();
int			compare_res();
int			compare_time();
int			compare_cmd();
int			compare_pid();
int			compare_rchar();
int			compare_wchar();
int			compare_syscr();
int			compare_syscw();
int			compare_reads();
int			compare_writes();
int			compare_cwrites();

int			(*proc_compares[]) () =
{
	compare_cpu,
	compare_size,
	compare_res,
	compare_time,
	compare_cmd,
	NULL
};

int			(*io_compares[]) () =
{
	compare_pid,
	compare_rchar,
	compare_wchar,
	compare_syscr,
	compare_syscw,
	compare_reads,
	compare_writes,
	compare_cwrites,
	compare_cmd,
	NULL
};

/*=SYSTEM STATE INFO====================================================*/

/* these are for calculating cpu state percentages */

static int64_t cp_time[NCPUSTATES];
static int64_t cp_old[NCPUSTATES];
static int64_t cp_diff[NCPUSTATES];

/* for calculating the exponential average */

static struct timeval lasttime;

/* these are for keeping track of processes */

#define HASH_SIZE		 (1003)
#define INITIAL_ACTIVE_SIZE  (256)
#define PROCBLOCK_SIZE		 (32)
static struct top_proc *ptable[HASH_SIZE];
static struct top_proc **pactive;
static struct top_proc **nextactive;
static unsigned int activesize = 0;
static time_t boottime = -1;

/* these are for passing data back to the machine independant portion */

static int64_t	cpu_states[NCPUSTATES];
static int	process_states[NPROCSTATES];
static long memory_stats[NMEMSTATS];
static long swap_stats[NSWAPSTATS];

/* usefull macros */
#define bytetok(x)	(((x) + 512) >> 10)
#define pagetok(x)	((x) * sysconf(_SC_PAGESIZE) >> 10)
#define HASH(x)		(((x) * 1686629713U) % HASH_SIZE)

/*======================================================================*/

struct io_node *
new_io_node(pid_t);

struct io_node *
get_io_stats(struct io_node *, pid_t);

struct io_node *
insert_io_stats(struct io_node *, struct io_node *);

void
update_io_stats(struct io_node *, pid_t, long long, long long, long long,
	long long, long long, long long, long long);

struct io_node *
upsert_io_stats(struct io_node *, pid_t, long long, long long, long long,
	long long, long long, long long, long long);

static inline char *
skip_ws(const char *p)
{
	while (isspace(*p))
		p++;
	return (char *) p;
}

static inline char *
skip_token(const char *p)
{
	while (isspace(*p))
		p++;
	while (*p && !isspace(*p))
		p++;
	return (char *) p;
}

static void
xfrm_cmdline(char *p, int len)
{
	while (--len > 0)
	{
		if (*p == '\0')
		{
			*p = ' ';
		}
		p++;
	}
}

static void
update_procname(struct top_proc * proc, char *cmd)

{
	printable(cmd);

	if (proc->name == NULL)
	{
		proc->name = strdup(cmd);
	}
	else if (strcmp(proc->name, cmd) != 0)
	{
		free(proc->name);
		proc->name = strdup(cmd);
	}
}

/*
 * Process structures are allocated and freed as needed.  Here we
 * keep big pools of them, adding more pool as needed.	When a
 * top_proc structure is freed, it is added to a freelist and reused.
 */

static struct top_proc *freelist = NULL;
static struct top_proc *procblock = NULL;
static struct top_proc *procmax = NULL;

static struct top_proc *
new_proc()
{
	struct top_proc *p;

	if (freelist)
	{
		p = freelist;
		freelist = freelist->next;
	}
	else if (procblock)
	{
		p = procblock;
		if (++procblock >= procmax)
		{
			procblock = NULL;
		}
	}
	else
	{
		p = procblock = (struct top_proc *) calloc(PROCBLOCK_SIZE,
												   sizeof(struct top_proc));
		procmax = procblock++ + PROCBLOCK_SIZE;
	}

	/* initialization */
	if (p->name != NULL)
	{
		free(p->name);
		p->name = NULL;
	}

	return p;
}

static void
free_proc(struct top_proc * proc)
{
	proc->next = freelist;
	freelist = proc;
}


int
machine_init(struct statics * statics)

{
	/* make sure the proc filesystem is mounted */
	{
		struct statfs sb;

		if (statfs(PROCFS, &sb) < 0 || sb.f_type != PROC_SUPER_MAGIC)
		{
			fprintf(stderr, "%s: proc filesystem not mounted on " PROCFS "\n",
					myname);
			return -1;
		}
	}

	/* chdir to the proc filesystem to make things easier */
	chdir(PROCFS);

	/* a few preliminary checks */
	{
		int			fd;
		char		buff[128];
		char	   *p;
		int			cnt;
		unsigned long uptime;
		struct timeval tv;

		/* get a boottime */
		if ((fd = open("uptime", 0)) != -1)
		{
			if (read(fd, buff, sizeof(buff)) > 0)
			{
				uptime = strtoul(buff, &p, 10);
				gettimeofday(&tv, 0);
				boottime = tv.tv_sec - uptime;
			}
			close(fd);
		}

		/* see how many states we get from stat */
		if ((fd = open("stat", 0)) != -1)
		{
			if (read(fd, buff, sizeof(buff)) > 0)
			{
				if ((p = strchr(buff, '\n')) != NULL)
				{
					*p = '\0';
					p = buff;
					cnt = 0;
					while (*p != '\0')
					{
						if (*p++ == ' ')
						{
							cnt++;
						}
					}
				}
			}

			close(fd);
		}
		if (cnt > 5)
		{
			/* we have iowait */
			show_iowait = 1;
		}
	}

	/* if we aren't showing iowait, then we have to tweak cpustatenames */
	if (!show_iowait)
	{
		cpustatenames[4] = NULL;
	}

	/* fill in the statics information */
	statics->procstate_names = procstatenames;
	statics->cpustate_names = cpustatenames;
	statics->memory_names = memorynames;
	statics->swap_names = swapnames;
	statics->order_names = ordernames;
	statics->order_names_io = ordernames_io;
	statics->boottime = boottime;
	statics->flags.fullcmds = 1;
	statics->flags.warmup = 1;

	/* allocate needed space */
	pactive = (struct top_proc **) malloc(sizeof(struct top_proc *) * INITIAL_ACTIVE_SIZE);
	activesize = INITIAL_ACTIVE_SIZE;

	/* make sure the hash table is empty */
	memset(ptable, 0, HASH_SIZE * sizeof(struct top_proc *));

	/* all done! */
	return 0;
}


void
get_system_info(struct system_info * info)

{
	char		buffer[4096 + 1];
	int			fd,
				len;
	char	   *p;

	/* get load averages */

	if ((fd = open("loadavg", O_RDONLY)) != -1)
	{
		if ((len = read(fd, buffer, sizeof(buffer) - 1)) > 0)
		{
			buffer[len] = '\0';
			info->load_avg[0] = strtod(buffer, &p);
			info->load_avg[1] = strtod(p, &p);
			info->load_avg[2] = strtod(p, &p);
			p = skip_token(p);	/* skip running/tasks */
			p = skip_ws(p);
			if (*p)
			{
				info->last_pid = atoi(p);
			}
			else
			{
				info->last_pid = -1;
			}
		}
		close(fd);
	}

	/* get the cpu time info */
	if ((fd = open("stat", O_RDONLY)) != -1)
	{
		if ((len = read(fd, buffer, sizeof(buffer) - 1)) > 0)
		{
			buffer[len] = '\0';
			p = skip_token(buffer);		/* "cpu" */
			cp_time[0] = strtoul(p, &p, 0);
			cp_time[1] = strtoul(p, &p, 0);
			cp_time[2] = strtoul(p, &p, 0);
			cp_time[3] = strtoul(p, &p, 0);
			if (show_iowait)
			{
				cp_time[4] = strtoul(p, &p, 0);
			}

			/* convert cp_time counts to percentages */
			percentages(NCPUSTATES, cpu_states, cp_time, cp_old, cp_diff);
		}
		close(fd);
	}

	/* get system wide memory usage */
	if ((fd = open("meminfo", O_RDONLY)) != -1)
	{
		char	   *p;
		int			mem = 0;
		int			swap = 0;
		unsigned long memtotal = 0;
		unsigned long memfree = 0;
		unsigned long swaptotal = 0;

		if ((len = read(fd, buffer, sizeof(buffer) - 1)) > 0)
		{
			buffer[len] = '\0';
			p = buffer - 1;

			/* iterate thru the lines */
			while (p != NULL)
			{
				p++;
				if (p[0] == ' ' || p[0] == '\t')
				{
					/* skip */
				}
				else if (strncmp(p, "Mem:", 4) == 0)
				{
					p = skip_token(p);	/* "Mem:" */
					p = skip_token(p);	/* total memory */
					memory_stats[MEMUSED] = strtoul(p, &p, 10);
					memory_stats[MEMFREE] = strtoul(p, &p, 10);
					memory_stats[MEMSHARED] = strtoul(p, &p, 10);
					memory_stats[MEMBUFFERS] = strtoul(p, &p, 10);
					memory_stats[MEMCACHED] = strtoul(p, &p, 10);
					memory_stats[MEMUSED] = bytetok(memory_stats[MEMUSED]);
					memory_stats[MEMFREE] = bytetok(memory_stats[MEMFREE]);
					memory_stats[MEMSHARED] = bytetok(memory_stats[MEMSHARED]);
					memory_stats[MEMBUFFERS] =
							bytetok(memory_stats[MEMBUFFERS]);
					memory_stats[MEMCACHED] = bytetok(memory_stats[MEMCACHED]);
					mem = 1;
				}
				else if (strncmp(p, "Swap:", 5) == 0)
				{
					p = skip_token(p);	/* "Swap:" */
					p = skip_token(p);	/* total swap */
					swap_stats[SWAPUSED] = strtoul(p, &p, 10);
					swap_stats[SWAPFREE] = strtoul(p, &p, 10);
					swap_stats[SWAPUSED] = bytetok(swap_stats[SWAPUSED]);
					swap_stats[SWAPFREE] = bytetok(swap_stats[SWAPFREE]);
					swap = 1;
				}
				else if (!mem && strncmp(p, "MemTotal:", 9) == 0)
				{
					p = skip_token(p);
					memtotal = strtoul(p, &p, 10);
				}
				else if (!mem && memtotal > 0 && strncmp(p, "MemFree:", 8) == 0)
				{
					p = skip_token(p);
					memfree = strtoul(p, &p, 10);
					memory_stats[MEMUSED] = memtotal - memfree;
					memory_stats[MEMFREE] = memfree;
				}
				else if (!mem && strncmp(p, "MemShared:", 10) == 0)
				{
					p = skip_token(p);
					memory_stats[MEMSHARED] = strtoul(p, &p, 10);
				}
				else if (!mem && strncmp(p, "Buffers:", 8) == 0)
				{
					p = skip_token(p);
					memory_stats[MEMBUFFERS] = strtoul(p, &p, 10);
				}
				else if (!mem && strncmp(p, "Cached:", 7) == 0)
				{
					p = skip_token(p);
					memory_stats[MEMCACHED] = strtoul(p, &p, 10);
				}
				else if (!swap && strncmp(p, "SwapTotal:", 10) == 0)
				{
					p = skip_token(p);
					swaptotal = strtoul(p, &p, 10);
				}
				else if (!swap && swaptotal > 0 && strncmp(p, "SwapFree:", 9) == 0)
				{
					p = skip_token(p);
					memfree = strtoul(p, &p, 10);
					swap_stats[SWAPUSED] = swaptotal - memfree;
					swap_stats[SWAPFREE] = memfree;
				}
				else if (!mem && strncmp(p, "SwapCached:", 11) == 0)
				{
					p = skip_token(p);
					swap_stats[SWAPCACHED] = strtoul(p, &p, 10);
				}

				/* move to the next line */
				p = strchr(p, '\n');
			}
		}
		close(fd);
	}

	/* set arrays and strings */
	info->cpustates = cpu_states;
	info->memory = memory_stats;
	info->swap = swap_stats;
}


static void
read_one_proc_stat(pid_t pid, struct top_proc * proc, struct process_select * sel)
{
	char		buffer[4096],
			   *p,
			   *q;
	int			fd,
				len;
	int			fullcmd;
	char		value[BUFFERLEN + 1];

	/* if anything goes wrong, we return with proc->state == 0 */
	proc->state = 0;

	/* full cmd handling */
	fullcmd = sel->fullcmd;
	if (fullcmd == 1)
	{
		sprintf(buffer, "%d/cmdline", pid);
		if ((fd = open(buffer, O_RDONLY)) != -1)
		{
			/* read command line data */
			/* (theres no sense in reading more than we can fit) */
			if ((len = read(fd, buffer, MAX_COLS)) > 1)
			{
				buffer[len] = '\0';
				xfrm_cmdline(buffer, len);
				update_procname(proc, buffer);
			}
			else
			{
				fullcmd = 0;
			}
			close(fd);
		}
		else
		{
			fullcmd = 0;
		}
	}

	/* grab the proc stat info in one go */
	sprintf(buffer, "%d/stat", pid);

	fd = open(buffer, O_RDONLY);
	len = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);

	buffer[len] = '\0';

	proc->uid = (uid_t) proc_owner((int) pid);

	/* parse out the status, described in 'man proc' */

	/* skip pid and locate command, which is in parentheses */
	if ((p = strchr(buffer, '(')) == NULL)
	{
		return;
	}
	if ((q = strrchr(++p, ')')) == NULL)
	{
		return;
	}

	/* set the procname */
	*q = '\0';
	if (!fullcmd)
	{
		update_procname(proc, p);
	}

	/* scan the rest of the line */
	p = q + 1;
	p = skip_ws(p);
	switch (*p++)				/* state */
	{
		case 'R':
			proc->state = 1;
			break;
		case 'S':
			proc->state = 2;
			break;
		case 'D':
			proc->state = 3;
			break;
		case 'Z':
			proc->state = 4;
			break;
		case 'T':
			proc->state = 5;
			break;
		case 'W':
			proc->state = 6;
			break;
		case '\0':
			return;
	}

	p = skip_token(p);			/* skip ppid */
	p = skip_token(p);			/* skip pgrp */
	p = skip_token(p);			/* skip session */
	p = skip_token(p);			/* skip tty nr */
	p = skip_token(p);			/* skip tty pgrp */
	p = skip_token(p);			/* skip flags */
	p = skip_token(p);			/* skip min flt */
	p = skip_token(p);			/* skip cmin flt */
	p = skip_token(p);			/* skip maj flt */
	p = skip_token(p);			/* skip cmaj flt */

	proc->time = strtoul(p, &p, 10);	/* utime */
	proc->time += strtoul(p, &p, 10);	/* stime */

	p = skip_token(p);			/* skip cutime */
	p = skip_token(p);			/* skip cstime */

	proc->pri = strtol(p, &p, 10);		/* priority */
	proc->nice = strtol(p, &p, 10);		/* nice */
	p = skip_token(p);			/* skip num_threads */
	p = skip_token(p);			/* skip itrealvalue, 0 */
	proc->start_time = strtoul(p, &p, 10);		/* start_time */
	proc->size = bytetok(strtoul(p, &p, 10));	/* vsize */
	proc->rss = pagetok(strtoul(p, &p, 10));	/* rss */


#if 0
	/* for the record, here are the rest of the fields */
	p = skip_token(p);			/* skip rlim */
	p = skip_token(p);			/* skip start_code */
	p = skip_token(p);			/* skip end_code */
	p = skip_token(p);			/* skip start_stack */
	p = skip_token(p);			/* skip esp */
	p = skip_token(p);			/* skip eip */
	p = skip_token(p);			/* skip signal */
	p = skip_token(p);			/* skip sigblocked */
	p = skip_token(p);			/* skip sigignore */
	p = skip_token(p);			/* skip sigcatch */
	p = skip_token(p);			/* skip wchan */
	p = skip_token(p);			/* skip nswap, not maintained */
	p = skip_token(p);			/* exit signal */
	p = skip_token(p);			/* processor */
	p = skip_token(p);			/* rt_priority */
	p = skip_token(p);			/* policy */
	p = skip_token(p);			/* delayacct_blkio_ticks */
#endif

	/* Get the io stats. */
	sprintf(buffer, "%d/io", pid);
	fd = open(buffer, O_RDONLY);
	if (fd == -1)
	{
		/*
		 * CONFIG_TASK_IO_ACCOUNTING is not enabled in the Linux kernel or
		 * this version of Linux may not support collecting i/o statistics
		 * per pid.  Report 0's.
		 */
		proc->rchar = 0;
		proc->wchar = 0;
		proc->syscr = 0;
		proc->syscw = 0;
		proc->read_bytes = 0;
		proc->write_bytes = 0;
		proc->cancelled_write_bytes = 0;
		return;
	}
	len = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);

	buffer[len] = '\0';
	p = buffer;
	GET_VALUE(proc->rchar);
	GET_VALUE(proc->wchar);
	GET_VALUE(proc->syscr);
	GET_VALUE(proc->syscw);
	GET_VALUE(proc->read_bytes);
	GET_VALUE(proc->write_bytes);
	GET_VALUE(proc->cancelled_write_bytes);
}


caddr_t
get_process_info(struct system_info * si,
				 struct process_select * sel,
				 int compare_index, char *conninfo, int mode)
{
	struct timeval thistime;
	double		timediff,
				alpha,
				beta;
	struct top_proc *proc;
	pid_t		pid;
	unsigned long now;
	unsigned long elapsed;
	int			i;

	/* calculate the time difference since our last check */
	gettimeofday(&thistime, 0);
	if (lasttime.tv_sec)
	{
		timediff = ((thistime.tv_sec - lasttime.tv_sec) +
					(thistime.tv_usec - lasttime.tv_usec) * 1e-6);
	}
	else
	{
		timediff = 0;
	}
	lasttime = thistime;

	/* round current time to a second */
	now = (unsigned long) thistime.tv_sec;
	if (thistime.tv_usec >= 500000)
	{
		now++;
	}

	/* calculate constants for the exponental average */
	if (timediff > 0.0 && timediff < 30.0)
	{
		alpha = 0.5 * (timediff / 30.0);
		beta = 1.0 - alpha;
	}
	else
	{
		alpha = beta = 0.5;
	}
	timediff *= HZ;				/* convert to ticks */

	/* mark all hash table entries as not seen */
	for (i = 0; i < HASH_SIZE; ++i)
	{
		for (proc = ptable[i]; proc; proc = proc->next)
		{
			proc->state = 0;
		}
	}

	/* read the process information */
	{
		int			total_procs = 0;
		struct top_proc **active;

		int			show_idle = sel->idle;
		int			show_uid = sel->uid != -1;

		int			i;
		int			rows;
		PGconn	   *pgconn;
		PGresult   *pgresult = NULL;

		memset(process_states, 0, sizeof(process_states));

		pgconn = connect_to_db(conninfo);
		if (pgconn != NULL)
		{
			pgresult = pg_processes(pgconn);
			rows = PQntuples(pgresult);
		}
		else
		{
			rows = 0;
		}
		for (i = 0; i < rows; i++)
		{
			char	   *procpid = PQgetvalue(pgresult, i, 0);
			struct top_proc *pp;
			unsigned long otime;

			pid = atoi(procpid);

			/* look up hash table entry */
			proc = pp = ptable[HASH(pid)];
			while (proc && proc->pid != pid)
			{
				proc = proc->next;
			}

			/* if we came up empty, create a new entry */
			if (proc == NULL)
			{
				proc = new_proc();
				proc->pid = pid;
				proc->next = pp;
				ptable[HASH(pid)] = proc;
				proc->time = 0;
				proc->wcpu = 0;
			}

			otime = proc->time;

			read_one_proc_stat(pid, proc, sel);
			if (sel->fullcmd == 2)
				update_procname(proc, PQgetvalue(pgresult, i, 1));

			if (proc->state == 0)
				continue;

			total_procs++;
			process_states[proc->state]++;

			if (timediff > 0.0)
			{
				if ((proc->pcpu = (proc->time - otime) / timediff) < 0.0001)
				{
					proc->pcpu = 0;
				}
				proc->wcpu = proc->pcpu * alpha + proc->wcpu * beta;
			}
			else if ((elapsed = (now - boottime) * HZ - proc->start_time) > 0)
			{
				/*
				 * What's with the noop statement? if ((proc->pcpu =
				 * (double)proc->time / (double)elapsed) < 0.0001) {
				 * proc->pcpu; }
				 */
				proc->wcpu = proc->pcpu;
			}
			else
			{
				proc->wcpu = proc->pcpu = 0.0;
			}
		}
		if (pgresult != NULL)
			PQclear(pgresult);
		PQfinish(pgconn);

		/* make sure we have enough slots for the active procs */
		if (activesize < total_procs)
		{
			pactive = (struct top_proc **) realloc(pactive,
									sizeof(struct top_proc *) * total_procs);
			activesize = total_procs;
		}

		/* set up the active procs and flush dead entries */
		active = pactive;
		for (i = 0; i < HASH_SIZE; i++)
		{
			struct top_proc *last;
			struct top_proc *ptmp;

			last = NULL;
			proc = ptable[i];
			while (proc != NULL)
			{
				if (proc->state == 0)
				{
					ptmp = proc;
					if (last)
					{
						proc = last->next = proc->next;
					}
					else
					{
						proc = ptable[i] = proc->next;
					}
					free_proc(ptmp);
				}
				else
				{
					if ((show_idle || proc->state == 1 || proc->pcpu) &&
						(!show_uid || proc->uid == sel->uid))
					{
						*active++ = proc;
						last = proc;
					}
					proc = proc->next;
				}
			}
		}

		si->p_active = active - pactive;
		si->p_total = total_procs;
		si->procstates = process_states;
	}

	/* if requested, sort the "active" procs */
	if (si->p_active) {
		if (mode == MODE_IO_STATS) {
			qsort(pactive, si->p_active, sizeof(struct top_proc *),
			  		io_compares[compare_index]);
		}
		else
		{
			qsort(pactive, si->p_active, sizeof(struct top_proc *),
			  		proc_compares[compare_index]);
		}
	}

	/* don't even pretend that the return value thing here isn't bogus */
	nextactive = pactive;
	return (caddr_t) 0;
}


char *
format_header(char *uname_field)

{
	int			uname_len = strlen(uname_field);

	if (uname_len > 8)
		uname_len = 8;

	memcpy(strchr(fmt_header, 'X'), uname_field, uname_len);

	return fmt_header;
}


char *
format_next_io(caddr_t handle, char *(*get_userid) (uid_t))
{
	static char fmt[MAX_COLS];	/* static area where result is built */
	static struct io_node *head = NULL;
	struct top_proc *p = *nextactive++;
	struct io_node *node = NULL;

	head = upsert_io_stats(head, p->pid, p->rchar, p->wchar, p->syscr,
			p->syscw, p->read_bytes, p->write_bytes, p->cancelled_write_bytes);

	if (mode_stats == STATS_DIFF)
	{
		node = get_io_stats(head, p->pid);
		if (node == NULL)
		{
			snprintf(fmt, sizeof(fmt), "%5d %5d %5d %7d %7d %5d %6d %7d %s",
					p->pid, 0, 0, 0, 0, 0, 0, 0, p->name);
		}
		else
		{
			snprintf(fmt, sizeof(fmt),
					"%5d %5s %5s %7lld %7lld %5s %6s %7s %s",
					p->pid,
					format_b(node->diff_rchar),
					format_b(node->diff_wchar),
					node->diff_syscr,
					node->diff_syscw,
					format_b(node->diff_read_bytes),
					format_b(node->diff_write_bytes),
					format_b(node->diff_cancelled_write_bytes),
					p->name);
		}
	}
	else
	{
		snprintf(fmt, sizeof(fmt),
				"%5d %5s %5s %7lld %7lld %5s %6s %7s %s",
				p->pid,
				format_b(p->rchar),
				format_b(p->wchar),
				p->syscr,
				p->syscw,
				format_b(p->read_bytes),
				format_b(p->write_bytes),
				format_b(p->cancelled_write_bytes),
				p->name);
	}

	return (fmt);
}

char *
format_next_process(caddr_t handle, char *(*get_userid) (uid_t))
{
	static char fmt[MAX_COLS];	/* static area where result is built */
	struct top_proc *p = *nextactive++;

	snprintf(fmt, sizeof(fmt),
			 "%5d %-8.8s %3d %4d %5s %5s %-5s %6s %5.2f%% %5.2f%% %s",
			 p->pid,
			 (*get_userid) (p->uid),
			 p->pri < -99 ? -99 : p->pri,
			 p->nice,
			 format_k(p->size),
			 format_k(p->rss),
			 state_abbrev[p->state],
			 format_time(p->time / HZ),
			 p->wcpu * 100.0,
			 p->pcpu * 100.0,
			 p->name);

	/* return the result */
	return (fmt);
}

/* comparison routines for qsort */

/*
 * There are currently four possible comparison routines.  main selects
 * one of these by indexing in to the array proc_compares.
 *
 * Possible keys are defined as macros below.  Currently these keys are
 * defined:  percent cpu, cpu ticks, process state, resident set size,
 * total virtual memory usage.	The process states are ordered as follows
 * (from least to most important):	WAIT, zombie, sleep, stop, start, run.
 * The array declaration below maps a process state index into a number
 * that reflects this ordering.
 */

/* First, the possible comparison keys.  These are defined in such a way
   that they can be merely listed in the source code to define the actual
   desired ordering.
 */

#define ORDERKEY_PCTCPU  if (dresult = p2->pcpu - p1->pcpu,\
			 (result = dresult > 0.0 ? 1 : dresult < 0.0 ? -1 : 0) == 0)
#define ORDERKEY_CPTICKS if ((result = (long)p2->time - (long)p1->time) == 0)
#define ORDERKEY_STATE	 if ((result = (sort_state[p2->state] - \
			 sort_state[p1->state])) == 0)
#define ORDERKEY_PRIO	 if ((result = p2->pri - p1->pri) == 0)
#define ORDERKEY_RSSIZE  if ((result = p2->rss - p1->rss) == 0)
#define ORDERKEY_MEM	 if ((result = p2->size - p1->size) == 0)
#define ORDERKEY_NAME	 if ((result = strcmp(p1->name, p2->name)) == 0)
#define ORDERKEY_PID	 if ((result = p1->pid - p2->pid) == 0)
#define ORDERKEY_RCHAR	 if ((result = p1->rchar - p2->rchar) == 0)
#define ORDERKEY_WCHAR	 if ((result = p1->wchar - p2->wchar) == 0)
#define ORDERKEY_SYSCR	 if ((result = p1->syscr - p2->syscr) == 0)
#define ORDERKEY_SYSCW	 if ((result = p1->syscw - p2->syscw) == 0)
#define ORDERKEY_READS	 if ((result = p1->read_bytes - p2->read_bytes) == 0)
#define ORDERKEY_WRITES	 if ((result = p1->write_bytes - p2->write_bytes) == 0)
#define ORDERKEY_CWRITES if ((result = p1->cancelled_write_bytes - p2->cancelled_write_bytes) == 0)

/* Now the array that maps process state to a weight */

unsigned char sort_state[] =
{
	0,							/* empty */
	6,							/* run */
	3,							/* sleep */
	5,							/* disk wait */
	1,							/* zombie */
	2,							/* stop */
	4							/* swap */
};


/* compare_cpu - the comparison function for sorting by cpu percentage */

int
compare_cpu(
			struct top_proc ** pp1,
			struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;
	double		dresult;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_PCTCPU
		ORDERKEY_CPTICKS
		ORDERKEY_STATE
		ORDERKEY_PRIO
		ORDERKEY_RSSIZE
		ORDERKEY_MEM
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}

/* compare_size - the comparison function for sorting by total memory usage */

int
compare_size(
			 struct top_proc ** pp1,
			 struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;
	double		dresult;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_MEM
		ORDERKEY_RSSIZE
		ORDERKEY_PCTCPU
		ORDERKEY_CPTICKS
		ORDERKEY_STATE
		ORDERKEY_PRIO
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}

/* compare_res - the comparison function for sorting by resident set size */

int
compare_res(
			struct top_proc ** pp1,
			struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;
	double		dresult;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_RSSIZE
		ORDERKEY_MEM
		ORDERKEY_PCTCPU
		ORDERKEY_CPTICKS
		ORDERKEY_STATE
		ORDERKEY_PRIO
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}

/* compare_time - the comparison function for sorting by total cpu time */

int
compare_time(
			 struct top_proc ** pp1,
			 struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;
	double		dresult;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_CPTICKS
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		ORDERKEY_PRIO
		ORDERKEY_MEM
		ORDERKEY_RSSIZE
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}


/* compare_cmd - the comparison function for sorting by command name */

int
compare_cmd(
			struct top_proc ** pp1,
			struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;
	double		dresult;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_NAME
		ORDERKEY_PCTCPU
		ORDERKEY_CPTICKS
		ORDERKEY_STATE
		ORDERKEY_PRIO
		ORDERKEY_RSSIZE
		ORDERKEY_MEM
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}

int	
compare_pid(struct top_proc ** pp1, struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_PID
		ORDERKEY_RCHAR
		ORDERKEY_WCHAR
		ORDERKEY_SYSCR
		ORDERKEY_SYSCW
		ORDERKEY_READS
		ORDERKEY_WRITES
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}

int
compare_rchar(struct top_proc ** pp1, struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_RCHAR
		ORDERKEY_PID
		ORDERKEY_WCHAR
		ORDERKEY_SYSCR
		ORDERKEY_SYSCW
		ORDERKEY_READS
		ORDERKEY_WRITES
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}

int
compare_wchar(struct top_proc ** pp1, struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_WCHAR
		ORDERKEY_PID
		ORDERKEY_RCHAR
		ORDERKEY_SYSCR
		ORDERKEY_SYSCW
		ORDERKEY_READS
		ORDERKEY_WRITES
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}

int
compare_syscr(struct top_proc ** pp1, struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_SYSCR
		ORDERKEY_PID
		ORDERKEY_RCHAR
		ORDERKEY_WCHAR
		ORDERKEY_SYSCW
		ORDERKEY_READS
		ORDERKEY_WRITES
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}

int
compare_syscw(struct top_proc ** pp1, struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_SYSCW
		ORDERKEY_PID
		ORDERKEY_RCHAR
		ORDERKEY_WCHAR
		ORDERKEY_SYSCR
		ORDERKEY_READS
		ORDERKEY_WRITES
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}

int
compare_reads(struct top_proc ** pp1, struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_READS
		ORDERKEY_PID
		ORDERKEY_RCHAR
		ORDERKEY_WCHAR
		ORDERKEY_SYSCR
		ORDERKEY_SYSCW
		ORDERKEY_WRITES
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}

int
compare_writes(struct top_proc ** pp1, struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_WRITES
		ORDERKEY_PID
		ORDERKEY_RCHAR
		ORDERKEY_WCHAR
		ORDERKEY_SYSCR
		ORDERKEY_SYSCW
		ORDERKEY_READS
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}

int
compare_cwrites(struct top_proc ** pp1, struct top_proc ** pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;

	/* remove one level of indirection */
	p1 = *pp1;
	p2 = *pp2;

	ORDERKEY_CWRITES
		ORDERKEY_PID
		ORDERKEY_RCHAR
		ORDERKEY_WCHAR
		ORDERKEY_SYSCR
		ORDERKEY_SYSCW
		ORDERKEY_READS
		ORDERKEY_WRITES
		ORDERKEY_NAME
		;

	return result == 0 ? 0 : result < 0 ? -1 : 1;
}

/*
 * proc_owner(pid) - returns the uid that owns process "pid", or -1 if
 *				the process does not exist.
 *				It is EXTREMLY IMPORTANT that this function work correctly.
 *				If pg_top runs setuid root (as in SVR4), then this function
 *				is the only thing that stands in the way of a serious
 *				security problem.  It validates requests for the "kill"
 *				and "renice" commands.
 */

uid_t
proc_owner(pid_t pid)

{
	struct stat sb;
	char		buffer[32];

	sprintf(buffer, "%d", pid);

	if (stat(buffer, &sb) < 0)
		return -1;
	else
		return (int) sb.st_uid;
}

struct io_node *
new_io_node(pid_t pid)
{
	struct io_node *node;

	node = (struct io_node *) malloc(sizeof(struct io_node));
	bzero(node, sizeof(struct io_node));
	node->pid = pid;
	node->next = NULL;

	return node;
}

struct io_node *
get_io_stats(struct io_node *head, pid_t pid)
{
	struct io_node *node = head;

	while (node != NULL)
	{
		if (node->pid == pid)
		{
			return node;
		}
		node = node->next;
	}

	return node;
}

struct io_node *
insert_io_stats(struct io_node *head, struct io_node *node)
{
	struct io_node *c = head;
	struct io_node *p = NULL;

	if (node->pid < head->pid)
	{
		node->next = head;
		head = node;
		return head;
	}

	c = head->next;
	p = head;
	while (c != NULL)
	{
		if (node->pid < c->pid)
		{
			node->next = c;
			p->next = node;
			return head;
		}
		p = c;
		c = c->next;
	}

	if (c == NULL)
	{
		p->next = node;
	}

	return head;
}

void
update_io_stats(struct io_node *node, pid_t pid, long long rchar,
	long long wchar, long long syscr, long long syscw, long long read_bytes,
	long long write_bytes, long long cancelled_write_bytes)
{
	/* Calculate difference between previous and current values. */
	node->diff_rchar = rchar - node->old_rchar;
	node->diff_wchar = wchar - node->old_wchar;
	node->diff_syscr = syscr - node->old_syscr;
	node->diff_syscw = syscw - node->old_syscw;
	node->diff_read_bytes = read_bytes - node->old_read_bytes;
	node->diff_write_bytes = write_bytes - node->old_write_bytes;
	node->diff_cancelled_write_bytes =
			cancelled_write_bytes - node->old_cancelled_write_bytes;

	/* Save the current values as previous values. */
	node->old_rchar = rchar;
	node->old_wchar = wchar;
	node->old_syscr = syscr;
	node->old_syscw = syscw;
	node->old_read_bytes = read_bytes;
	node->old_write_bytes = write_bytes;
	node->old_cancelled_write_bytes = cancelled_write_bytes;
}

struct io_node *
upsert_io_stats(struct io_node *head, pid_t pid, long long rchar,
	long long wchar, long long syscr, long long syscw, long long read_bytes,
	long long write_bytes, long long cancelled_write_bytes)
{
	struct io_node *c = head;

	/* List is empty, create a new node. */
	if (head == NULL)
	{
		head = new_io_node(pid);
		update_io_stats(head, pid, rchar, wchar, syscr, syscw, read_bytes,
				write_bytes, cancelled_write_bytes);
		return head;
	}

	/* Check if this pid exists already. */
	while (c != NULL)
	{
		if (c->pid == pid)
		{
			/* Found an existing node with same pid, update it. */
			update_io_stats(c, pid, rchar, wchar, syscr, syscw, read_bytes,
					write_bytes, cancelled_write_bytes);
			return head;
		}
		c = c->next;
	}

	 /* Didn't find pig.  Create a new node, save the data and insert it. */
	c = new_io_node(pid);
	update_io_stats(c, pid, rchar, wchar, syscr, syscw, read_bytes,
			write_bytes, cancelled_write_bytes);
	head = insert_io_stats(head, c);
	return head;
}
