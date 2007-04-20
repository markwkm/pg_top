/*
 * top - a top users display for Unix
 *
 * SYNOPSIS: For Intel based SysVr4.2MP  (UnixWare 2)
 *
 * DESCRIPTION:
 * System V release 4.2MP for i?86 (UnixWare2)
 *
 * LIBS:  	-lelf
 *
 * CFLAGS:	-DHAVE_GETOPT -DORDER

 * AUTHOR:  	Daniel Harris	<daniel@greencape.com.au>
 *              Mike Hopkirk    <hops@sco.com>
 *
 * BASED ON: 	Most of the work in this module is attributable to the
 *		authors of m_svr42.c and m_sunos5.c.  (Thanks!)
 *
 *		Originally written by Daniel Green as an implementation for
 *		Unixware7 (sysVr5)
 *		Incomplete for that but applied by Mike Hopkirk to svr4.2MP
 *		for which there was not a working port available.
 *              
 * NOTES:
 *
 *	You shouldn't make this setuid anything.  It doesn't flip between
 *	saved uids.
 *
 *	This module looks nothing like other top modules.  In my deluded
 *	world, function follows form.  Code needs to look good in order
 *	to work.  :-)  Apologies to anyone offended by by reformatting.
 *
 *	The number of processes is calculated by the number of entries in the
 *	/proc filesystem.
 *
 *	sysinfo structure should be available from the kernel in preference
 *	to following met_localdata_ptrs_p but if so its naming is obscure.
 *
 *	Ordering of tasks other than by the default isn't implemented yet.
 *
 *	Nice value is always displayed as 0 due bug in UW2
 * 
 *
 * DATE		CHANGE
 * 03/09/1998	Couple of comment changes.  Prepare for release.
 * 13/06/1998	Cleaned out debugging code, prepared for limited release.
 * 09/07/1999	Modified for UnixWare 2 (2.1.2) build - hops
 *              Added use of system getopt and additional sort orders
 *
 */


/* ************************************************************************** */

/* build config
 *  SHOW_NICE - process nice fields always accessed as 0 so changed
 *     default to display # of threads in use instead.
 *     define this to display nice fields (values always 0)
 * #define SHOW_NICE 1 
 */


/*
 * Defines
 */

#define	_KMEMUSER	/* Needed to access appropriate system include file   */
			/* structures.					      */


#define UNIX	"/unix"	/* Miscellaneous paths.				      */
#define PROCFS	"/proc"
#define PATH_KMEM "/dev/kmem"


/*
 * Maximum and minumum priorities.
 */
#ifndef PRIO_MAX
#define PRIO_MAX	20
#endif
#ifndef PRIO_MIN
#define PRIO_MIN	-20
#endif


/*
 * Scaling factor used to adjust kernel load average numbers.
 * Unixware note: as I can't find any documentation on the avenrun
 * symbol, I'm not entirely sure if this is correct.
 */
#ifndef FSCALE
#define FSHIFT  8		/* bits to right of fixed binary point */
#define FSCALE  (1<<FSHIFT)
#endif

/* Macro to convert between avenrun load average numbers and normal	*/
/* load average.							*/
#define loaddouble(x) ((double)(x) / FSCALE)


/* Convert number of pages to size in kB.				*/
#define pagetok(size) ((size * PAGESIZE) >> 10)


/*
 * Unixware doesn't explicitly track zombie processes.
 * However, they can be identified as processes with no threads.
 * Also, define an additional artificial process state to keep
 * track of how many zombies there are.
 */
#define	ZOMBIE(z)	((z)->p.p_nlwp == 0)
#define	ZOMBIESTATE	6


/*
 * Definitions for the index in the nlist array.
 */
#define X_AVENRUN	0
#define X_NEXTPID	1
#define X_V		2
#define X_OFFSETS	3
#define X_TOTALMEM	4


/*
 * Hash function for the table of processes used to keep old information.
 */
#define	HASH(x)	((x << 1) % numoldprocs)


/* ************************************************************************** */

/*
 * Include files
 */

#include "utils.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <nlist.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/procfs.h>
#include <sys/sysinfo.h>
#include <sys/sysmacros.h>
#include <sys/vmmeter.h>
#include <sys/ksym.h>
#include <vm/anon.h>
#include <sys/priocntl.h>
#include <sys/rtpriocntl.h>
#include <sys/tspriocntl.h>
#include <sys/procset.h>
#include <sys/var.h>
#include <sys/metrics.h>
#include <sys/time.h>

#include "top.h"
#include "machine.h"


/* ************************************************************************** */


/*
 * Structures
 */

/*
 * A process is just a thread container under Unixware.
 * So, define the following structure which will aggregate
 * most of what we want.
 */
struct	uwproc
{
	int		dummy;
	struct	psinfo	ps;
	struct	proc	p;
	struct	lwp	l;
	double		pctcpu;
	double		wcpu;
};

/* defines to simplify access to some of contents of uwproc struct */
#define PR_pri   ps.pr_lwp.pr_pri
#define PR_state ps.pr_lwp.pr_state
#define PR_time  ps.pr_lwp.pr_time


/*
 * oldproc
 * Oldproc is used to hold process CPU history necessary to
 * calculate %CPU and WCPU figures.
 */
struct oldproc
{
	int	oldpid;
	double	oldtime;	/* Duration of process in ns.	*/
	double	oldpct;
};


/*
 * nlist
 * An array of the kernel sybols used by this module.
 */
static struct nlist nlst[] =
{
	{"avenrun"},			/* 0 X_AVENRUN	 */
	{"nextpid"},			/* 1 X_NEXTPID	 */
	{"v"},				/* 2 X_V	 */
	{"met_localdata_ptrs_p"},	/* 3 X_OFFSETS */
	{"totalmem"},			/* 4 X_TOTALMEM  */
	{NULL}
};


/*
 * Get_process_info passes back a handle.  This is what it looks like:
 */

struct handle
{
	struct uwproc	**next_proc;	/* points to next valid proc pointer */
	int 		  remaining;	/* number of pointers remaining */
};


/* ************************************************************************** */


/*
 * Globals
 */

static unsigned long	avenrun_offset;
static unsigned long	nextpid_offset;
static unsigned long	v_offset;
static unsigned long	offset_offset;
static unsigned long	totalmem_offset;

static int		kmem = -1;	/* FD to /dev/kmem.		      */

static int		maxprocs;	/* Maximum number of processes that   */
					/* can be running on the system.      */

static int		numoldprocs;	/* Size of oldproc hash.	      */

static int		numprocs = 0;	/* Number of processes currently      */
					/* running on the system.  Updated    */
					/* each time getptable() is called.   */
static int		bytes;		/* Size of array of process structs.  */
static struct uwproc   *pbase;		/* Pointer to array of process info.  */
static struct uwproc  **pref;		/* Vector of uwbase pointers.	      */
static struct oldproc  *oldbase;	/* Pointer to array of old proc info. */

static DIR	       *procdir;

extern int		errno;
extern char	       *myname;


/*
 * An abbreviation of the states each process may be in.
 */
char *state_abbrev[] =
	{ "oncpu", "run", "sleep", "stop", "idle", "zomb", NULL};


/*
 * The states each process may be in.
 */
int process_states[6];
char *procstatenames[] =
{
	" on cpu, ", " running, ", " sleeping, ", " stopped, ",
	" idle, ", " zombie", NULL
};


/*
 * CPU usage tracked by the kernel.
 */
#define CPUSTATES	4

int cpu_states[CPUSTATES];
char *cpustatenames[] =
	{"idle", "wait", "user", "sys", NULL};


/*
 * These are for detailing the memory statistics.
 */
int memory_stats[3];
char *memorynames[] =
	{"K phys, ", "K swap, ", "K swap free", NULL};


/*
 *  These definitions control the format of the per-process area
 */
static char header[] =
#ifdef SHOW_NICE
    "  PID X        PRI NICE  SIZE   RES STATE   TIME   WCPU    CPU COMMAND";
#else
    "  PID X        PRI  THR  SIZE   RES STATE   TIME   WCPU    CPU  COMMAND";
#endif

/* 0123456   -- field to fill in starts at header+6 */  /* XXXX */
#define UNAME_START 6
#define PROC_FORMAT "%5d %-8.8s %3d %4d %5s %5s %-5s %6s %5.2f%% %5.2f%% %s"

/* these are names given to allowed sorting orders -- first is default */
char *ordernames[] = 
{"cpu", "state", "size", "res", "time", "pid", "uid", "rpid", "ruid", NULL};


/* ************************************************************************** */


/*
 * Prototypes
 */

int		machine_init(struct statics *);
char		*format_header(char *);
void		get_system_info(struct system_info *);
caddr_t		get_process_info(struct system_info *,
				 struct process_select *, int (*)());
char		*format_next_process(caddr_t, char *(*)());
int		check_nlist(register struct nlist *);
int		getkval(unsigned long, void *, int, char *);
int		proc_compare(void *, void *);
void		getptable(struct uwproc *);
uid_t		proc_owner(pid_t);
int		setpriority(int, int, int);
void		get_swapinfo(long *, long *);

/* forward definitions for comparison functions */
int compare_state(void *, void *);
int compare_cpu(void *, void *);
int compare_size(void *, void *);
int compare_res(void *, void *);
int compare_time(void *, void *);
int compare_pid(void *, void *);
int compare_uid(void *, void *);
int compare_rpid(void *, void *);
int compare_ruid(void *, void *);

int (*proc_compares[])() = {
    compare_cpu,
    compare_state,
    compare_size,
    compare_res,
    compare_time,
    compare_pid,
    compare_uid,
    compare_rpid,
    compare_ruid,
    NULL };


extern long	percentages ();
extern void	quit ();



/* ************************************************************************** */

/*
 * machine_int
 * Perform once-off initialisation tasks.
 */
int
machine_init (struct statics *statics)
{
	static	 struct var 		 v;
	struct	oldproc			*op;
	int				 i;

	/*
	 * Fill in the statics information.
	 */
	statics->procstate_names	= procstatenames;
	statics->cpustate_names		= cpustatenames;
	statics->memory_names		= memorynames;

        statics->order_names            = ordernames;

	/*
	 * Go through the kernel symbols required for this
	 * module, and check that they are available.
	 */
	if (nlist (UNIX, nlst))
	{
		fprintf (stderr, "Unable to nlist %s\n", UNIX);
		return -1;
	}

	/*
	 * Make sure they were all found.
	 */
	if (check_nlist (nlst) > 0)
		return -1;

	/*
	 * Open KMEM device for future use.
	 */
	kmem	= open(PATH_KMEM, O_RDONLY);
	if (kmem == -1)
	{
		perror(PATH_KMEM);
		return -1;
	}

	/*
	 * Extract the maximum number of running processes.
	 */
	getkval(nlst[X_V].n_value, (void *)&v, sizeof(v), nlst[X_V].n_name);
	maxprocs		= v.v_proc;

	/*
	 * Save pointers.
	 */
	avenrun_offset		= nlst[X_AVENRUN].n_value;
	nextpid_offset		= nlst[X_NEXTPID].n_value;
	v_offset		= nlst[X_V].n_value;
	offset_offset		= nlst[X_OFFSETS].n_value;
	totalmem_offset		= nlst[X_TOTALMEM].n_value;

	/*
	 * Allocate space for proc structure array and array of pointers.
	 */
	bytes	 = maxprocs * sizeof (struct uwproc);

	pbase	 = (struct uwproc *) malloc (bytes);
	pref	 = (struct uwproc **)
		   malloc (maxprocs * sizeof (struct uwproc *));

	numoldprocs = maxprocs * 2;
	oldbase	 = (struct oldproc *)malloc(numoldprocs *
					    sizeof(struct oldproc));

	if (pbase == (struct uwproc *) NULL ||
	    pref == (struct uwproc **) NULL ||
	    oldbase == (struct oldproc *) NULL)
	{
		fprintf(stderr, "%s: can't allocate sufficient memory\n",
			myname);
		return -1;
	}

	/*
	 * Obtain a handle on the /proc filesystem, and change into it.
	 */
	if (!(procdir = opendir (PROCFS)))
	{
		fprintf (stderr, "Unable to open %s\n", PROCFS);
		return -1;
	}

	if (chdir (PROCFS))
	{
		fprintf (stderr, "Unable to chdir to %s\n", PROCFS);
		return -1;
	}

	/*
	 * Initialise the oldproc structures.
	 */
	for (op = oldbase, i = 0; i < numoldprocs; i++)
	{
		op[i].oldpid	= -1;
	}


	/*
	 * All done!
	 */
	return (0);
}

/* ************************************************************************** */

/*
 * format_header
 */

char *
format_header (char *uname_field)
{
	register char *ptr;

	ptr = header + UNAME_START;
	while (*uname_field != '\0')
		*ptr++ = *uname_field++;

	return (header);
}

/* ************************************************************************** */

/*
 * Extract information out of system data structures.
 */
void
get_system_info (struct system_info *si)
{
	long			avenrun[3];
	static time_t		cp_old[CPUSTATES];
	static time_t		cp_diff[CPUSTATES];/*for cpu state percentages*/
	register int		i;

	struct	met_localdata_ptrs	*mlpp;
	struct	met_localdata_ptrs	 mlp;
	struct	plocalmet		 plm;
	struct	metp_cpu		 mc;

	unsigned	long	totalmem;

	long			totalswap;
	long			totalswapfree;



	/*
	 * Get process id of last process.
	 */
	getkval (nextpid_offset, (void *)&(si->last_pid),
			sizeof (si->last_pid), "nextpid");
	si->last_pid--;


	/*
	 * Get load average array.
	 */
	getkval (avenrun_offset, (void *) avenrun, sizeof (avenrun),
			"avenrun");
	
	/*
	 * Convert load averages to doubles.
	 */
	for (i = 0; i < 3; i++)
		si->load_avg[i] = loaddouble (avenrun[i]);

	/*
	 * Extract CPU percentages.
	 */

	/*
	 * 1. Retrieve pointer to metrics data structure.
	 */
	getkval(offset_offset, (void *)&mlpp,
		sizeof (struct met_localdata_ptrs_p *),
		"met_localdata_ptrs_pp");

	/*
	 * 2. Retrieve metrics data structure. (ptr to metrics engine)
	 */
	getkval((unsigned long)mlpp, (void *)&mlp, sizeof (mlp),
		"met_localdata_ptrs_p");

	/*
	 * 3. Retrieve (first local metrics) plocalmet data structure.
	 */
	getkval((unsigned long)mlp.localdata_p[0], (void *)&plm,
		sizeof(struct plocalmet), "plocalmet");

	percentages(CPUSTATES, cpu_states, plm.metp_cpu.mpc_cpu,
		    cp_old, cp_diff);

	/*
	 * Retrieve memory information.
	 */

	/*
	 * 1.	Get physical memory size.
	 */
	getkval(totalmem_offset, (void *)&totalmem, sizeof (unsigned long),
		"totalmem");

	/*
	 * 2.	Get physical swap size, and amount of swap remaining.
	 */
	get_swapinfo(&totalswap, &totalswapfree);


	/*
	 * Insert memory information into memory_stat structure.
	 */
	memory_stats[0]	= totalmem >> 10;
	memory_stats[1]	= pagetok(totalswap);
	memory_stats[2]	= pagetok(totalswapfree);

	/*
	 * Set arrays and strings.
	 */
	si->cpustates	= cpu_states;
	si->memory 	= memory_stats;
}

/* ************************************************************************** */

/*
 * Extract process information.
 */
static struct handle handle;
caddr_t
get_process_info (
		   struct system_info *si,
		   struct process_select *sel,
		   int (*compare) ())
{
	register int	i;
	register int	j = 0;
	register int	active_procs;
	register int	total_procs;
	register struct	uwproc **prefp;
	register struct	uwproc *uwp;


	/*
	 * These are copied out of sel for speed.
	 */
	int		show_idle;
	int		show_system;
	int		show_uid;

	/*
	 * Read all the proc structures.
	 */
	getptable (pbase);


	/*
	 * Get a pointer to the states summary array.
	 */
	si->procstates	= process_states;


	/*
	 * Set up flags which define what we are going to select.
	 */
	show_idle	= sel->idle;
	show_system	= sel->system;
	show_uid	= sel->uid != -1;

	/*
	 * Count up process states and get pointers to interesting procs.
	 */
	total_procs	= 0;
	active_procs	= 0;


	(void) memset (process_states, 0, sizeof (process_states));
	prefp		= pref;

	for (i = 0; i < numprocs; i++)
	{

		uwp	= &pbase[i];

		/*
		 * Place pointers to each valid proc structure in pref[].
		 * Processes with P_SYS set are system processes---these
		 * get ignored unless show_system is set.
		 */

		uwp->dummy	= 0;

		if ((show_system || ((uwp->p.p_flag & P_SYS) == 0)))
		{
			total_procs++;

			if (ZOMBIE(uwp))
			{
				process_states[ZOMBIESTATE]++;
			}
			else
			{
				if ( (show_idle || uwp->l.l_stat == SRUN ||
				      uwp->l.l_stat == SONPROC) &&
				     (!show_uid ||
				      uwp->ps.pr_uid == (uid_t)sel->uid))
				{
					process_states[uwp->l.l_stat]++;

					prefp[j]	= uwp;

					j++;
					active_procs++;
				}
			}
		}
	}


	/*
	 * If requested, sort the "interesting" processes.
	 */
	if (compare != NULL)
		qsort ((void *) pref, active_procs,
		       sizeof (struct uwproc *), compare);

	/*
	 * Remember active and total counts.
	 */
	si->p_total		= total_procs;
	si->P_ACTIVE		= active_procs;

	/*
	 * Pass back a handle.
	 */
	handle.next_proc	= pref;
	handle.remaining	= active_procs;

	return ((caddr_t) & handle);
}


/* ************************************************************************** */
/*
 * Format the output string for a process.
 */
char fmt[MAX_COLS];			/* static area where result is built */

char *
format_next_process (
		      caddr_t handle,
		      char *(*get_userid) ())
{
	register struct uwproc		*pp;
	struct handle			*hp;
	register long			 cputime;
	register double			 pctcpu;

	/*
	 * Find and remember the next proc structure.
	 */
	hp	= (struct handle *) handle;
	pp	= *(hp->next_proc++);

	hp->remaining--;

	(void) snprintf (fmt, MAX_COLS,
		PROC_FORMAT,
		pp->ps.pr_pid,
		(*get_userid) (pp->ps.pr_uid),
		pp->PR_pri,
#ifdef SHOW_NICE
		pp->ps.pr_lwp.pr_nice,
#else
                (u_short)pp->p.p_nlwp < 999 ? (u_short)pp->p.p_nlwp : 999,
#endif
		format_k(pagetok (pp->ps.pr_size)),
		format_k(pagetok (pp->ps.pr_rssize)),
		state_abbrev[pp->PR_state],
		format_time(pp->PR_time.tv_sec),
		pp->wcpu,
		pp->pctcpu,
		pp->ps.pr_fname);

	/*
	 * Return the result.
	 */
	return (fmt);
}

/* ************************************************************************** */

/*
 * check_nlist(nlst) - checks the nlist to see if any symbols were not
 *		found.  For every symbol that was not found, a one-line
 *		message is printed to stderr.  The routine returns the
 *		number of symbols NOT found.
 */
int
check_nlist (register struct nlist *nlst)
{
	register int i;

	/*
	 * Check to see if we got ALL the symbols we requested.
	 * This will write one line to stderr for every symbol not found.
	 */

	i = 0;

	while (nlst->n_name != NULL)
	{
		if (nlst->n_type == 0)
		{
			/*
			 * This one wasn't found.
			 */
			fprintf(stderr, "kernel: no symbol named `%s'\n",
				nlst->n_name);
			i = 1;
		}
		nlst++;
	}

	return (i);
}

/* ************************************************************************** */

/*
 *  getkval(offset, ptr, size, refstr) - get a value out of the kernel.
 *	"offset" is the byte offset into the kernel for the desired value,
 *  	"ptr" points to a buffer into which the value is retrieved,
 *  	"size" is the size of the buffer (and the object to retrieve),
 *  	"refstr" is a reference string used when printing error meessages,
 *	    if "refstr" starts with a '!', then a failure on read will not
 *  	    be fatal (this may seem like a silly way to do things, but I
 *  	    really didn't want the overhead of another argument).
 *
 */

int
getkval (
          unsigned long offset,
          void	*ptr,
          int	 size,
          char *refstr)
{
	if (lseek (kmem, (long) offset, 0) == -1)
	{
		if (*refstr == '!')
			refstr++;
		fprintf (stderr, "%s: lseek to %s: %s\n",
			 myname, refstr, strerror(errno));
		quit (22);
	}
	if (read (kmem, (char *) ptr, size) == -1)
	{
		if (*refstr == '!')
		{
			/*
			 * We lost the race with the kernel,
			 * process isn't in memory.
			 */
			return (0);
		}
		else
		{
			(void) fprintf (stderr, "%s: reading %s: %s\n",
			myname, refstr, strerror(errno));
			quit (23);
		}
	}

	return (1);
}


/* ************************************************************************** */

/*
 * Extract process table and derive %cpu figures.
*/
void
getptable (struct uwproc *baseptr)
{
	struct dirent		*direntp;
	struct uwproc		*curruwproc;
	struct oldproc		*op;

	static struct	timeval	 lasttime = {0, 0};
	struct		timeval	 thistime;

	double			 alpha, beta, timediff;

	int			 pos, i;

	numprocs		= 0;


	gettimeofday(&thistime, NULL);

	/*
 	 * Calculate background information for CPU statistic.
	 */
	if (lasttime.tv_sec)
	{
		timediff	= ((double)thistime.tv_sec * 1.0e7 +
				   (double)thistime.tv_usec * 10.0) -
				  ((double)lasttime.tv_sec * 1.0e7 +
				   (double)lasttime.tv_usec * 10.0);
	}
	else
	{
		timediff	= 1.0e7;
	}

	/*
	 * Constants for exponential average.  avg = alpha * new + beta * avg
	 * The goal is 50% decay in 30 sec.  However if the sample period
	 * is greater than 30 sec, there's not a lot we can do.
	 */
	if (timediff < 30.0e7)
	{
		alpha	= 0.5 * (timediff / 30.0e7);
		beta	= 1.0 - alpha;
	}
	else
	{
		alpha	= 0.5;
		beta	= 0.5;
	}


	/*
	 * While there are still entries (processes) in the /proc
	 * filesystem to be examined...
	 */
	for (rewinddir (procdir); direntp = readdir (procdir);)
	{
		char	buf[MAXPATHLEN];
		int	fd;
		int	rc;
		int	pos;

		/*
		 * Ignore parent and current directory entries.
		 */
		if (direntp->d_name[0] == '.')
			continue;

		/*
		 * Construct filename representing the psinfo
		 * structure on disk.
		 */
		snprintf(buf, MAXPATHLEN, PROCFS "/%s/psinfo", direntp->d_name);
		
		if ((fd	= open (buf, O_RDONLY)) == -1)
		{
			fprintf(stderr, "Can't open %s: %s\n", buf,
				strerror(errno));
			continue;
		}

		curruwproc		= &baseptr[numprocs];

		/*
		 * Read in psinfo structure from disk.
		 */
		if (read(fd, (void *)&curruwproc->ps,
			 sizeof(psinfo_t)) != sizeof(psinfo_t))
		{
			close(fd);
			fprintf(stderr, "Can't read %s: %s\n", buf,
				strerror(errno));
			continue;
		}
		close(fd);

		/*
		 * Extract the proc structure from the kernel.
		 */
		rc = getkval((unsigned long)curruwproc->ps.pr_addr,
				(void *)&curruwproc->p, sizeof(struct proc),
				"!proc");
		if (rc == -1)
		{
			fprintf(stderr, "Can't read proc structure\n");
			continue;
		}


		/*
		 * Extract the lwp structure from the kernel.
		 */
		rc = getkval((unsigned long)curruwproc->ps.pr_lwp.pr_addr,
				(void *)&curruwproc->l, sizeof(struct lwp),
				"!lwp");
		if (rc == -1)
		{
			fprintf(stderr, "Can't read lwp structure\n");
			continue;
		}


		/*
		 * Work out %cpu figure for process.
		 */
		pos	= HASH(curruwproc->p.p_epid);

		while(1)
		{
			if (oldbase[pos].oldpid == -1)
			{
				/*
				 * Process not present in hash.
				 */
				break;
			}
			if (oldbase[pos].oldpid == curruwproc->p.p_epid)
			{
				double	new;

				/*
				 * Found old data.
				 */
				new = (double)curruwproc->PR_time.tv_sec
					* 1.0e9 +
					curruwproc->PR_time.tv_nsec;

				curruwproc->pctcpu = ((
					(double)curruwproc->PR_time.tv_sec *
				 	1.0e9 +
					(double)curruwproc->PR_time.tv_nsec)
					- (double)oldbase[pos].oldtime) /
					timediff;
				

				curruwproc->wcpu = oldbase[pos].oldpct *
					beta + curruwproc->pctcpu * alpha;
				
				break;
			}

			pos++;
			if (pos == numoldprocs)
			{
				pos	= 0;
			}
		}

		if (oldbase[pos].oldpid == -1)
		{
			/*
			 * New process.
			 * All of its cputime used.
			 */
			if (lasttime.tv_sec)
			{
				curruwproc->pctcpu =
					(curruwproc->PR_time.tv_sec * 1.0e9
					 + curruwproc->PR_time.tv_nsec) /
					timediff;

				curruwproc->wcpu   = curruwproc->pctcpu;
			}
			else
			{
				curruwproc->pctcpu	= 0.0;
				curruwproc->wcpu	= 0.0;
			}
		}

		numprocs++;
	}


	/*
	 * Save current CPU time for next time around
	 * For the moment recreate the hash table each time, as the code
	 * is easier that way.
	 */

	/*
	 * Empty the hash table.
	 */
	for (pos = 0; pos < numoldprocs; pos++)
	{
		oldbase[pos].oldpid	= -1;
	}

	/*
	 * Recreate the hash table from the curruwproc information.
	 */
	for (i = 0; i < numprocs; i++)
	{

		/*
		 * Find an empty spot in the hash table.
		 */
		pos	= HASH(baseptr[i].p.p_epid);


		while (1)
		{
			if (oldbase[pos].oldpid == -1)
				break;
			pos++;

			if (pos == numoldprocs)
				pos	= 0;
		}

		oldbase[pos].oldpid	= baseptr[i].p.p_epid;

		oldbase[pos].oldtime	= baseptr[i].PR_time.tv_sec *
					   1.0e9 +
					  baseptr[i].PR_time.tv_nsec;

		oldbase[pos].oldpct	= baseptr[i].wcpu;
	}

	lasttime = thistime;
}


/* ************************************************************************** */

/*
 * Return the owner of the specified process, for use in commands.c
 * as we're running setuid root.
 */
uid_t
proc_owner (pid_t pid)
{
	register struct uwproc		*uwp;
	struct	proc			 p;
	int				 i;

	for (i = 0, uwp = pbase; i < numprocs; i++, uwp++)
	{
		if (uwp->p.p_epid == pid)
			return (uwp->ps.pr_uid);
	}

	return -1;
}

/* ************************************************************************** */

int
setpriority (int dummy, int who, int niceval)
{
	int		scale;
	int		prio;
	pcinfo_t	pcinfo;
	pcparms_t	pcparms;
	tsparms_t	*tsparms;

	strcpy (pcinfo.pc_clname, "TS");

	if (priocntl (0, 0, PC_GETCID, (caddr_t) & pcinfo) == -1)
		return -1;

	prio = niceval;
	if (prio > PRIO_MAX)
		prio = PRIO_MAX;
	else if (prio < PRIO_MIN)
		prio = PRIO_MIN;

	tsparms			= (tsparms_t *) pcparms.pc_clparms;
	scale			= ((tsinfo_t *) pcinfo.pc_clinfo)->ts_maxupri;
	tsparms->ts_uprilim	= tsparms->ts_upri = -(scale * prio) / 20;
	pcparms.pc_cid		= pcinfo.pc_cid;

	if (priocntl (P_PID, who, PC_SETPARMS, (caddr_t) & pcparms) == -1)
		return (-1);

	return (0);
}

/* ************************************************************************** */

/*
 * get_swapinfo
 * Get total and free swap.
 * Snarfed from m_sunos5.c
 */
void
get_swapinfo(long *total, long *fr)
{
	register int		cnt, i;
	register int		t, f;

	struct swaptable	*swt;
	struct swapent		*ste;

	static char		path[256];

	/*
	 * Get total number of swap entries.
	 */
	cnt	= swapctl(SC_GETNSWP, 0);

	/*
	 * Allocate enough space to hold count + n swapents.
	 */
	swt = (struct swaptable *)malloc(sizeof(int) +
	cnt * sizeof(struct swapent));
	if (swt == NULL)
	{
		*total = 0;
		*fr = 0;

		return;
	}
	swt->swt_n = cnt;

	/*
	 * Fill in ste_path pointers: we don't care about the paths,
	 * so we point them all to the same buffer.
	 */
	ste = &(swt->swt_ent[0]);
	i = cnt;
	while (--i >= 0)
	{
		ste++->ste_path = path;
	}

	/*
	 * Grab all swap info.
	 */
	swapctl(SC_LIST, swt);

	/*
	 * Walk thru the structs and sum up the fields.
	 */
	t = f = 0;
	ste = &(swt->swt_ent[0]);
	i = cnt;
	while (--i >= 0)
	{
		/*
		 * Dont count slots being deleted.
		 */
		if (!(ste->ste_flags & ST_INDEL))
		{
			t += ste->ste_pages;
			f += ste->ste_free;
		}
		ste++;
	}

	/*
	 * Fill in the results.
	 */
	*total = t;
	*fr = f;

	free(swt);
}


/* ************************************************************************** */

/* comparison routine for qsort */

/*
 *  proc_compare - comparison function for "qsort"
 *	Compares the resource consumption of two processes using five
 *  	distinct keys.  The keys (in descending order of importance) are:
 *  	percent cpu, cpu ticks, state, resident set size, total virtual
 *  	memory usage.  The process states are ordered as follows (from least
 *  	to most important):  idle, stop, sleep, run, on processor.  The
 *  	array declaration below maps a process state index into a number
 *  	that reflects this ordering.
 */

unsigned char sorted_state[] =
{
  5,				/* run on a processor   */
  4,				/* run			*/
  3,				/* sleep		*/
  2,				/* stop			*/
  1,				/* idle			*/
};


#if 0       /* original compare rtn for single sort order */
int
proc_compare(void *v1, void *v2)
{
    struct uwproc		**pp1 = (struct uwproc **)v1;
    struct uwproc		**pp2 = (struct uwproc **)v2;

    register struct uwproc 	 *p1	= *pp1;
    register struct uwproc 	 *p2  = *pp2;

    register long result;

    double	d;

    /* use %cpu to break the tie. */
    d = p2->pctcpu - p1->pctcpu;

    if (d == 0.0)
    {
	/* use cpticks to break the tie */
	if ((result = p2->PR_time.tv_sec - p1->PR_time.tv_sec) == 0)
	  {
	    /* use process state to break the tie. */
	    if ((result = (long) (sorted_state[p2->PR_state] -
				  sorted_state[p1->.PR_state])) == 0)
	      {
		/* use priority to break the tie */
		if ((result = p2->PR_pri - p1->PR_pri) == 0)
		  {
		    /* use resident set size (rssize) to break the tie */
		    if ((result = p2->ps.pr_rssize - p1->ps.pr_rssize) == 0)
		      {
			/* use total memory to break the tie */
			result = (p2->ps.pr_size - p1->ps.pr_size);
		      }
		  }
	      }
	  }
    } 
    else
    {
	if (d < 0.0)
	{
		result	= -1;
	}
	else
	{
		result	= 1;
	}
    }

    return (result);
}
#endif

/* ----------------- comparison routines for qsort ---------------- */

/* First, the possible comparison keys.  These are defined in such a way
   that they can be merely listed in the source code to define the actual
   desired ordering.
 */

#define ORDERKEY_PCTCPU  if (dresult = p2->pctcpu - p1->pctcpu,\
			     (result = dresult > 0.0 ? 1 : \
			     dresult < 0.0 ? -1 : 0) == 0)

#define ORDERKEY_CPTICKS if ((result = p2->PR_time.tv_sec - p1->PR_time.tv_sec) == 0)
#define ORDERKEY_STATE   if ((result = (long) (sorted_state[p2->PR_state] - \
			       sorted_state[p1->PR_state])) == 0)

#define ORDERKEY_PRIO    if ((result = p2->PR_pri - p1->PR_pri) == 0)
#define ORDERKEY_RSSIZE  if ((result = p2->ps.pr_rssize-p1->ps.pr_rssize) == 0)
#define ORDERKEY_MEM     if ((result = (p2->ps.pr_size - p1->ps.pr_size)) == 0)

#define ORDERKEY_PID     if ((result = (p2->ps.pr_pid  - p1->ps.pr_pid))  == 0)
#define ORDERKEY_UID     if ((result = (p2->ps.pr_uid  - p1->ps.pr_uid))  == 0)
#define ORDERKEY_RPID    if ((result = (p1->ps.pr_pid  - p2->ps.pr_pid))  == 0)
#define ORDERKEY_RUID    if ((result = (p1->ps.pr_uid  - p2->ps.pr_uid))  == 0)


/* compare_cpu - the comparison function for sorting by cpu % (deflt) */
int
compare_cpu(void *v1, void *v2)
{
    struct uwproc	    **pp1 = (struct uwproc **)v1;
    struct uwproc	    **pp2 = (struct uwproc **)v2;
    register struct uwproc  *p1;
    register struct uwproc  *p2;
    register long result;
    double dresult;

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

    return (result);
}

/* compare_state - comparison function for sorting by state,pri,time,size */
int
compare_state (void *v1, void *v2)
{
    struct uwproc		**pp1 = (struct uwproc **)v1;
    struct uwproc		**pp2 = (struct uwproc **)v2;
    register struct uwproc  *p1;
    register struct uwproc  *p2;
    register long result;
    double dresult;

    /* remove one level of indirection */
    p1 = *pp1;
    p2 = *pp2;

    ORDERKEY_STATE
    ORDERKEY_PRIO
    ORDERKEY_CPTICKS
    ORDERKEY_RSSIZE
    ORDERKEY_MEM
    ORDERKEY_PCTCPU
    ;

    return (result);
}

/* compare_size - the comparison function for sorting by total memory usage */
int
compare_size(void *v1, void *v2)
{
    struct uwproc	    **pp1 = (struct uwproc **)v1;
    struct uwproc	    **pp2 = (struct uwproc **)v2;
    register struct uwproc  *p1;
    register struct uwproc  *p2;
    register long           result;
    double                  dresult;

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

    return (result);
  }

/* compare_res - the comparison function for sorting by resident set size */
int
compare_res(void *v1, void *v2)
{
    struct uwproc	    **pp1 = (struct uwproc **)v1;
    struct uwproc	    **pp2 = (struct uwproc **)v2;
    register struct uwproc  *p1;
    register struct uwproc  *p2;
    register long result;
    double dresult;

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

    return (result);
  }

/* compare_time - the comparison function for sorting by total cpu time */
int
compare_time(void *v1, void *v2)
{
    struct uwproc	    **pp1 = (struct uwproc **)v1;
    struct uwproc	    **pp2 = (struct uwproc **)v2;
    register struct uwproc  *p1;
    register struct uwproc  *p2;
    register long result;
    double dresult;

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

    return (result);
  }

/* compare_pid - the comparison function for sorting by pid */
int
compare_pid(void *v1, void *v2)
{
    struct uwproc	    **pp1 = (struct uwproc **)v1;
    struct uwproc	    **pp2 = (struct uwproc **)v2;
    register struct uwproc  *p1;
    register struct uwproc  *p2;
    register long result;
    double dresult;

    /* remove one level of indirection */
    p1 = *pp1;
    p2 = *pp2;

    ORDERKEY_PID
    ORDERKEY_CPTICKS
    ORDERKEY_PCTCPU
    ORDERKEY_STATE
    ORDERKEY_PRIO
    ORDERKEY_MEM
    ORDERKEY_RSSIZE
    ;

    return (result);
  }

/* compare_uid - the comparison function for sorting by user ID */
int
compare_uid(void *v1, void *v2)
{
    struct uwproc	    **pp1 = (struct uwproc **)v1;
    struct uwproc	    **pp2 = (struct uwproc **)v2;
    register struct uwproc  *p1;
    register struct uwproc  *p2;
    register long result;
    double dresult;

    /* remove one level of indirection */
    p1 = *pp1;
    p2 = *pp2;

    ORDERKEY_UID
    ORDERKEY_CPTICKS
    ORDERKEY_PCTCPU
    ORDERKEY_STATE
    ORDERKEY_PRIO
    ORDERKEY_MEM
    ORDERKEY_RSSIZE
    ;

    return (result);
  }

/* compare_rpid - the comparison function for sorting by pid ascending */
int
compare_rpid(void *v1, void *v2)
{
    struct uwproc	    **pp1 = (struct uwproc **)v1;
    struct uwproc	    **pp2 = (struct uwproc **)v2;
    register struct uwproc  *p1;
    register struct uwproc  *p2;
    register long result;
    double dresult;

    /* remove one level of indirection */
    p1 = *pp1;
    p2 = *pp2;

    ORDERKEY_RPID
    ORDERKEY_CPTICKS
    ORDERKEY_PCTCPU
    ORDERKEY_STATE
    ORDERKEY_PRIO
    ORDERKEY_MEM
    ORDERKEY_RSSIZE
    ;

    return (result);
  }

/* compare_uid - the comparison function for sorting by user ID ascending */
int
compare_ruid(void *v1, void *v2)
{
    struct uwproc	    **pp1 = (struct uwproc **)v1;
    struct uwproc	    **pp2 = (struct uwproc **)v2;
    register struct uwproc  *p1;
    register struct uwproc  *p2;
    register long result;
    double dresult;

    /* remove one level of indirection */
    p1 = *pp1;
    p2 = *pp2;

    ORDERKEY_RUID 
    ORDERKEY_CPTICKS
    ORDERKEY_PCTCPU
    ORDERKEY_STATE
    ORDERKEY_PRIO
    ORDERKEY_MEM
    ORDERKEY_RSSIZE
    ;

    return (result);
  }

