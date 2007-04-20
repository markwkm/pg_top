/*
 * top - a top users display for Unix
 *
 * SYNOPSIS:  any Solbourne running OS/MP 4.1A
 *
 * DESCRIPTION:
 * This is the machine-dependent module for OS/MP 4.1A
 * This makes top work on the following systems:
 *	Solbourne machines running OS/MP 4.1A only
 *
 * LIBS:  -lkvm
 *
 * AUTHOR:  William LeFebvre <wnl@groupsys.com>
 *          Brett McCoy <brtmac@maverick.ksu.ksu.edu>
 */

#include <sys/types.h>
#include <sys/signal.h>
/* make sure param.h gets loaded with KERNEL defined to get PZERO & NZERO */
#define KERNEL
#include <sys/param.h>
#undef KERNEL

#include <stdio.h>
#include <kvm.h>
#include <nlist.h>
#include <math.h>
#include <sys/dir.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/dk.h>
#include <sys/vm.h>
#include <sys/file.h>
#include <sys/time.h>
#include <vm/page.h>

#include "top.h"
#include "machine.h"
#include "utils.h"

/* declarations for load_avg */
#include "loadavg.h"

/* get_process_info passes back a handle.  This is what it looks like: */

struct handle
{
    struct proc **next_proc;	/* points to next valid proc pointer */
    int remaining;		/* number of pointers remaining */
};

/* define what weighted cpu is.  */
#define weighted_cpu(pct, pp) ((pp)->p_time == 0 ? 0.0 : \
			 ((pct) / (1.0 - exp((pp)->p_time * logcpu))))

/* what we consider to be process size: */
#define PROCSIZE(pp) ((pp)->p_tsize + (pp)->p_dsize + (pp)->p_ssize)

/* definitions for indices in the nlist array */
#define X_AVENRUN	0
#define X_CCPU		1
#define X_MPID		2
#define X_NPROC		3
#define X_PROC		4
#define X_TOTAL		5
#define X_CP_TIME	6
#define X_PAGES		7
#define X_EPAGES	8
#define X_NCPUS		9
#define X_CP_MP_TIME	10

static struct nlist nlst[] = {
    { "_avenrun" },		/* 0 */
    { "_ccpu" },		/* 1 */
    { "_mpid" },		/* 2 */
    { "_nproc" },		/* 3 */
    { "_proc" },		/* 4 */
    { "_total" },		/* 5 */
    { "_cp_time" },		/* 6 */
    { "_pages" },		/* 7 */
    { "_epages" },		/* 8 */
    { "_ncpus" },		/* 9 */
    { "_cp_mp_time" },		/* 10 */
    { 0 }
};

/*
 *  These definitions control the format of the per-process area
 */

static char header[] =
  "  PID X        PRI  NI  SIZE   RES STATE    TIME   WCPU    CPU COMMAND";
/* 0123456   -- field to fill in starts at header+6 */
#define UNAME_START 6

#define Proc_format \
	"%5d %-8.8s %3d %3d %5s %5s %-5s %6s %5.2f%% %5.2f%% %.16s"


/* process state names for the "STATE" column of the display */
/* the extra nulls in the string "run" are for adding a slash and
   the processor number when needed */

char *state_abbrev[] =
{
    "", "sleep", "WAIT", "run/\0\0", "start", "zomb", "stop"
};

/* values that we stash away in _init and use in later routines */

static double logcpu;
kvm_t *kd;

/* these are retrieved from the kernel in _init */

static unsigned long proc;
static          int  nproc;
static load_avg ccpu;
static unsigned long pages;
static unsigned long epages;
static		int  ncpus;

/* these are offsets obtained via nlist and used in the get_ functions */

static unsigned long mpid_offset;
static unsigned long avenrun_offset;
static unsigned long total_offset;
static unsigned long cp_time_offset;
static unsigned long cp_mp_time_offset;

/* these are for calculating cpu state percentages */

static long cp_time[CPUSTATES];
static long cp_old[CPUSTATES];
static long cp_diff[CPUSTATES];
static long cp_mp_time[MAXNCPUS][CPUSTATES];
static long cp_mp_old[MAXNCPUS][CPUSTATES];
static long cp_mp_diff[MAXNCPUS][CPUSTATES];
int which_cpu_states = 9;

/* these are for detailing the process states */

int process_states[7];
char *procstatenames[] = {
    "", " sleeping, ", " ABANDONED, ", " running, ", " starting, ",
    " zombie, ", " stopped, ",
    NULL
};

/* these are for detailing the cpu states */

int cpu_states[5];
int cpu_mp_states[MAXNCPUS][5];
char *cpustatenames[] = {
    "user", "nice", "system", "idle",
    NULL
};

/* these are for detailing the memory statistics */

int memory_stats[4];
char *memorynames[] = {
    "K available, ", "K in use, ", "K free, ", "K locked", NULL
};

/* these are for keeping track of the proc array */

static int bytes;
static int pref_len;
static struct proc *pbase;
static struct proc **pref;

/* these are for getting the memory statistics */

static struct page *physpage;
static int bytesize;
static int count;
static int pageshift;		/* log base 2 of the pagesize */

/* define pagetok in terms of pageshift */

#define pagetok(size) ((size) << pageshift)

/* useful externals */
extern int errno;
extern char *sys_errlist[];

long lseek();
long time();

machine_init(statics)

struct statics *statics;

{
    register int i;
    register int pagesize;

    /* initialize the kernel interface */
    if ((kd = kvm_open(NULL, NULL, NULL, O_RDONLY, "top")) == NULL)
    {
	perror("kvm_open");
	return(-1);
    }

    /* get the list of symbols we want to access in the kernel */
    if ((i = kvm_nlist(kd, nlst)) < 0)
    {
	fprintf(stderr, "top: nlist failed\n");
	return(-1);
    }

    /* make sure they were all found */
    if (i > 0 && check_nlist(nlst) > 0)
    {
	return(-1);
    }

    /* get the symbol values out of kmem */
    (void) getkval(nlst[X_PROC].n_value,   (int *)(&proc),	sizeof(proc),
	    nlst[X_PROC].n_name);
    (void) getkval(nlst[X_NPROC].n_value,  &nproc,		sizeof(nproc),
	    nlst[X_NPROC].n_name);
    (void) getkval(nlst[X_CCPU].n_value,   (int *)(&ccpu),	sizeof(ccpu),
	    nlst[X_CCPU].n_name);
    (void) getkval(nlst[X_PAGES].n_value,  (int *)(&pages),	sizeof(pages),
	    nlst[X_PAGES].n_name);
    (void) getkval(nlst[X_EPAGES].n_value, (int *)(&epages),	sizeof(epages),
	    nlst[X_EPAGES].n_name);

    /* stash away certain offsets for later use */
    mpid_offset = nlst[X_MPID].n_value;
    avenrun_offset = nlst[X_AVENRUN].n_value;
    total_offset = nlst[X_TOTAL].n_value;
    cp_time_offset = nlst[X_CP_TIME].n_value;
    cp_mp_time_offset = nlst[X_CP_MP_TIME].n_value;
    (void) getkval(nlst[X_NCPUS].n_value, (int *)(&ncpus), sizeof(ncpus),
		   nlst[X_NCPUS].n_name);

    /* this is used in calculating WCPU -- calculate it ahead of time */
    logcpu = log(loaddouble(ccpu));

    /* allocate space for proc structure array and array of pointers */
    bytes = nproc * sizeof(struct proc);
    pbase = (struct proc *)malloc(bytes);
    pref  = (struct proc **)malloc(nproc * sizeof(struct proc *));

    /* Just in case ... */
    if (pbase == (struct proc *)NULL || pref == (struct proc **)NULL)
    {
	fprintf(stderr, "top: can't allocate sufficient memory\n");
	return(-1);
    }

    /* allocate a table to hold all the page structs */
    bytesize = epages - pages;
    count = bytesize / sizeof(struct page);
    physpage = (struct page *)malloc(epages - pages);
    if (physpage == NULL)
    {
	fprintf(stderr, "top: can't allocate sufficient memory\n");
	return(-1);
    }
   
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

    /* all done! */
    return(0);
}

char *format_header(uname_field)

register char *uname_field;

{
    register char *ptr;

    ptr = header + UNAME_START;
    while (*uname_field != '\0')
    {
	*ptr++ = *uname_field++;
    }

    return(header);
}

get_system_info(si)

struct system_info *si;

{
    load_avg avenrun[3];
    int		i;
    int	 	j;

    /* get the cp_time array */
    (void) getkval(cp_time_offset, (int *)cp_time, sizeof(cp_time),
		   "_cp_time");

    /* get the cp_mp_time arrays as well */
    (void) getkval(cp_mp_time_offset, (int *) cp_mp_time, sizeof(cp_mp_time),
		 "_cp_mp_time");

    /* get load average array */
    (void) getkval(avenrun_offset, (int *)avenrun, sizeof(avenrun),
		   "_avenrun");

    /* get mpid -- process id of last process */
    (void) getkval(mpid_offset, &(si->last_pid), sizeof(si->last_pid),
		   "_mpid");

    /* get the array of physpage descriptors */
    (void) getkval(pages, (int *)physpage, bytesize, "array _page");

    /* convert load averages to doubles */
    {
	register double *infoloadp;
	register load_avg *sysloadp;

	infoloadp = si->load_avg;
	sysloadp = avenrun;
	for (i = 0; i < 3; i++)
	{
	    *infoloadp++ = loaddouble(*sysloadp++);
	}
    }

    /* calculate percentages for each of the cpu's */
    for (i = 0; i < ncpus; i++)
	(void) percentages(CPUSTATES, cpu_mp_states[i], cp_mp_time[i],
			   cp_mp_old[i], cp_mp_diff[i]);

    /* if the selected cpu doesn't exist show summary of all cpu's */
    /* otherwise, show stats for selected cpu */
    if (which_cpu_states >= ncpus) {
	for (i = 0; i < CPUSTATES; i++) {
	    cpu_states[i] = 0;
	    for (j = 0; j < ncpus; j++)
		cpu_states[i] += cpu_mp_states[j][i];
	    cpu_states[i] /= ncpus;
	}
    } else {
	for (i = 0; i < CPUSTATES; i++) {
	    cpu_states[i] = cpu_mp_states[which_cpu_states][i];
	}
    }

    /* sum memory statistics */
    {
	register struct page *pp;
	register int cnt;
	register int inuse;
	register int free;
	register int locked;

	/* bop thru the array counting page types */
	pp = physpage;
	inuse = free = locked = 0;
	for (cnt = count; --cnt >= 0; pp++)
	{
	    if (pp->p_free)
	    	free++;
	    else if (pp->p_lock || pp->p_keepcnt > 0)
	    	locked++;
	    else
	    	inuse++;
	}

	/* convert memory stats to Kbytes */
	memory_stats[0] = pagetok(inuse + free);
	memory_stats[1] = pagetok(inuse);
	memory_stats[2] = pagetok(free);
	memory_stats[3] = pagetok(locked);
    }

    /* set arrays and strings */
    si->cpustates = cpu_states;
    si->memory = memory_stats;
}

static struct handle handle;

caddr_t get_process_info(si, sel, compare)

struct system_info *si;
struct process_select *sel;
int (*compare)();

{
    register int i;
    register int total_procs;
    register int active_procs;
    register struct proc **prefp;
    register struct proc *pp;

    /* these are copied out of sel for speed */
    int show_idle;
    int show_system;
    int show_uid;
    int show_command;

    /* read all the proc structures in one fell swoop */
    (void) getkval(proc, (int *)pbase, bytes, "proc array");

    /* get a pointer to the states summary array */
    si->procstates = process_states;

    /* set up flags which define what we are going to select */
    show_idle = sel->idle;
    show_system = sel->system;
    show_uid = sel->uid != -1;
    show_command = sel->command != NULL;

    /* count up process states and get pointers to interesting procs */
    total_procs = 0;
    active_procs = 0;
    bzero((char *)process_states, sizeof(process_states));
    prefp = pref;
    for (pp = pbase, i = 0; i < nproc; pp++, i++)
    {
	/*
	 *  Place pointers to each valid proc structure in pref[].
	 *  Process slots that are actually in use have a non-zero
	 *  status field.  Processes with SSYS set are system
	 *  processes---these get ignored unless show_sysprocs is set.
	 */
	if (pp->p_stat != 0 &&
	    (show_system || ((pp->p_flag & SSYS) == 0)))
	{
	    total_procs++;
	    process_states[pp->p_stat]++;
	    if ((pp->p_stat != SZOMB) &&
		(show_idle || (pp->p_pctcpu != 0) || (pp->p_stat == SRUN)) &&
		(!show_uid || pp->p_uid == (uid_t)sel->uid))
	    {
		*prefp++ = pp;
		active_procs++;
	    }
	}
    }

    /* if requested, sort the "interesting" processes */
    if (compare != NULL)
    {
	qsort((char *)pref, active_procs, sizeof(struct proc *), compare);
    }

    /* remember active and total counts */
    si->p_total = total_procs;
    si->p_active = pref_len = active_procs;

    /* pass back a handle */
    handle.next_proc = pref;
    handle.remaining = active_procs;
    return((caddr_t)&handle);
}

char fmt[MAX_COLS];		/* static area where result is built */

char *format_next_process(handle, get_userid)

caddr_t handle;
char *(*get_userid)();

{
    register struct proc *pp;
    register long cputime;
    register double pct;
    struct user u;
    struct handle *hp;

    /* find and remember the next proc structure */
    hp = (struct handle *)handle;
    pp = *(hp->next_proc++);
    hp->remaining--;
    
    /* get the process's user struct and set cputime */
    if (getu(pp, &u) == -1)
    {
	(void) strcpy(u.u_comm, "<swapped>");
	cputime = 0;
    }
    else
    {
	/* set u_comm for system processes */
	if (u.u_comm[0] == '\0')
	{
	    if (pp->p_pid == 0)
	    {
		(void) strcpy(u.u_comm, "Swapper");
	    }
	    else if (pp->p_pid == 2)
	    {
		(void) strcpy(u.u_comm, "Pager");
	    }
	}

	cputime = u.u_ru.ru_utime.tv_sec + u.u_ru.ru_stime.tv_sec;
    }

    /* calculate the base for cpu percentages */
    pct = pctdouble(pp->p_pctcpu);

    state_abbrev[SRUN][4] = (pp->p_lastcpu) + '0';

    /* format this entry */
    sprintf(fmt,
	    Proc_format,
	    pp->p_pid,
	    (*get_userid)(pp->p_uid),
	    pp->p_pri - PZERO,
	    pp->p_nice - NZERO,
	    format_k(pagetok(PROCSIZE(pp))),
	    format_k(pagetok(pp->p_rssize)),
	    state_abbrev[pp->p_stat],
	    format_time(cputime),
	    100.0 * weighted_cpu(pct, pp),
	    100.0 * pct,
	    printable(u.u_comm));

    /* return the result */
    return(fmt);
}

/*
 *  getu(p, u) - get the user structure for the process whose proc structure
 *	is pointed to by p.  The user structure is put in the buffer pointed
 *	to by u.  Return 0 if successful, -1 on failure (such as the process
 *	being swapped out).
 */

getu(p, u)

register struct proc *p;
struct user *u;

{
    register struct user *lu;

    lu = kvm_getu(kd, p);
    if (lu == NULL)
    {
	return(-1);
    }
    else
    {
	*u = *lu;
	return(0);
    }
}

/*
 * check_nlist(nlst) - checks the nlist to see if any symbols were not
 *		found.  For every symbol that was not found, a one-line
 *		message is printed to stderr.  The routine returns the
 *		number of symbols NOT found.
 */

int check_nlist(nlst)

register struct nlist *nlst;

{
    register int i;

    /* check to see if we got ALL the symbols we requested */
    /* this will write one line to stderr for every symbol not found */

    i = 0;
    while (nlst->n_name != NULL)
    {
	if (nlst->n_type == 0)
	{
	    /* this one wasn't found */
	    fprintf(stderr, "kernel: no symbol named `%s'\n", nlst->n_name);
	    i = 1;
	}
	nlst++;
    }

    return(i);
}


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

getkval(offset, ptr, size, refstr)

unsigned long offset;
int *ptr;
int size;
char *refstr;

{
    if (kvm_read(kd, offset, ptr, size) != size)
    {
	if (*refstr == '!')
	{
	    return(0);
	}
	else
	{
	    fprintf(stderr, "top: kvm_read for %s: %s\n",
		refstr, sys_errlist[errno]);
	    quit(23);
	    /*NOTREACHED*/
	}
    }
    return(1);
}
    
/* comparison routine for qsort */

/*
 *  proc_compare - comparison function for "qsort"
 *	Compares the resource consumption of two processes using five
 *  	distinct keys.  The keys (in descending order of importance) are:
 *  	percent cpu, cpu ticks, state, resident set size, total virtual
 *  	memory usage.  The process states are ordered as follows (from least
 *  	to most important):  WAIT, zombie, sleep, stop, start, run.  The
 *  	array declaration below maps a process state index into a number
 *  	that reflects this ordering.
 */

static unsigned char sorted_state[] =
{
    0,	/* not used		*/
    3,	/* sleep		*/
    1,	/* ABANDONED (WAIT)	*/
    6,	/* run			*/
    5,	/* start		*/
    2,	/* zombie		*/
    4	/* stop			*/
};
 
proc_compare(pp1, pp2)

struct proc **pp1;
struct proc **pp2;

{
    register struct proc *p1;
    register struct proc *p2;
    register int result;
    register pctcpu lresult;

    /* remove one level of indirection */
    p1 = *pp1;
    p2 = *pp2;

    /* compare percent cpu (pctcpu) */
    if ((lresult = p2->p_pctcpu - p1->p_pctcpu) == 0)
    {
	/* use cpticks to break the tie */
	if ((result = p2->p_cpticks - p1->p_cpticks) == 0)
	{
	    /* use process state to break the tie */
	    if ((result = sorted_state[p2->p_stat] -
			  sorted_state[p1->p_stat])  == 0)
	    {
		/* use priority to break the tie */
		if ((result = p2->p_pri - p1->p_pri) == 0)
		{
		    /* use resident set size (rssize) to break the tie */
		    if ((result = p2->p_rssize - p1->p_rssize) == 0)
		    {
			/* use total memory to break the tie */
			result = PROCSIZE(p2) - PROCSIZE(p1);
		    }
		}
	    }
	}
    }
    else
    {
	result = lresult < 0 ? -1 : 1;
    }

    return(result);
}

/*
 * proc_owner(pid) - returns the uid that owns process "pid", or -1 if
 *		the process does not exist.
 *		It is EXTREMLY IMPORTANT that this function work correctly.
 *		If top runs setuid root (as in SVR4), then this function
 *		is the only thing that stands in the way of a serious
 *		security problem.  It validates requests for the "kill"
 *		and "renice" commands.
 */

int proc_owner(pid)

int pid;

{
    register int cnt;
    register struct proc **prefp;
    register struct proc *pp;

    prefp = pref;
    cnt = pref_len;
    while (--cnt >= 0)
    {
	if ((pp = *prefp++)->p_pid == (pid_t)pid)
	{
	    return((int)pp->p_uid);
	}
    }
    return(-1);
}
