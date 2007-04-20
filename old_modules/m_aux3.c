/*
 * top - a top users display for Unix
 *
 * SYNOPSIS:  a Mac running A/UX version 3.x
 *
 * DESCRIPTION:
 * This is the machine-dependent module for A/UX 3.x.
 * ==
 * Although AUX does not generally have a renice systemcall, it can be
 * implemented by tweeking kernel memory.  While such a simple hack should
 * not be difficult to get right, USE THIS FEATURE AT YOUR OWN RISK!
 * To turn on setpriority emulation, add "-DIMPLEMENT_SETPRIORITY" to
 * the CFLAGS when prompted in the configure script.
 *
 * CFLAGS: -Dclear=clear_scr -DPRIO_PROCESS=0
 *
 * LIBS:
 *
 * AUTHOR:  Richard Henderson <rth@tamu.edu>
 */


#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <a.out.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysinfo.h>
#include <sys/var.h>
#include <sys/swap.h>

#define FSCALE	65536.0

#include "top.h"
#include "machine.h"
#include "loadavg.h"

/*=NLIST INFO===========================================================*/

#define X_V		0
#define X_SYSINFO	1
#define X_AVENRUN	2
#define X_MAXMEM	3
#define X_FREEMEM	4
#define X_SWAPTAB	5
#define X_AVAILRMEM	6
#define X_AVAILSMEM	7

static struct nlist nlst[] = {
    {"v"},
    {"sysinfo"},
    {"avenrun"},
    {"maxmem"},
    {"freemem"},
    {"swaptab"},
    {0},		/* "availrmem" */
    {0},		/* "availsmem" */
    {0}
};

static int kmem;
static int mem;

static struct var v;

#define V_OFS		(nlst[X_V].n_value)
#define SYSINFO_OFS	(nlst[X_SYSINFO].n_value)
#define AVENRUN_OFS	(nlst[X_AVENRUN].n_value)
#define MAXMEM_OFS	(nlst[X_MAXMEM].n_value)
#define FREEMEM_OFS	(nlst[X_FREEMEM].n_value)
#define SWAPTAB_OFS	(nlst[X_SWAPTAB].n_value)
#define AVAILRMEM_OFS	(nlst[X_AVAILRMEM].n_value)
#define AVAILSMEM_OFS	(nlst[X_AVAILSMEM].n_value)

/*=SYSTEM STATE INFO====================================================*/

/* these are for calculating cpu state percentages */

static long cp_time[NCPUSTATES];
static long cp_old[NCPUSTATES];
static long cp_diff[NCPUSTATES];

/* these are for keeping track of the proc array */

struct top_proc
{
    pid_t p_pid;
    pid_t p_pgrp;
    uid_t p_uid;
    int p_pri;
    int p_nice;
    int p_size;
    int p_stat;
    int p_flag;
    int p_slot;
    time_t p_start;
    time_t p_time;
    float p_pcpu;
    float p_wcpu;
    char p_name[COMMSIZ];
};

static int hash_size;
static struct top_proc *ptable;	/* the hash table of processes */
static struct top_proc *eptable;
static struct top_proc **pactive; /* list of active structures */
static struct top_proc **nextactive; /* for iterating through the processes */
static struct proc *preal;
static struct proc *epreal;

static pid_t last_pid;
static struct timeval last_update;

/* these are for passing data back to the mach. ind. portion */

static int cpu_states[NCPUSTATES];
static int process_states[8];
static int memory_stats[6];

/* a few useful macros... */

#define blocktok(b)	((b) >> 1)
#define pagetok(pg)	((pg) << (v.v_pageshift - LOG1024))
#define HASH(x)		((x) * 1686629713UL % hash_size)

/*=STATE IDENT STRINGS==================================================*/

static char *state_abbrev[] =
{
    "", "sleep", "run", "zomb", "stop", "start", "cpu", "swap",
    NULL
};

static char *procstatenames[] =
{
    "", " sleeping, ", " running, ", " zombie, ", " stopped, ",
    " starting, ", " on cpu, ", " swapping, ",
    NULL
};

static char *cpustatenames[] =
{
    "idle", "user", "kernel", "wait", "nice",
    NULL
};

static char *memorynames[] = 
{
    "K used, ", "K free, ", "K locked   Swap: ", 
    "K used, ", "K free",
    NULL
};

static char fmt_header[] = 
  "  PID  PGRP X        PRI NICE  SIZE STATE   TIME    WCPU     CPU COMMAND";


/*======================================================================*/

int
machine_init(statics)
    struct statics *statics;
{
    /* access kernel memory */
    if (
#ifdef IMPLEMENT_SETPRIORITY
        (kmem = open("/dev/kmem", O_RDWR)) < 0 &&
#endif
	(kmem = open("/dev/kmem", O_RDONLY)) < 0)
    {
	perror("/dev/kmem");
	return -1;
    }
    if ((mem = open("/dev/mem", O_RDONLY)) < 0)
    {
	perror("/dev/mem");
	return -1;
    }

    /* get the list of symbols we want to access in the kernel */
    nlst[X_AVAILRMEM].n_nptr = "availrmem";
    nlst[X_AVAILSMEM].n_nptr = "availsmem";

    if (nlist("/unix", nlst) < 0)
    {
	fprintf(stderr, "top: nlist failed\n");
	return -1;
    }

    /* make sure they were all found */
    if (check_nlist(nlst) > 0)
	return -1;

    /* grab the kernel configuration information */
    (void)getkval(V_OFS, (char *)&v, sizeof(v), "v");

    /* allocate space for process related info */
    hash_size = v.v_proc * 3 / 2;
    ptable = (struct top_proc *)malloc(hash_size * sizeof(struct top_proc));
    pactive = (struct top_proc **)malloc(v.v_proc * sizeof(struct top_proc *));

    if (!ptable || !pactive)
    {
	fprintf(stderr, "top: can't allocate sufficient memory\n");
	return -1;
    }

    eptable = ptable + hash_size;

    {
	struct top_proc *p;
	for (p = ptable; p != eptable; ++p)
	    p->p_pid = -1;
    }

    /* fill in the statics information */
    statics->procstate_names = procstatenames;
    statics->cpustate_names = cpustatenames;
    statics->memory_names = memorynames;

    /* all done! */
    return 0;
}

static struct top_proc *
lookup_proc(id)
    pid_t id;
{
    struct top_proc *p;

    p = ptable+HASH(rp->p_pid);
    while (p->p_pid != rp->p_pid && p->p_pid != -1)
    {
	if (++p == eptable)
	    p = ptable;
    }

    return p;
}
    

static void
update_proc_table()
{
    struct proc *rp;
    struct top_proc *p;
    float timediff, alpha, beta;

    getkval((long)v.ve_proctab, (char *)preal, 
	    sizeof(struct proc)*v.v_proc, "proc array");

    /* calculate the time difference since our last proc read */
    {
	struct timeval thistime;
	gettimeofday(&thistime, 0);
	if (last_update.tv_sec)
	    timediff = ((thistime.tv_sec - last_update.tv_sec) +
		        (thistime.tv_usec - last_update.tv_usec) * 1e-6);
	else
	    timediff = 1e9;
	last_update = thistime;
    }

    /* calculate constants for the exponental average */
    if (timediff < 30.0)
    {
	alpha = 0.5 * (timediff / 30.0);
	beta = 1.0 - alpha;
    }
    else
	alpha = beta = 0.5;

    timediff *= v.v_hz;

    /* mark the hash table entries as not seen */
    for (p = ptable; p != eptable; ++p)
	p->p_stat = 0;

    for (rp = preal; rp != epreal; ++rp)
    {
	struct user u;

	if (rp->p_stat == 0)
	    continue;
	else if (rp->p_stat == SZOMB ||
		 lseek(mem, rp->p_addr, 0) < 0 ||
	         read(mem, &u, sizeof(u)) != sizeof(u))
	{
	    strcpy(u.u_comm, "???");
	    u.u_utime = u.u_stime = u.u_start = 0;
	}

	p = lookup_proc(rp->p_pid);

	p->p_pgrp = rp->p_pgrp;
	p->p_uid = rp->p_uid;
	p->p_pri = rp->p_pri - PZERO;
	p->p_nice = rp->p_nice - NZERO;
	p->p_size = pagetok(rp->p_size);
	p->p_stat = rp->p_stat;
	p->p_flag = rp->p_flag;
	if (p->p_pid != rp->p_pid)
	{
	    /* new process */
	    p->p_pid = rp->p_pid;
	    p->p_slot = rp - preal;
	    p->p_start = u.u_start;
	    p->p_time = u.u_utime + u.u_stime;
	    p->p_pcpu = p->p_time / timediff;
	    p->p_wcpu = p->p_pcpu;
	    strncpy(p->p_name, u.u_comm, sizeof(u.u_comm));
	}
	else
	{
	    time_t oldtime = p->p_time;
	    p->p_time = u.u_utime + u.u_stime;
	    p->p_pcpu = (p->p_time - oldtime) / timediff;
	    p->p_wcpu = alpha * p->p_pcpu + beta * p->p_wcpu;
	}
    }
	
    for (p = ptable; p != eptable; ++p)
	if (p->p_stat == 0)
	    p->p_pid = -1;
}

void
get_system_info(info)
    struct system_info *info;
{
    /* convert load averages */
    {
	load_avg ar[3];

	(void)getkval(AVENRUN_OFS, (char *)&ar, sizeof(ar), "avenrun");

	/* convert load averages to doubles */
	info->load_avg[0] = loaddouble(ar[0]);
	info->load_avg[1] = loaddouble(ar[1]);
	info->load_avg[2] = loaddouble(ar[2]);
    }

    /* get cpu time counts */
    {
	struct sysinfo si;

	(void)getkval(SYSINFO_OFS, (char *)&si, sizeof(si), "sysinfo");

	memcpy(cp_time, si.cpu, sizeof(cp_time));
	percentages(NCPUSTATES, cpu_states, cp_time, cp_old, cp_diff);
    }

    /* get memory usage information */
    {
	int freemem, availrmem, availsmem, maxmem;
	struct swaptab swaptab[MSFILES];
	int i, swaptot, swapfree;

        (void)getkval(MAXMEM_OFS, (char *)&maxmem, sizeof(maxmem), "maxmem");
	(void)getkval(FREEMEM_OFS, (char *)&freemem, sizeof(freemem),
		      "freemem");
	(void)getkval(AVAILRMEM_OFS, (char *)&availrmem, sizeof(availrmem),
		      "availrmem");
	(void)getkval(AVAILSMEM_OFS, (char *)&availsmem, sizeof(availsmem),
		      "availsmem");
        (void)getkval(SWAPTAB_OFS, (char *)&swaptab, sizeof(swaptab),
		      "swaptab");

	for (i = swaptot = swapfree = 0; i < MSFILES; ++i)
	    if (swaptab[i].st_dev)
	    {
		swaptot += swaptab[i].st_npgs;
		swapfree += swaptab[i].st_nfpgs;
	    }

	memory_stats[0] = pagetok(availrmem - freemem);
	memory_stats[1] = pagetok(freemem);
	memory_stats[2] = pagetok(maxmem - availrmem);
	memory_stats[3] = pagetok(swaptot - swapfree);
	memory_stats[4] = pagetok(swapfree);
    }

    update_proc_table();

    /* search proc structures for newest process id */
    {
	struct top_proc *p;
	time_t t = 0;
	pid_t id = 0;

	for (p = ptable; p != eptable; ++p)
	{
	    if (!p->p_stat)
		continue;
	    if (p->p_start > t || p->p_start == t && p->p_pid > id)
	    {
		t = p->p_start;
		id = p->p_pid;
	    }
	}

	if (id > last_pid || id < last_pid - 10000)
	    last_pid = id;

	info->last_pid = last_pid;
    }

    /* set arrays and strings */
    info->cpustates = cpu_states;
    info->memory = memory_stats;
}

caddr_t
get_process_info(si, sel, compare)
     struct system_info *si;
     struct process_select *sel;
     int (*compare)();
{
    int total_procs;
    struct top_proc *p, **a;

    /* these are copied out of sel for speed */
    int show_idle, show_system, show_uid, show_command;

    /* get a pointer to the states summary array */
    si->procstates = process_states;

    /* set up flags which define what we are going to select */
    show_idle = sel->idle;
    show_system = sel->system;
    show_uid = sel->uid != -1;
    show_command = sel->command != NULL;

    /* count up process states and get pointers to interesting procs */
    total_procs = 0;
    memset(process_states, 0, sizeof(process_states));

    for (p = ptable, a = pactive; p != eptable; ++p)
    {
	int stat = p->p_stat, flag = p->p_flag;

	if (stat == 0 || (flag & SSYS) && !show_system)
	    continue;

	total_procs++;
	process_states[stat]++;

	if (stat != SZOMB &&
	    (show_idle || stat == SRUN || stat == SIDL || stat == SONPROC ||
	     ((stat == SSLEEP || stat == SSTOP) &&
	      (flag & (SINTR | SSYS)) == 0)) &&
	     (!show_uid || p->p_uid == (uid_t)sel->uid))
	{
	    /* add it to our active list */
	    *a++ = p;
	}
    }

    /* remember active and total counts */
    si->p_total = total_procs;
    si->p_active = a - pactive;

    /* if requested, sort the "interesting" processes */
    if (compare != NULL)
	qsort(pactive, si->p_active, sizeof(struct top_proc *), compare);

    /* set up to iterate though processes */
    nextactive = pactive;

    /* don't even pretend the return value isn't bogus */
    return 0;
}


char *
format_header(uname_field)
    char *uname_field;
{
    int len = strlen(uname_field);
    if (len > 8)
	len = 8;

    memcpy(strchr(fmt_header, 'X'), uname_field, len);

    return fmt_header;
}

char *
format_next_process(handle, get_userid)
     caddr_t handle;
     char *(*get_userid)();
{
    static char fmt[128];	/* static area where result is built */
    struct top_proc *pp = *nextactive++;

    sprintf(fmt,
	    "%5d %5d %-8.8s %3d %4d %5s %-5s %6s %6.2f%% %6.2f%% %.14s",
	    pp->p_pid,
	    pp->p_pgrp,
	    (*get_userid)(pp->p_uid),
	    pp->p_pri,
	    pp->p_nice,
	    format_k(pp->p_size),
	    state_abbrev[pp->p_stat],
	    format_time((time_t)pp->p_time / v.v_hz),
	    pp->p_wcpu * 100.0,
	    pp->p_pcpu * 100.0,
	    pp->p_name);

    /* return the result */
    return (fmt);
}


/*
 * check_nlist(nlst) - checks the nlist to see if any symbols were not
 *              found.  For every symbol that was not found, a one-line
 *              message is printed to stderr.  The routine returns the
 *              number of symbols NOT found.
 */

int
check_nlist(nlst)
     register struct nlist *nlst;
{
    register int i;

    /* check to see if we got ALL the symbols we requested */
    /* this will write one line to stderr for every symbol not found */

    i = 0;
    while (nlst->n_name[0])
    {
	if (nlst->n_value == 0)
	{
	    /* this one wasn't found */
	    fprintf(stderr, "kernel: no symbol named `%s'\n", nlst->n_name);
	    i = 1;
	}
	nlst++;
    }

    return (i);
}


/*
 *  getkval(offset, ptr, size, refstr) - get a value out of the kernel.
 *      "offset" is the byte offset into the kernel for the desired value,
 *      "ptr" points to a buffer into which the value is retrieved,
 *      "size" is the size of the buffer (and the object to retrieve),
 *      "refstr" is a reference string used when printing error meessages,
 *          if "refstr" starts with a '!', then a failure on read will not
 *          be fatal (this may seem like a silly way to do things, but I
 *          really didn't want the overhead of another argument).
 *      
 */

getkval(offset, ptr, size, refstr)
     unsigned long offset;
     int *ptr;
     int size;
     char *refstr;
{
    extern int errno;
    extern char *sys_errlist[];

    if (lseek(kmem, offset, 0) < 0 || read(kmem, ptr, size) != size)
    {
	if (*refstr == '!')
	{
	    return (0);
	}
	else
	{
	    fprintf(stderr, "top: getkval for %s: %s\n",
		    refstr, sys_errlist[errno]);
	    quit(23);
	    /*NOTREACHED */
	}
    }
    return (1);
}

/* comparison routine for qsort */

/*
 *  proc_compare - comparison function for "qsort"
 *      Compares the resource consumption of two processes using five
 *      distinct keys.  The keys (in descending order of importance) are:
 *      percent cpu, cpu ticks, state, resident set size, total virtual
 *      memory usage.  The process states are ordered as follows (from least
 *      to most important):  WAIT, zombie, sleep, stop, start, run.  The
 *      array declaration below maps a process state index into a number
 *      that reflects this ordering.
 */

static unsigned char sorted_state[] =
{
    0,	/* not used             */
    3,	/* sleep                */
    6,	/* runable              */
    1,	/* zombie               */
    4,	/* stop                 */
    5,	/* start                */
    7,	/* running              */
    2,	/* swapping             */
};

proc_compare(pp1, pp2)
    struct top_proc **pp1, **pp2;
{
    struct top_proc *p1, *p2;
    int result;
    double dresult;

    /* remove one level of indirection */
    p1 = *pp1;
    p2 = *pp2;

    /* compare percent cpu */
    dresult = p2->p_pcpu - p1->p_pcpu;
    if (dresult != 0.0)
	return dresult > 0.0 ? 1 : -1;

    /* use process state to break the tie */
    if ((result = (sorted_state[p2->p_stat] -
		   sorted_state[p1->p_stat])) == 0)
    {
	/* use priority to break the tie */
	if ((result = p2->p_pri - p1->p_pri) == 0)
	{
	    /* use total memory to break the tie */
	    result = p2->p_size - p1->p_size;
	}
    }

    return result;
}

/*
 * proc_owner(pid) - returns the uid that owns process "pid", or -1 if
 *              the process does not exist.
 *              It is EXTREMLY IMPORTANT that this function work correctly.
 *              If top runs setuid root (as in SVR4), then this function
 *              is the only thing that stands in the way of a serious
 *              security problem.  It validates requests for the "kill"
 *              and "renice" commands.
 */

int
proc_owner(pid)
    int pid;
{
    struct top_proc *p;

    for (p = ptable; p != eptable; ++p)
	if (p->p_pid == pid)
	    return p->p_uid;

    return -1;
}

/* 
 * setpriority(int which, pid_t pid, int val)
 * This system does not have this system call -- fake it
 */

int
setpriority(which, pid, val)
    int which, pid, val;
{
#ifndef IMPLEMENT_SETPRIORITY
    errno = ENOSYS;
    return -1;
#else
    struct top_proc *p;
    struct proc proc;
    int uid;

    /* sanity check arguments */
    val += NZERO;
    if (val < 0)
	val = 0;
    else if (val > 39)
	val = 39;

    p = lookup_proc(pid);
    if (p->p_pid == -1)
    {
	errno = ESRCH;
	return -1;
    }

    getkval((long)v.ve_proctab+p->p_slot*sizeof(proc),
	    (char *)&proc, sizeof(proc), "proc array");

    if (proc.p_stat == 0 || proc.p_pid != pid)
    {
	errno = ESRCH;
	return -1;
    }

    /* make sure we don't allow nasty people to do nasty things */
    uid = getuid();
    if (uid != 0)
    {
	if (uid != proc.p_uid || val < proc.p_nice)
	{
	    errno = EACCES;
	    return -1;
	}
    }

    /* renice */
    proc.p_nice = val;
    if (lseek(kmem, (v.ve_proctab + p->p_slot*sizeof(proc) +
		     offsetof(struct proc, p_nice), 0) < 0 ||
	write(kmem, &rp->p_nice, sizeof(rp->p_nice)) != sizeof(rp->p_nice))
    {
	return -1;
    }

    return 0;
#endif
}
