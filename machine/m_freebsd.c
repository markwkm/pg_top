/*
 * pg_top - a top PostgreSQL users display for Unix
 *
 * SYNOPSIS:  For FreeBSD-2.x, 3.x, 4.x, and 5.x
 *
 * DESCRIPTION:
 * Originally written for BSD4.4 system by Christos Zoulas.
 * Ported to FreeBSD 2.x by Steven Wallace && Wolfram Schneider
 * Order support hacked in from top-3.5beta6/machine/m_aix41.c
 *	 by Monte Mitzelfelt
 * Ported to FreeBSD 5.x by William LeFebvre
 *
 * AUTHOR:	Christos Zoulas <christos@ee.cornell.edu>
 *			Steven Wallace	<swallace@freebsd.org>
 *			Wolfram Schneider <wosch@FreeBSD.org>
 */


#include <sys/time.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/param.h>
#include <err.h>

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <nlist.h>
#include <math.h>
#include <kvm.h>
#include <pwd.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/dkstat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/vmmeter.h>
#include <sys/resource.h>
#include <sys/rtprio.h>
#include <sys/tree.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* Swap */
#include <stdlib.h>
#include <sys/conf.h>

#include <osreldate.h>			/* for changes in kernel structures */

#include "pg_top.h"
#include "machine.h"
#include "utils.h"

/* declarations for load_avg */
#include "loadavg.h"

#define GETSYSCTL(name, var) getsysctl(name, &(var), sizeof(var))

static int	getkval __P((unsigned long, int *, int, char *));
extern char *printable __P((char *));
static void getsysctl(const char *name, void *ptr, size_t len);
int			swapmode __P((int *retavail, int *retfree));

static int	maxcpu;
static int	maxid;
static int	ncpus;
static u_long cpumask;
static long *times;
static long *pcpu_cp_time;
static long *pcpu_cp_old;
static long *pcpu_cp_diff;
static int64_t * pcpu_cpu_states;

static int	smpmode;
static int	namelength;
static int	cmdlength;


/* get_process_info passes back a handle.  This is what it looks like: */

struct handle
{
	struct kinfo_proc **next_proc;	/* points to next valid proc pointer */
	int			remaining;		/* number of pointers remaining */
};

struct pg_proc
{
	RB_ENTRY(pg_proc) entry;
	pid_t		pid;

	/* This will be the previous copy of ki_rusage. */
    struct rusage ki_rusage;

	char *name;
	char *usename;
	int pgstate;
	unsigned long xtime;
	unsigned long qtime;
	unsigned int locks;

	/* Replication data */
	char	   *application_name;
	char	   *client_addr;
	char	   *repstate;
	char	   *primary;
	char	   *sent;
	char	   *write;
	char	   *flush;
	char	   *replay;
	long long	sent_lag;
	long long	write_lag;
	long long	flush_lag;
	long long	replay_lag;
};

int			topproccmp(struct pg_proc *, struct pg_proc *);

RB_HEAD(pgproc, pg_proc) head_proc = RB_INITIALIZER(&head_proc);
RB_PROTOTYPE(pgproc, pg_proc, entry, topproccmp)
RB_GENERATE(pgproc, pg_proc, entry, topproccmp)

/* macros to access process information */
#if OSMAJOR <= 4
#define PP(pp, field) ((pp)->kp_proc . p_##field)
#define EP(pp, field) ((pp)->kp_eproc . e_##field)
#define VP(pp, field) ((pp)->kp_eproc.e_vm . vm_##field)
#define PRUID(pp) ((pp)->kp_eproc.e_pcred.p_ruid)
#else
#define PP(pp, field) ((pp)->ki_##field)
#define VP(pp, field) ((pp)->ki_##field)
#define PRUID(pp) ((pp)->ki_ruid)
#endif
#define RU(pp)	(&(pp)->ki_rusage)

/* what we consider to be process size: */
#if OSMAJOR <= 4
#define PROCSIZE(pp) (VP((pp), map.size) / 1024)
#else
#define PROCSIZE(pp) (((pp)->ki_size) / 1024)
#endif

/* for 5.x and higher we show thread count */
#if OSMAJOR >= 5
#define SHOW_THREADS
#endif

/* definitions for indices in the nlist array */

static struct nlist nlst[] = {
#define X_CCPU		0
	{"_ccpu"},
#define X_CP_TIME	1
	{"_cp_time"},
#define X_AVENRUN	2
	{"_averunnable"},

#define X_BUFSPACE	3
	{"_bufspace"},				/* K in buffer cache */
#define X_CNT			4
	{"_cnt"},					/* struct vmmeter cnt */

/* Last pid */
#define X_LASTPID	5
	{"_nextpid"},

#define X_BOOTTIME	6
	{"_boottime"},

	{0}
};

/*
 *	These definitions control the format of the per-process area
 */

static char header[] =
"  PID %-*.*s    SIZE    RES STATE   XTIME  QTIME    CPU LOCKS COMMAND";

/* process state names for the "STATE" column of the display */
/* the extra nulls in the string "run" are for adding a slash and
   the processor number when needed */

char	   *state_abbrev[] =
{
	"", "START", "RUN\0\0\0", "SLEEP", "STOP", "ZOMB",
};


char fmt_header_io[] =
		"PID   USERNAME     VCSW  IVCSW   READ  WRITE  FAULT  TOTAL COMMAND";

static kvm_t * kd;

/* values that we stash away in _init and use in later routines */

static double logcpu;

/* these are retrieved from the kernel in _init */

static load_avg ccpu;

/* these are offsets obtained via nlist and used in the get_ functions */

static unsigned long cp_time_offset;
static unsigned long avenrun_offset;
static unsigned long lastpid_offset;
static int	lastpid;
static unsigned long cnt_offset;
static unsigned long bufspace_offset;

/* these are for calculating cpu state percentages */

static int64_t cp_time[CPUSTATES];
static int64_t cp_old[CPUSTATES];
static int64_t cp_diff[CPUSTATES];

/* these are for detailing the process states */

int			process_states[6];

/* these are for detailing the cpu states */

int64_t		cpu_states[CPUSTATES];
char	   *cpustatenames[] = {
	"user", "nice", "system", "interrupt", "idle", NULL
};

/* these are for detailing the memory statistics */

long		memory_stats[7];
char	   *memorynames[] = {
	"K Active, ", "K Inact, ", "K Wired, ", "K Cache, ", "K Buf, ", "K Free",
	NULL
};

long		swap_stats[7];
char	   *swapnames[] = {
/*	 0			 1			  2			  3			   4	   5 */
	"K Total, ", "K Used, ", "K Free, ", "% Inuse, ", "K In, ", "K Out",
	NULL
};


/* these are for keeping track of the proc array */

static int	nproc;
static int	onproc = -1;
static int	pref_len;
static struct kinfo_proc *pbase;
static struct kinfo_proc **pref;

/* these are for getting the memory statistics */

static int	pageshift;			/* log base 2 of the pagesize */

/* define pagetok in terms of pageshift */

#define pagetok(size) ((size) << pageshift)

/* useful externals */
long		percentages();

/* sorting orders. first is default */
char	   *ordernames[] = {"cpu", "size", "res", "time", "pri", NULL};

/* compare routines */
int			proc_compare(), compare_size(), compare_res(), compare_time(), compare_prio();

int			(*proc_compares[]) () =
{
	proc_compare,
		compare_size,
		compare_res,
		compare_time,
		compare_prio,
		NULL
};

int
machine_init(struct statics *statics)

{
	register int pagesize;
	size_t		size;
	struct passwd *pw;
	int			i,
				j,
				empty;

	size = sizeof(smpmode);
	if ((sysctlbyname("machdep.smp_active", &smpmode, &size, NULL, 0) != 0 &&
		 sysctlbyname("smp.smp_active", &smpmode, &size, NULL, 0) != 0) ||
		size != sizeof(smpmode))
		smpmode = 0;

	while ((pw = getpwent()) != NULL)
	{
		if (strlen(pw->pw_name) > namelength)
			namelength = strlen(pw->pw_name);
	}
	if (namelength < 8)
		namelength = 8;
	if (smpmode && namelength > 13)
		namelength = 13;
	else if (namelength > 15)
		namelength = 15;

	/*
	 * Silence kvm_open in the event that the pid from the database is gone
	 * before we ask the operating system about it.
	 */
	if ((kd = kvm_open(NULL, "/dev/null", NULL, O_RDONLY, NULL)) == NULL)
		return -1;

	/* get number of cpus */
	GETSYSCTL("kern.ccpu", ccpu);

	/* stash away certain offsets for later use */
	cp_time_offset = nlst[X_CP_TIME].n_value;
	avenrun_offset = nlst[X_AVENRUN].n_value;
	lastpid_offset = nlst[X_LASTPID].n_value;
	cnt_offset = nlst[X_CNT].n_value;
	bufspace_offset = nlst[X_BUFSPACE].n_value;

	/* this is used in calculating WCPU -- calculate it ahead of time */
	logcpu = log(loaddouble(ccpu));

	pbase = NULL;
	pref = NULL;
	nproc = 0;
	onproc = -1;

	/* get the page size with "getpagesize" and calculate pageshift from it */
	pagesize = getpagesize();
	pageshift = 0;
	while (pagesize > 1)
	{
		pageshift++;
		pagesize >>= 1;
	}

	/* we only need the amount of log(2)1024 for our conversion */
	pageshift -= LOG1024;

	/* fill in the statics information */
	statics->procstate_names = procstatenames;
	statics->cpustate_names = cpustatenames;
	statics->memory_names = memorynames;
	statics->swap_names = swapnames;
	statics->order_names = ordernames;
	statics->flags.fullcmds = 1;

	/* Allocate state for per-CPU stats. */
	cpumask = 0;
	ncpus = 0;
	GETSYSCTL("kern.smp.maxcpus", maxcpu);
	size = sizeof(long) * maxcpu * CPUSTATES;
	times = malloc(size);
	if (times == NULL)
		err(1, "malloc %zd bytes", size);
	if (sysctlbyname("kern.cp_times", times, &size, NULL, 0) == -1)
		err(1, "sysctlbyname kern.cp_times");
	pcpu_cp_time = calloc(1, size);
	maxid = (size / CPUSTATES / sizeof(long)) - 1;
	for (i = 0; i <= maxid; i++)
	{
		empty = 1;
		for (j = 0; empty && j < CPUSTATES; j++)
		{
			if (times[i * CPUSTATES + j] != 0)
				empty = 0;
		}
		if (!empty)
		{
			cpumask |= (1ul << i);
			ncpus++;
		}
	}
	size = sizeof(long) * ncpus * CPUSTATES;
	pcpu_cp_old = calloc(1, size);
	pcpu_cp_diff = calloc(1, size);
	pcpu_cpu_states = calloc(1, size);
	statics->ncpus = ncpus;

	/* all done! */
	return (0);
}

char *
format_header(char *uname_field)

{
	static char Header[128];

	snprintf(Header, sizeof(Header), header, namelength, namelength,
			uname_field);

	cmdlength = 80 - strlen(Header) + 6;

	return Header;
}

static int	swappgsin = -1;
static int	swappgsout = -1;
extern struct timeval timeout;

void
get_system_info(struct system_info *si)

{
	struct loadavg sysload;
	size_t		size;
	int			i,
				j;

	/* get the CPU stats */
	size = (maxid + 1) * CPUSTATES * sizeof(long);
	if (sysctlbyname("kern.cp_times", pcpu_cp_time, &size, NULL, 0) == -1)
		err(1, "sysctlbyname kern.cp_times");
	GETSYSCTL("kern.cp_time", cp_time);
	GETSYSCTL("vm.loadavg", sysload);
	GETSYSCTL("kern.lastpid", lastpid);

	/* convert load averages to doubles */
	for (i = 0; i < 3; i++)
		si->load_avg[i] = (double) sysload.ldavg[i] / sysload.fscale;

	/* convert cp_time counts to percentages */
	for (i = j = 0; i <= maxid; i++)
	{
		if ((cpumask & (1ul << i)) == 0)
			continue;
		percentages(CPUSTATES, &pcpu_cpu_states[j * CPUSTATES],
					&pcpu_cp_time[j * CPUSTATES],
					&pcpu_cp_old[j * CPUSTATES],
					&pcpu_cp_diff[j * CPUSTATES]);
		j++;
	}
	percentages(CPUSTATES, cpu_states, cp_time, cp_old, cp_diff);

	/* sum memory & swap statistics */
	{
		static unsigned int swap_delay = 0;
		static int	swapavail = 0;
		static int	swapfree = 0;
		static int	bufspace = 0;
		static int	nspgsin,
					nspgsout;

		/*
		 * Use this temporary int array because we use longs for the other
		 * patforms.
		 */
		int			tmp_memory_stats[7];

		GETSYSCTL("vfs.bufspace", bufspace);
		GETSYSCTL("vm.stats.vm.v_active_count", tmp_memory_stats[0]);
		GETSYSCTL("vm.stats.vm.v_inactive_count", tmp_memory_stats[1]);
		GETSYSCTL("vm.stats.vm.v_wire_count", tmp_memory_stats[2]);
		GETSYSCTL("vm.stats.vm.v_cache_count", tmp_memory_stats[3]);
		GETSYSCTL("vm.stats.vm.v_free_count", tmp_memory_stats[5]);
		GETSYSCTL("vm.stats.vm.v_swappgsin", nspgsin);
		GETSYSCTL("vm.stats.vm.v_swappgsout", nspgsout);

		/* convert memory stats to Kbytes */
		memory_stats[0] = pagetok(tmp_memory_stats[0]);
		memory_stats[1] = pagetok(tmp_memory_stats[1]);
		memory_stats[2] = pagetok(tmp_memory_stats[2]);
		memory_stats[3] = pagetok(tmp_memory_stats[3]);
		memory_stats[4] = bufspace / 1024;
		memory_stats[5] = pagetok(tmp_memory_stats[5]);
		memory_stats[6] = -1;

		/* first interval */
		if (swappgsin < 0)
		{
			swap_stats[4] = 0;
			swap_stats[5] = 0;
		}

		/* compute differences between old and new swap statistic */
		else
		{
			swap_stats[4] = pagetok(((nspgsin - swappgsin)));
			swap_stats[5] = pagetok(((nspgsout - swappgsout)));
		}

		swappgsin = nspgsin;
		swappgsout = nspgsout;

		/* call CPU heavy swapmode() only for changes */
		if (swap_stats[4] > 0 || swap_stats[5] > 0 || swap_delay == 0)
		{
			swap_stats[3] = swapmode(&swapavail, &swapfree);
			swap_stats[0] = swapavail;
			swap_stats[1] = swapavail - swapfree;
			swap_stats[2] = swapfree;
		}
		swap_delay = 1;
		swap_stats[6] = -1;
	}

	/* set arrays and strings */
	si->cpustates = cpu_states;
	si->memory = memory_stats;
	si->swap = swap_stats;

	if (lastpid > 0)
	{
		si->last_pid = lastpid;
	}
	else
	{
		si->last_pid = -1;
	}

}

static struct handle handle;
static int	show_fullcmd;

caddr_t
get_process_info(struct system_info *si,
				 struct process_select *sel,
				 int compare_index,
				 struct pg_conninfo_ctx *conninfo,
				 int mode)

{
	register int i;
	register int total_procs;
	register int active_procs;
	register struct kinfo_proc **prefp;
	register struct kinfo_proc *pp;

	/* these are copied out of sel for speed */
	int			show_idle;
	int			show_self;
	int			show_system = 0;

	PGresult   *pgresult = NULL;
	struct pg_proc *n, *p;

	nproc = 0;
	connect_to_db(conninfo);
	if (conninfo->connection != NULL)
	{
		if (mode == MODE_REPLICATION)
		{
			pgresult = pg_replication(conninfo->connection);
		}
		else
		{
			pgresult = pg_processes(conninfo->connection);
		}
		nproc = PQntuples(pgresult);
		if (nproc > onproc)
			pbase = (struct kinfo_proc *)
				realloc(pbase, sizeof(struct kinfo_proc) * nproc);

		pgresult = pg_processes(conninfo->connection);
	}

	if (nproc > onproc)
		pref = (struct kinfo_proc **) realloc(pref, sizeof(struct kinfo_proc *)
											  * (onproc = nproc));
	if (pref == NULL)
	{
		(void) fprintf(stderr, "pg_top: Out of memory.\n");
		quit(23);
	}
	/* get a pointer to the states summary array */
	si->procstates = process_states;

	/* set up flags which define what we are going to select */
	show_idle = sel->idle;
	show_self = 0;
	show_fullcmd = sel->fullcmd;

	/* count up process states and get pointers to interesting procs */
	total_procs = 0;
	active_procs = 0;
	memset((char *) process_states, 0, sizeof(process_states));
	prefp = pref;
	for (pp = pbase, i = 0; i < nproc; pp++, i++)
	{
		struct kinfo_proc *junk2;
		int			junk;

		junk2 = kvm_getprocs(kd, KERN_PROC_PID,
							 atoi(PQgetvalue(pgresult, i, 0)), &junk);
		if (junk2 == NULL)
		{
			continue;
		}

		/*
		 * FIXME: This memcpy is so not elegant and the reason why I'm doing
		 * it...
		 */
		memcpy(&pbase[i], &junk2[0], sizeof(struct kinfo_proc));

		/*
		 * Place pointers to each valid proc structure in pref[]. Process
		 * slots that are actually in use have a non-zero status field.
		 * Processes with P_SYSTEM set are system processes---these get
		 * ignored unless show_sysprocs is set.
		 */
		if (PP(pp, stat) != 0 &&
			(show_self != PP(pp, pid)) &&
			(show_system || ((PP(pp, flag) & P_SYSTEM) == 0)))
		{
			total_procs++;
			process_states[(unsigned char) PP(pp, stat)]++;
			if ((PP(pp, stat) != SZOMB) &&
				(show_idle || (PP(pp, pctcpu) != 0) ||
				 (PP(pp, stat) == SRUN)))
			{
				*prefp++ = pp;
				active_procs++;
			}
		}

		n = malloc(sizeof(struct pg_proc));
		if (n == NULL)
		{
			fprintf(stderr, "malloc error\n");
			if (pgresult != NULL)
				PQclear(pgresult);
			disconnect_from_db(conninfo);
			exit(1);
		}
		memset(n, 0, sizeof(struct pg_proc));
		n->pid = atoi(PQgetvalue(pgresult, i, 0));
		p = RB_INSERT(pgproc, &head_proc, n);
		if (p != NULL)
		{
			free(n);
			n = p;
			memcpy(RU(n), RU(&junk2[0]), sizeof(struct rusage));
		}

		if (mode == MODE_REPLICATION)
		{
			update_str(&n->usename, PQgetvalue(pgresult, i, REP_USENAME));
			update_str(&n->application_name,
					PQgetvalue(pgresult, i, REP_APPLICATION_NAME));
			update_str(&n->client_addr,
					PQgetvalue(pgresult, i, REP_CLIENT_ADDR));
			update_str(&n->repstate, PQgetvalue(pgresult, i, REP_STATE));
			update_str(&n->primary,
					PQgetvalue(pgresult, i, REP_WAL_INSERT));
			update_str(&n->sent, PQgetvalue(pgresult, i, REP_SENT));
			update_str(&n->write, PQgetvalue(pgresult, i, REP_WRITE));
			update_str(&n->flush, PQgetvalue(pgresult, i, REP_FLUSH));
			update_str(&n->replay, PQgetvalue(pgresult, i, REP_REPLAY));
			n->sent_lag = atol(PQgetvalue(pgresult, i, REP_SENT_LAG));
			n->write_lag = atol(PQgetvalue(pgresult, i, REP_WRITE_LAG));
			n->flush_lag = atol(PQgetvalue(pgresult, i, REP_FLUSH_LAG));
			n->replay_lag = atol(PQgetvalue(pgresult, i, REP_REPLAY_LAG));
		}
		else
		{
			update_str(&n->name, PQgetvalue(pgresult, i, PROC_QUERY));
			printable(n->name);
			update_state(&n->pgstate, PQgetvalue(pgresult, i, PROC_STATE));
			update_str(&n->usename, PQgetvalue(pgresult, i, PROC_USENAME));
			n->xtime = atol(PQgetvalue(pgresult, i, PROC_XSTART));
			n->qtime = atol(PQgetvalue(pgresult, i, PROC_QSTART));
			n->locks = atoi(PQgetvalue(pgresult, i, PROC_LOCKS));
		}
	}

	if (pgresult != NULL)
		PQclear(pgresult);
	disconnect_from_db(conninfo);

	/* if requested, sort the "interesting" processes */
	if (compare_index >= 0 && active_procs)
		qsort((char *) pref, active_procs, sizeof(struct kinfo_proc *),
		  		proc_compares[compare_index]);

	/* remember active and total counts */
	si->p_total = total_procs;
	si->p_active = pref_len = active_procs;

	/* pass back a handle */
	handle.next_proc = pref;
	handle.remaining = active_procs;
	return ((caddr_t) & handle);
}

char		fmt[MAX_COLS];		/* static area where result is built */
char		cmd[MAX_COLS];

char *
format_next_io(caddr_t handle)
{
	register struct kinfo_proc *pp;
	struct handle *hp;
	struct pg_proc n, *p = NULL;

	/* find and remember the next proc structure */
	hp = (struct handle *) handle;
	pp = *(hp->next_proc++);
	hp->remaining--;

	memset(&n, 0, sizeof(struct pg_proc));
	n.pid = PP(pp, pid);
	p = RB_FIND(pgproc, &head_proc, &n);

	snprintf(fmt, sizeof(fmt),
			"%5d %-*.*s %6ld %6ld %6ld %6ld %6ld %6ld %s",
			PP(pp, pid),
			namelength, namelength,
			p->usename,
			RU(pp)->ru_nvcsw - RU(p)->ru_nvcsw,
			RU(pp)->ru_nivcsw - RU(p)->ru_nivcsw,
			RU(pp)->ru_inblock - RU(p)->ru_inblock,
			RU(pp)->ru_oublock - RU(p)->ru_oublock,
			RU(pp)->ru_majflt - RU(p)->ru_majflt,
			(RU(pp)->ru_inblock - RU(p)->ru_inblock) +
					(RU(pp)->ru_oublock - RU(p)->ru_oublock) +
					(RU(pp)->ru_majflt - RU(p)->ru_majflt),
			p->name);

	return fmt;
}

char *
format_next_process(caddr_t handle)

{
	register struct kinfo_proc *pp;
	register long cputime;
	register double pct;
	struct handle *hp;
	char		status[16];
	int			state;

	struct pg_proc n, *pr = NULL;

	/* find and remember the next proc structure */
	hp = (struct handle *) handle;
	pp = *(hp->next_proc++);
	hp->remaining--;

	/* get the process's command name in to "cmd" */
	if (show_fullcmd)
	{
		struct pargs pargs;
		int			len;

		/* get the pargs structure */
		getkval((unsigned long) PP(pp, args), (int *) &pargs,
				sizeof(pargs), "!pargs");

		/* determine workable length */
		if ((len = pargs.ar_length) >= MAX_COLS)
		{
			len = MAX_COLS - 1;
		}

		/* get the string from that */
		getkval((unsigned long) PP(pp, args) +
				sizeof(pargs.ar_ref) +
				sizeof(pargs.ar_length),
				(int *) cmd, len, "!cmdline");
	}
#if OSMAJOR <= 4
	else if ((PP(pp, flag) & P_INMEM) == 0)
#else
	else if ((PP(pp, sflag) & PS_INMEM) == 0)
#endif
	{
		/* Print swapped processes as <pname> */
		char	   *p;

		cmd[0] = '<';
		p = strecpy(cmd + 1, PP(pp, comm));
		*p++ = '>';
		*p = '\0';
	}
	else
	{
		/* take it straight out of p_comm */
		strncpy(cmd, PP(pp, comm), MAX_COLS - 1);
	}

	/*
	 * Convert the process's runtime from microseconds to seconds.  This time
	 * includes the interrupt time although that is not wanted here. ps(1) is
	 * similarly sloppy.
	 */
	cputime = (PP(pp, runtime) + 500000) / 1000000;

	/* calculate the base for cpu percentages */
	pct = pctdouble(PP(pp, pctcpu));

	/* generate "STATE" field */
	switch (state = PP(pp, stat))
	{
		case SRUN:
			if (smpmode && PP(pp, oncpu) != 0xff)
				snprintf(status, sizeof(status), "CPU%d", PP(pp, oncpu));
			else
				strcpy(status, "RUN");
			break;
		case SSLEEP:
			if (PP(pp, wmesg) != NULL)
			{
#if OSMAJOR <= 4
				snprintf(status, sizeof(status), "%.6s", EP(pp, wmesg));
#else
				snprintf(status, sizeof(status), "%.6s", PP(pp, wmesg));
#endif
				break;
			}
			/* fall through */
		default:

			if (state >= 0 &&
				state < sizeof(state_abbrev) / sizeof(*state_abbrev))
				snprintf(status, sizeof(status), "%.6s", state_abbrev[(unsigned char) state]);
			else
				snprintf(status, sizeof(status), "?%5d", state);
			break;
	}

	memset(&n, 0, sizeof(struct pg_proc));
	n.pid = PP(pp, pid);
	pr = RB_FIND(pgproc, &head_proc, &n);

	/* format this entry */
	snprintf(fmt, sizeof(fmt),
			"%5d %-*.*s %7s %6s %-6.6s %5s %5s %5.2f%% %5d %s",
			PP(pp, pid),
			namelength, namelength,
			pr->usename,
			format_k(PROCSIZE(pp)),
			format_k(pagetok(VP(pp, rssize))),
			backendstatenames[pr->pgstate],
			format_time(pr->xtime),
			format_time(pr->qtime),
			100.0 * pct,
			pr->locks,
			pr->name);

	/* return the result */
	return (fmt);
}

char *
format_next_replication(caddr_t handle)
{
	static char fmt[MAX_COLS];	/* static area where result is built */
	register struct kinfo_proc *pp;
	struct handle *hp;
	struct pg_proc n, *p = NULL;

	/* find and remember the next proc structure */
	hp = (struct handle *) handle;
	pp = *(hp->next_proc++);
	hp->remaining--;

	memset(&n, 0, sizeof(struct pg_proc));
	n.pid = PP(pp, pid);
	p = RB_FIND(pgproc, &head_proc, &n);

	snprintf(fmt, sizeof(fmt),
			 "%7d %-8.8s %-11.11s %15s %-9.9s %-10.10s %-10.10s %-10.10s %-10.10s %-10.10s %5s %5s %5s %5s",
			 p->pid,
			 p->usename,
			 p->application_name,
			 p->client_addr,
			 p->repstate,
			 p->primary,
			 p->sent,
			 p->write,
			 p->flush,
			 p->replay,
			 format_b(p->sent_lag),
			 format_b(p->write_lag),
			 format_b(p->flush_lag),
			 format_b(p->replay_lag));

	/* return the result */
	return (fmt);
}

/*
 *	getkval(offset, ptr, size, refstr) - get a value out of the kernel.
 *	"offset" is the byte offset into the kernel for the desired value,
 *		"ptr" points to a buffer into which the value is retrieved,
 *		"size" is the size of the buffer (and the object to retrieve),
 *		"refstr" is a reference string used when printing error meessages,
 *		if "refstr" starts with a '!', then a failure on read will not
 *			be fatal (this may seem like a silly way to do things, but I
 *			really didn't want the overhead of another argument).
 *
 */

static int
getkval(unsigned long offset, int *ptr, int size, char *refstr)

{
	if (kvm_read(kd, offset, (char *) ptr, size) != size)
	{
		if (*refstr == '!')
		{
			return (0);
		}
		else
		{
			fprintf(stderr, "pg_top: kvm_read for %s: %s\n",
					refstr, strerror(errno));
			quit(23);
		}
	}
	return (1);
}

/* comparison routines for qsort */

/*
 *	proc_compare - comparison function for "qsort"
 *	Compares the resource consumption of two processes using five
 *		distinct keys.	The keys (in descending order of importance) are:
 *		percent cpu, cpu ticks, state, resident set size, total virtual
 *		memory usage.  The process states are ordered as follows (from least
 *		to most important):  WAIT, zombie, sleep, stop, start, run.  The
 *		array declaration below maps a process state index into a number
 *		that reflects this ordering.
 */

static unsigned char sorted_state[] =
{
	0,							/* not used		*/
	3,							/* sleep		*/
	1,							/* ABANDONED (WAIT) */
	6,							/* run			*/
	5,							/* start		*/
	2,							/* zombie		*/
	4							/* stop			*/
};


#define ORDERKEY_PCTCPU \
  if (lresult = (long) PP(p2, pctcpu) - (long) PP(p1, pctcpu), \
	 (result = lresult > 0 ? 1 : lresult < 0 ? -1 : 0) == 0)

#define ORDERKEY_CPTICKS \
  if ((result = PP(p2, runtime) > PP(p1, runtime) ? 1 : \
				PP(p2, runtime) < PP(p1, runtime) ? -1 : 0) == 0)

#define ORDERKEY_STATE \
  if ((result = sorted_state[(unsigned char) PP(p2, stat)] - \
				sorted_state[(unsigned char) PP(p1, stat)]) == 0)

#if OSMAJOR <= 4
#define ORDERKEY_PRIO \
  if ((result = PP(p2, priority) - PP(p1, priority)) == 0)
#else
#define ORDERKEY_PRIO \
  if ((result = PP(p2, pri.pri_user) - PP(p1, pri.pri_user)) == 0)
#endif

#define ORDERKEY_RSSIZE \
  if ((result = VP(p2, rssize) - VP(p1, rssize)) == 0)

#define ORDERKEY_MEM \
  if ( (result = PROCSIZE(p2) - PROCSIZE(p1)) == 0 )

/* compare_cpu - the comparison function for sorting by cpu percentage */

int
proc_compare(struct proc **pp1, struct proc **pp2)

{
	register struct kinfo_proc *p1;
	register struct kinfo_proc *p2;
	register int result;
	register pctcpu lresult;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;

	ORDERKEY_PCTCPU
		ORDERKEY_CPTICKS
		ORDERKEY_STATE
		ORDERKEY_PRIO
		ORDERKEY_RSSIZE
		ORDERKEY_MEM
		;

	return (result);
}

/* compare_size - the comparison function for sorting by total memory usage */

int
compare_size(struct proc **pp1, struct proc **pp2)

{
	register struct kinfo_proc *p1;
	register struct kinfo_proc *p2;
	register int result;
	register pctcpu lresult;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;

	ORDERKEY_MEM
		ORDERKEY_RSSIZE
		ORDERKEY_PCTCPU
		ORDERKEY_CPTICKS
		ORDERKEY_STATE
		ORDERKEY_PRIO
		;

	return (result);
}

/* compare_res - the comparison function for sorting by resident set size */

int
compare_res(struct proc **pp1, struct proc **pp2)

{
	register struct kinfo_proc *p1;
	register struct kinfo_proc *p2;
	register int result;
	register pctcpu lresult;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;

	ORDERKEY_RSSIZE
		ORDERKEY_MEM
		ORDERKEY_PCTCPU
		ORDERKEY_CPTICKS
		ORDERKEY_STATE
		ORDERKEY_PRIO
		;

	return (result);
}

/* compare_time - the comparison function for sorting by total cpu time */

int
compare_time(struct proc **pp1, struct proc **pp2)

{
	register struct kinfo_proc *p1;
	register struct kinfo_proc *p2;
	register int result;
	register pctcpu lresult;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;

	ORDERKEY_CPTICKS
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		ORDERKEY_PRIO
		ORDERKEY_RSSIZE
		ORDERKEY_MEM
		;

	return (result);
}

/* compare_prio - the comparison function for sorting by cpu percentage */

int
compare_prio(struct proc **pp1, struct proc **pp2)

{
	register struct kinfo_proc *p1;
	register struct kinfo_proc *p2;
	register int result;
	register pctcpu lresult;

	/* remove one level of indirection */
	p1 = *(struct kinfo_proc **) pp1;
	p2 = *(struct kinfo_proc **) pp2;

	ORDERKEY_PRIO
		ORDERKEY_CPTICKS
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		ORDERKEY_RSSIZE
		ORDERKEY_MEM
		;

	return (result);
}

/*
 * proc_owner(pid) - returns the uid that owns process "pid", or -1 if
 *		the process does not exist.
 *		It is EXTREMLY IMPORTANT that this function work correctly.
 *		If pg_top runs setuid root (as in SVR4), then this function
 *		is the only thing that stands in the way of a serious
 *		security problem.  It validates requests for the "kill"
 *		and "renice" commands.
 */

uid_t
proc_owner(pid_t pid)

{
	register int cnt;
	register struct kinfo_proc **prefp;
	register struct kinfo_proc *pp;

	prefp = pref;
	cnt = pref_len;
	while (--cnt >= 0)
	{
		pp = *prefp++;
		if (PP(pp, pid) == (pid_t) pid)
		{
			return ((int) PRUID(pp));
		}
	}
	return (-1);
}


static void
getsysctl(const char *name, void *ptr, size_t len)
{
	size_t		nlen = len;

	if (sysctlbyname(name, ptr, &nlen, NULL, 0) == -1)
	{
		fprintf(stderr, "pg_top: sysctl(%s...) failed: %s\n", name,
				strerror(errno));
		quit(23);
	}
	if (nlen != len)
	{
		fprintf(stderr, "pg_top: sysctl(%s...) expected %lu, got %lu\n",
				name, (unsigned long) len, (unsigned long) nlen);
		quit(23);
	}
}

/*
 * swapmode is based on a program called swapinfo written
 * by Kevin Lahey <kml@rokkaku.atl.ga.us>.
 */

#define SVAR(var) __STRING(var) /* to force expansion */
#define KGET(idx, var)							\
	KGET1(idx, &var, sizeof(var), SVAR(var))
#define KGET1(idx, p, s, msg)						\
	KGET2(nlst[idx].n_value, p, s, msg)
#define KGET2(addr, p, s, msg)						\
	if (kvm_read(kd, (u_long)(addr), p, s) != s) {				\
		warnx("cannot read %s: %s", msg, kvm_geterr(kd));		\
		return (0);												\
	   }
#define KGETRET(addr, p, s, msg)					\
	if (kvm_read(kd, (u_long)(addr), p, s) != s) {			\
		warnx("cannot read %s: %s", msg, kvm_geterr(kd));	\
		return (0);						\
	}


int
swapmode(int *retavail, int *retfree)

{
	int			n;
	int			pagesize = getpagesize();
	struct kvm_swap swapary[1];

	*retavail = 0;
	*retfree = 0;

#define CONVERT(v)	((quad_t)(v) * pagesize / 1024)

	n = kvm_getswapinfo(kd, swapary, 1, 0);
	if (n < 0 || swapary[0].ksw_total == 0)
		return (0);

	*retavail = CONVERT(swapary[0].ksw_total);
	*retfree = CONVERT(swapary[0].ksw_total - swapary[0].ksw_used);

	n = (int) ((double) swapary[0].ksw_used * 100.0 /
			   (double) swapary[0].ksw_total);
	return (n);
}

int
topproccmp(struct pg_proc *e1, struct pg_proc *e2)
{
	return (e1->pid < e2->pid ? -1 : e1->pid > e2->pid);
}
