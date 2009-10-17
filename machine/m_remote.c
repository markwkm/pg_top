/*
 * Copyright (c) 2008-2009, Mark Wong
 */

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <libpq-fe.h>

#include "pg.h"

#include "remote.h"
#include "utils.h"

#define QUERY_CPUTIME \
		"SELECT user, nice, system, idle, iowait\n" \
		"FROM pg_cputime()"

#define QUERY_LOADAVG \
		"SELECT load1, load5, load15, last_pid\n" \
		"FROM pg_loadavg()"

#define QUERY_MEMUSAGE \
		"SELECT memused, memfree, memshared, membuffers, memcached,\n" \
		"       swapused, swapfree, swapcached\n" \
		"FROM pg_memusage()"

#define QUERY_PROCTAB \
		"SELECT pid, comm, fullcomm, state, utime, stime, priority, nice,\n" \
		"       starttime, vsize, rss, uid, username, rchar, wchar,\n" \
		"       syscr, syscw, reads, writes, cwrites\n" \
		"FROM pg_proctab()"

#define QUERY_PG_PROC \
		"SELECT COUNT(*)\n" \
		"FROM pg_catalog.pg_proc\n" \
		"WHERE proname = '%s'"

enum column_cputime { c_cpu_user, c_cpu_nice, c_cpu_system, c_cpu_idle,
		c_cpu_iowait };
enum column_loadavg { c_load1, c_load5, c_load15, c_last_pid };
enum column_memusage { c_memused, c_memfree, c_memshared, c_membuffers,
		c_memcached, c_swapused, c_swapfree, c_swapcached};
enum column_proctab { c_pid, c_comm, c_fullcomm, c_state, c_utime, c_stime,
		c_priority, c_nice, c_starttime, c_vsize, c_rss, c_uid, c_username,
		c_rchar, c_wchar, c_syscr, c_syscw, c_reads, c_writes, c_cwrites };

#define HASH_SIZE (1003)
#define HASH(x) (((x) * 1686629713U) % HASH_SIZE)

#define bytetok(x)  (((x) + 512) >> 10)

#define INITIAL_ACTIVE_SIZE (256)
#define PROCBLOCK_SIZE (32)

#define NCPUSTATES 5
#define NMEMSTATS 5
#define NPROCSTATES 7
#define NSWAPSTATS 3

#define MEMUSED 0
#define MEMFREE 1
#define MEMSHARED 2
#define MEMBUFFERS 3
#define MEMCACHED 4
#define NMEMSTATS 5

#define SWAPUSED 0
#define SWAPFREE 1
#define SWAPCACHED 2

struct top_proc
{
	pid_t pid;
	uid_t uid;
	char *name;
	char *username;
	int pri;
	int nice;
	unsigned long size;
	unsigned long rss; /* in k */
	int state;
	unsigned long time;
	unsigned long start_time;
	double pcpu;
	double wcpu;

	/* The change in the previous values and current values. */
	long long rchar_diff;
	long long wchar_diff;
	long long syscr_diff;
	long long syscw_diff;
	long long read_bytes_diff;
	long long write_bytes_diff;
	long long cancelled_write_bytes_diff;

	/* The absolute values. */
	long long rchar;
	long long wchar;
	long long syscr;
	long long syscw;
	long long read_bytes;
	long long write_bytes;
	long long cancelled_write_bytes;

	struct top_proc *next;
};

static unsigned int activesize = 0;
static time_t boottime = -1;
static struct top_proc **nextactive;
static struct top_proc **pactive;
static struct top_proc *freelist = NULL;
static struct top_proc *procblock = NULL;
static struct top_proc *procmax = NULL;
static struct top_proc *ptable[HASH_SIZE];

static char *cpustatenames[NCPUSTATES + 1] =
{
	"user", "nice", "system", "idle", "iowait", NULL
};

static char *memorynames[NMEMSTATS + 1] =
{
	"K used, ", "K free, ", "K shared, ", "K buffers, ", "K cached", NULL
};

/* these are names given to allowed sorting orders -- first is default */
static char *ordernames[] = {"cpu", "size", "res", "time", "command", NULL};

static char *procstatenames[NPROCSTATES + 1] =
{
	"", " running, ", " sleeping, ", " uninterruptable, ", " zombie, ",
	" stopped, ", " swapping, ", NULL
};

static char *state_abbrev[NPROCSTATES + 1] =
{
	"", "run", "sleep", "disk", "zomb", "stop", "swap", NULL
};

static char *swapnames[NSWAPSTATS + 1] =
{
	"K used, ", "K free, ", "K cached", NULL
};

static char fmt_header[] =
		"  PID X        PRI NICE  SIZE   RES STATE   TIME   WCPU    CPU COMMAND";

/* Now the array that maps process state to a weight. */

unsigned char sort_state_r[] =
{
	0,						  /* empty */
	6,						  /* run */
	3,						  /* sleep */
	5,						  /* disk wait */
	1,						  /* zombie */
	2,						  /* stop */
	4						  /* swap */
};

static int64_t cpu_states[NCPUSTATES];
static long memory_stats[NMEMSTATS];
static int process_states[NPROCSTATES];
static long swap_stats[NSWAPSTATS];

static struct timeval lasttime;

static int64_t cp_time[NCPUSTATES];
static int64_t cp_old[NCPUSTATES];
static int64_t cp_diff[NCPUSTATES];

#define ORDERKEY_PCTCPU  if (dresult = p2->pcpu - p1->pcpu,\
			 (result = dresult > 0.0 ? 1 : dresult < 0.0 ? -1 : 0) == 0)
#define ORDERKEY_CPTICKS if ((result = (long)p2->time - (long)p1->time) == 0)
#define ORDERKEY_STATE   if ((result = (sort_state_r[p2->state] - \
			 sort_state_r[p1->state])) == 0)
#define ORDERKEY_PRIO	if ((result = p2->pri - p1->pri) == 0)
#define ORDERKEY_RSSIZE  if ((result = p2->rss - p1->rss) == 0)
#define ORDERKEY_MEM	 if ((result = p2->size - p1->size) == 0)
#define ORDERKEY_NAME	if ((result = strcmp(p1->name, p2->name)) == 0)

int check_for_function(PGconn *, char *);
int compare_cpu_r(struct top_proc **, struct top_proc **);
int compare_size_r(struct top_proc **, struct top_proc **);
int compare_res_r(struct top_proc **, struct top_proc **);
int compare_time_r(struct top_proc **, struct top_proc **);
int compare_cmd_r(struct top_proc **, struct top_proc **);
static void free_proc(struct top_proc *);
static struct top_proc *new_proc();

int
check_for_function(PGconn *pgconn, char *procname)
{
	PGresult *pgresult = NULL;
	int rows = 0;
	int count;
	char sql[128];

	sprintf(sql, QUERY_PG_PROC, procname);
	pgresult = PQexec(pgconn, sql);
	rows = PQntuples(pgresult);
	/* Don't need to clean up on error, the program will exit shortly after. */
	if (rows == 0)
	{
		fprintf(stderr, "Error executing '%s'.\n", sql);
		return -1;
	}
	count = atoi(PQgetvalue(pgresult, 0, 0));
	if (count == 0) {
		fprintf(stderr, "Stored function '%s' is missing.\n", procname);
		return -1;
	}
	if (pgresult != NULL)
		PQclear(pgresult);
	return 0;
}

int
(*proc_compares_r[])() =
{
	compare_cpu_r,
	compare_size_r,
	compare_res_r,
	compare_time_r,
	compare_cmd_r,
	NULL
};

/* compare_cpu_r - the comparison function for sorting by cpu percentage */

int
compare_cpu_r(struct top_proc **pp1, struct top_proc **pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;
	double	  dresult;

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

/* The comparison function for sorting by total memory usage. */

int
compare_size_r(struct top_proc **pp1, struct top_proc **pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;
	double	  dresult;

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

/* The comparison function for sorting by resident set size. */

int
compare_res_r(struct top_proc **pp1, struct top_proc **pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;
	double	  dresult;

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

/* The comparison function for sorting by total cpu time. */

int
compare_time_r(struct top_proc **pp1, struct top_proc **pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;
	double	  dresult;

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

/* The comparison function for sorting by command name. */

int
compare_cmd_r(struct top_proc ** pp1, struct top_proc **pp2)
{
	register struct top_proc *p1;
	register struct top_proc *p2;
	register long result;
	double	  dresult;

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

char *
format_header_r(char *uname_field)
{
	int uname_len = strlen(uname_field);

 	if (uname_len > 8)
		uname_len = 8;
	memcpy(strchr(fmt_header, 'X'), uname_field, uname_len);
	return fmt_header;
}

char *
format_next_io_r(caddr_t handler)
{
	static char fmt[MAX_COLS]; /* static area where result is built */
	struct top_proc *p = *nextactive++;

	if (mode_stats == STATS_DIFF)
		snprintf(fmt, sizeof(fmt),
				"%5d %5s %5s %7lld %7lld %5s %6s %7s %s",
				p->pid,
				format_b(p->rchar_diff),
				format_b(p->wchar_diff),
				p->syscr_diff,
				p->syscw_diff,
				format_b(p->read_bytes_diff),
				format_b(p->write_bytes_diff),
				format_b(p->cancelled_write_bytes_diff),
				p->name);
	else
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

	return (fmt);
}

char *
format_next_process_r(caddr_t handler)
{
	static char fmt[MAX_COLS]; /* static area where result is built */
	struct top_proc *p = *nextactive++;

	snprintf(fmt, sizeof(fmt),
			"%5d %-8.8s %3d %4d %5s %5s %-5s %6s %5.2f%% %5.2f%% %s",
			(int) p->pid, /* Some OS's need to cast pid_t to int. */
			p->username,
			p->pri < -99 ? -99 : p->pri,
			p->nice,
			format_k(p->size),
			format_k(p->rss),
			state_abbrev[p->state],
			format_time(p->time),
			p->wcpu * 100.0,
			p->pcpu * 100.0,
			p->name);

	return (fmt);
}

static void
free_proc(struct top_proc *proc)
{
	proc->next = freelist;
	freelist = proc;
}

void
get_system_info_r(struct system_info *info, char *conninfo)
{
	PGconn   *pgconn;
	PGresult *pgresult = NULL;
	int rows = 0;

	pgconn = connect_to_db(conninfo);
	if (pgconn != NULL)
	{
		pgresult = PQexec(pgconn, QUERY_LOADAVG);
		rows = PQntuples(pgresult);
	}

	/* Get load averages. */
	if (rows > 0)
	{
		info->load_avg[0] = atof(PQgetvalue(pgresult, 0, c_load1));
		info->load_avg[1] = atof(PQgetvalue(pgresult, 0, c_load5));
		info->load_avg[2] = atof(PQgetvalue(pgresult, 0, c_load15));
		info->last_pid = atoi(PQgetvalue(pgresult, 0, c_last_pid));
	}
	else
	{
		info->load_avg[0] = 0;
		info->load_avg[1] = 0;
		info->load_avg[2] = 0;
		info->last_pid = 0;
	}

	/* Get processor time info. */
	if (pgconn != NULL)
	{
		pgresult = PQexec(pgconn, QUERY_CPUTIME);
		rows = PQntuples(pgresult);
	}
	if (rows > 0)
	{
		cp_time[0] = atol(PQgetvalue(pgresult, 0, c_cpu_user));
		cp_time[1] = atol(PQgetvalue(pgresult, 0, c_cpu_nice));
		cp_time[2] = atol(PQgetvalue(pgresult, 0, c_cpu_system));
		cp_time[3] = atol(PQgetvalue(pgresult, 0, c_cpu_idle));
		cp_time[4] = atol(PQgetvalue(pgresult, 0, c_cpu_iowait));

		/* convert cp_time counts to percentages */
		percentages(NCPUSTATES, cpu_states, cp_time, cp_old, cp_diff);
	}
	else
	{
		cpu_states[0] = 0;
		cpu_states[1] = 0;
		cpu_states[2] = 0;
		cpu_states[3] = 0;
		cpu_states[4] = 0;
	}

	/* Get system wide memory usage. */
	if (pgconn != NULL)
	{
		pgresult = PQexec(pgconn, QUERY_MEMUSAGE);
		rows = PQntuples(pgresult);
	}
	if (rows > 0)
	{
		memory_stats[MEMUSED] = atol(PQgetvalue(pgresult, 0, c_memused));
		memory_stats[MEMFREE] = atol(PQgetvalue(pgresult, 0, c_memfree));
		memory_stats[MEMSHARED] = atol(PQgetvalue(pgresult, 0, c_memshared));
		memory_stats[MEMBUFFERS] = atol(PQgetvalue(pgresult, 0, c_membuffers));
		memory_stats[MEMCACHED] = atol(PQgetvalue(pgresult, 0, c_memcached));
		swap_stats[SWAPUSED] = atol(PQgetvalue(pgresult, 0, c_swapused));
		swap_stats[SWAPFREE] = atol(PQgetvalue(pgresult, 0, c_swapfree));
		swap_stats[SWAPCACHED] = atol(PQgetvalue(pgresult, 0, c_swapcached));
	}
	else
	{
		memory_stats[MEMUSED] = 0;
		memory_stats[MEMFREE] = 0;
		memory_stats[MEMSHARED] = 0;
		memory_stats[MEMBUFFERS] = 0;
		memory_stats[MEMCACHED] = 0;
		swap_stats[SWAPUSED] = 0;
		swap_stats[SWAPFREE] = 0;
		swap_stats[SWAPCACHED] = 0;
	}

	info->cpustates = cpu_states;
	info->memory = memory_stats;
	info->swap = swap_stats;

	if (pgresult != NULL)
		PQclear(pgresult);
	PQfinish(pgconn);
}

caddr_t
get_process_info_r(struct system_info *si, struct process_select *sel,
		int compare_index, char *conninfo)
{
	int i;
	struct top_proc *pp;
	struct top_proc *proc;
	struct top_proc **active;
	pid_t pid;

	PGconn *pgconn;
	PGresult *pgresult = NULL;
	int rows;

	struct timeval thistime;
	double timediff;
	double alpha;
	double beta;
	unsigned long now;
	unsigned long elapsed;

	int total_procs = 0;

	int show_idle = sel->idle;
	int show_uid = sel->uid != -1;

	memset(process_states, 0, sizeof(process_states));

	/* Calculate the time difference since our last check. */
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

	/* Round current time to a second. */
	now = (unsigned long) thistime.tv_sec;
	if (thistime.tv_usec >= 500000)
		now++;

	/* Calculate constants for the exponental average. */
	if (timediff > 0.0 && timediff < 30.0)
	{
		alpha = 0.5 * (timediff / 30.0);
		beta = 1.0 - alpha;
	}
	else
	{
		alpha = beta = 0.5;
	}
	timediff *= HZ;			 /* Convert to ticks. */

	/* Mark all has table entries as not seen. */
	for (i = 0; i < HASH_SIZE; ++i)
		for (proc = ptable[i]; proc; proc = proc->next)
			proc->state = 0;

	pgconn = connect_to_db(conninfo);
	if (pgconn != NULL)
	{
		pgresult = PQexec(pgconn, QUERY_PROCTAB);
		rows = PQntuples(pgresult);
	}
	else
	{
		rows = 0;
	}

	for (i = 0; i < rows; i++)
	{
		unsigned long otime;
		long long value;

		pid = atoi(PQgetvalue(pgresult, i, c_pid));

		/* Look up hash table entry. */
		proc = pp = ptable[HASH(pid)];

		while (proc && proc->pid != pid)
			proc = proc->next;

		/* Create a new entry if not found. */
		if (proc == NULL)
		{
			proc = new_proc();
			proc->pid = pid;
			proc->next = pp;
			ptable[HASH(pid)] = proc;
			proc->time = 0;
			proc->wcpu = 0;

			/* Never mark as owner because we are remote. */
			proc->uid = -1;
		}

		otime = proc->time;

		if (sel->fullcmd && PQgetvalue(pgresult, i, c_fullcomm))
			proc->name = strdup(PQgetvalue(pgresult, i, c_fullcomm));
		else
			proc->name = strdup(PQgetvalue(pgresult, i, c_comm));

		switch (PQgetvalue(pgresult, i, c_state)[0])
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
			continue;
		}

		proc->time = (unsigned long) atol(PQgetvalue(pgresult, i, c_utime));
		proc->time += (unsigned long) atol(PQgetvalue(pgresult, i, c_stime));
		proc->pri = atol(PQgetvalue(pgresult, i, c_priority));
		proc->nice = atol(PQgetvalue(pgresult, i, c_nice));
		proc->start_time = (unsigned long)
				atol(PQgetvalue(pgresult, i, c_starttime));
		proc->size = bytetok((unsigned long)
				atol(PQgetvalue(pgresult, i, c_vsize)));
		proc->rss = bytetok((unsigned long)
				atol(PQgetvalue(pgresult, i, c_rss)));

		proc->uid = atol(PQgetvalue(pgresult, i, c_uid));
		proc->username = strdup(PQgetvalue(pgresult, i, c_username));

		value = atoll(PQgetvalue(pgresult, i, c_rchar));
		proc->rchar_diff = value - proc->rchar;
		proc->rchar = value;

		value = atoll(PQgetvalue(pgresult, i, c_wchar));
		proc->wchar_diff = value - proc->wchar;
		proc->wchar = value;

		value = atoll(PQgetvalue(pgresult, i, c_syscr));
		proc->syscr_diff = value - proc->syscr;
		proc->syscr = value;

		value = atoll(PQgetvalue(pgresult, i, c_syscw));
		proc->syscw_diff = value - proc->syscw;
		proc->syscw = value;

		value = atoll(PQgetvalue(pgresult, i, c_reads));
		proc->read_bytes_diff = value - proc->read_bytes;
		proc->read_bytes = value;

		value = atoll(PQgetvalue(pgresult, i, c_writes));
		proc->write_bytes_diff = value - proc->write_bytes;
		proc->write_bytes = value;

		value = atoll(PQgetvalue(pgresult, i, c_cwrites));
		proc->cancelled_write_bytes_diff = value - proc->cancelled_write_bytes;
		proc->cancelled_write_bytes = value;

		++total_procs;
		++process_states[proc->state];

		if (timediff > 0.0)
		{
			if ((proc->pcpu = (proc->time - otime) / timediff) < 0.0001)
				proc->pcpu = 0;
			proc->wcpu = proc->pcpu * alpha + proc->wcpu * beta;
		}
		else if ((elapsed = (now - boottime) * HZ - proc->start_time) > 0)
			proc->wcpu = proc->pcpu;
		else
			proc->wcpu = proc->pcpu = 0.0;
	}

	if (pgresult != NULL)
		PQclear(pgresult);
	PQfinish(pgconn);

	/* Make sure we have enough slots for the active procs. */
	if (activesize < total_procs)
	{
		pactive = (struct top_proc **) realloc(pactive,
				sizeof(struct top_proc *) * total_procs);
		activesize = total_procs;
	}

 	/* Set up the active procs and flush dead entries. */
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

	/* Sort the "active" procs if specified. */
	if (si->p_active)
		qsort(pactive, si->p_active, sizeof(struct top_proc *),
			proc_compares_r[compare_index]);

	/* Don't even pretend that the return value thing here isn't bogus. */
	nextactive = pactive;

	return 0;
}

int
machine_init_r(struct statics *statics, char *conninfo)
{
	PGconn   *pgconn;

	/* Make sure the remote system has the stored function installed. */
	pgconn = connect_to_db(conninfo);
	if (pgconn == NULL)
	{
		fprintf(stderr, "Cannot connect to database.\n");
		return -1;
	}

	if (check_for_function(pgconn, "pg_cputime") != 0)
		return -1;
	if (check_for_function(pgconn, "pg_loadavg") != 0)
		return -1;
	if (check_for_function(pgconn, "pg_memusage") != 0)
		return -1;
	if (check_for_function(pgconn, "pg_proctab") != 0)
		return -1;
	PQfinish(pgconn);

	/* fill in the statics information */
	statics->procstate_names = procstatenames;
	statics->cpustate_names = cpustatenames;
	statics->memory_names = memorynames;
	statics->swap_names = swapnames;
	statics->order_names = ordernames;
	statics->boottime = boottime;
	statics->flags.fullcmds = 1;
	statics->flags.warmup = 1;

	/* allocate needed space */
	pactive = (struct top_proc **) malloc(sizeof(struct top_proc *) *
			INITIAL_ACTIVE_SIZE);
	activesize = INITIAL_ACTIVE_SIZE;

	/* make sure the hash table is empty */
	memset(ptable, 0, HASH_SIZE * sizeof(struct top_proc *));

	return 0;
}

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
			procblock = NULL;
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
