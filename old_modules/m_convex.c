/*
 * top - a top users display for Convex OS 11.X
 *
 * SYNOPSIS:  any C2XX running Convex OS 11.X.
 *
 * DESCRIPTION:
 * This is the machine-dependent module for Convex OS.11.X
 * Most of it was stolen from m_sunos4.c which was written by
 * William LeFebvre <wnl@groupsys.com>
 * Works for:
 *      Convex OS 11.1
 *
 * CFLAGS: -DHAVE_GETOPT
 * MODE: 2111
 * UID: root
 * GID: kmem
 * INSTALL: /usr/bin/install
 *
 * AUTHOR: William L. Jones jones@chpc.utexas.edu
 *         minor format changes Warren Vosper <warrenv@convex.com>
 */

#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <nlist.h>
#include <stdio.h>
#include <sys/dk.h>
#include <sys/vmmeter.h>

#include "top.h"
#include "machine.h"

/*
 * Defines.
 */
#define KMEM    "/dev/kmem"
#define VMUNIX  "/vmunix"
#define pagetok(size) ((size) << 2)

struct _oldproc {
	pid_t  p_pid;
	float  p_pctcpu;
};

/*
 * Globals.
 */
static int kmem;
static unsigned long    proc_addr;
static          int     nproc;
static unsigned long    avenrun_offset;
static unsigned long    total_offset;
static unsigned long    cp_time_offset;
static struct   proc    *proc;
static struct   _oldproc *oldproc;
static          long    cp_time[MAXCPUS][CPUSTATES];
static		long    cp_old[CPUSTATES];
static		long    cp_diff[CPUSTATES];
static 		double  avenrun[3];
static struct	proc    **pref;
static 		int     pref_len = 0;
static double           logcpu;
static int              ccpu;
static struct	vmtotal vmtotal;

/*
 * Defines
 */


/*
 * Extenrals.
 */
extern long percentages();

/*
 * nlist arrary.
 */
#define X_AVENRUN       0
#define X_NPROC         1
#define X_PROC          2
#define X_TOTAL         3
#define X_CP_TIME       4
#define X_CCPU		5

static struct nlist nlst[] = {
    { "_avenrun" },              /* 0 */
    { "_nproc" },                /* 1 */
    { "_proc" },                 /* 2 */
    { "_total"},		 /* 3 */
    { "_cp_time"},               /* 4 */
    { "_ccpu"},		         /* 5 */
    { "" },
};

/* declarations for load_avg */
#include "loadavg.h"

/* define what weighted cpu is.  */
#define weighted_cpu(pp) (*(float *)&pp->p_genid)

/* get_process_info passes back a handle.  This is what it looks like: */

struct handle
{
    struct proc **next_proc;    /* points to next valid proc pointer */
    int remaining;              /* number of pointers remaining */
};



/*
 *  These definitions control the format of the per-process area
 */

static char header[] =
  "  PID X           PRI  NICE  SIZE   RES STATE   TIME   WCPU    CPU COMMAND";
/* 0123456   -- field to fill in starts at header+6 */
#define UNAME_START 6

#define Proc_format \
	"%5d %-8.8s %7.2g %4d %5s %5s %-5s%4d:%02d %5.2f%% %5.2f%% %.14s"


int process_states[SSTOP+1];
char *procstatenames[] = {
    "", " running, ", " idle, ", " zombie, ", " sleeping, ",
    " stopped, ",
    NULL
};

char *state_abbrev[] =
{
    "init", "run", "idl", "zomb", "sleep", "stop"
};


/* these are for detailing the cpu states */

int cpu_states[5];
char *cpustatenames[] = {
    "user", "nice", "system", "idle", NULL
};

/* these are for detailing the memory statistics */

int memory_stats[4];
char *memorynames[] = {
    "M virt, ", "M real, ", "M free, ", NULL
};

/* useful externals */
extern int errno;
extern char *sys_errlist[];

long lseek();
long time();
long percentages();

machine_init(statics)

struct statics *statics;

{
    
    if ((kmem = open(KMEM, O_RDONLY)) == -1) {
        perror(KMEM);
        return(-1);
    }
   
    (void)nlist(VMUNIX, nlst);
   
    if (nlst[0].n_type == 0) {
	fprintf(stderr, "top: nlist failed\n");
	return -1;
    }
    /*
     * Get the sysmbol value of of kmem
     */
    (void) getkval(nlst[X_PROC].n_value, (int *)(&proc_addr), sizeof(proc_addr),
            nlst[X_PROC].n_un.n_name);
    (void) getkval(nlst[X_NPROC].n_value,&nproc,              sizeof(nproc),
            nlst[X_NPROC].n_un.n_name);
    (void) getkval(nlst[X_CCPU].n_value, (int *)(&ccpu),      sizeof(ccpu),
            nlst[X_CCPU].n_un.n_name);

    /* this is used in calculating WCPU -- calculate it ahead of time */
    logcpu = log(loaddouble(ccpu));

    /*
     * Allocate storage.
     */
    proc = (struct proc *)malloc(nproc * sizeof(struct proc));
    oldproc = (struct _oldproc *)malloc(nproc * sizeof(struct _oldproc));
    memset((char *)oldproc, 0, nproc*sizeof(struct _oldproc));
    pref = (struct proc **)malloc(nproc * sizeof(struct proc *));

    /*
     * stash away certain offsets for later us
     */
    avenrun_offset = nlst[X_AVENRUN].n_value;
    total_offset = nlst[X_TOTAL].n_value;
    cp_time_offset = nlst[X_CP_TIME].n_value;

    /* 
     * fill in the statics information 
     */
    statics->procstate_names = procstatenames;
    statics->cpustate_names = cpustatenames;
    statics->memory_names = memorynames;


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
    long cpu[CPUSTATES];
    int i,j;
    int total;

    /* get the cp_time array */
    (void) getkval(cp_time_offset, (int *)cp_time, sizeof(cp_time),
                   "_cp_time");

    /* get load average array */
    (void) getkval(avenrun_offset, (int *)avenrun, sizeof(avenrun),
                   "_avenrun");
    
    /* get memory stats */
    (void) getkval(total_offset,   (int *)&vmtotal,    sizeof(vmtotal),
                   "_total");

    for (i=0; i<3; i++) {
        si->load_avg[i] = avenrun[i];
    }

    for (i=0; i<CPUSTATES; i++) {
	cpu[i] = 0;
	for (j=0; j<MAXCPUS; j++) {
	   cpu[i] += cp_time[j][i];
	}
    }
    total = percentages(CPUSTATES, cpu_states, cpu, cp_old, cp_diff);
    si->cpustates = cpu_states;
    memory_stats[0] = pagetok(vmtotal.t_vm)/1024.0;
    memory_stats[1] = pagetok(vmtotal.t_rm)/1024.0;
    memory_stats[2] = pagetok(vmtotal.t_free)/1024.0;
    memory_stats[3] = -1;
    si->memory = memory_stats;
    si->procstates = process_states;
    si->p_total = 0;
    si->p_active = 0;
    si->last_pid = -1;
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

    static struct timeval lasttime = {0, 0};
    struct timeval thistime;
    struct timezone tzp;
    double timediff;
    double alpha, beta;

    gettimeofday(&thistime,&tzp);
    /*
     * To avoid divides, we keep times in nanoseconds.  This is
     * scaled by 1e7 rather than 1e9 so that when we divide we
     * get percent.
     */
    if (lasttime.tv_sec)
	timediff = ((double) thistime.tv_sec - lasttime.tv_sec);
    else
    	timediff = 1;
    /*
     * constants for exponential average.  avg = alpha * new + beta * avg
     * The goal is 50% decay in 30 sec.  However if the sample period
     * is greater than 30 sec, there's not a lot we can do.
     */
    if (timediff < 30) {
 	alpha = 0.5 * (timediff / 30.0);
	beta = 1.0 - alpha;
    } else {
	alpha = 0.5;
	beta = 0.5;
    }

    /* set up flags which define what we are going to select */
    show_idle = sel->idle;
    show_system = sel->system;
    show_uid = sel->uid != -1;
    show_command = sel->command != NULL;

    /* read all the proc structures in one fell swoop */

    (void) getkval(proc_addr, (int *)proc, nproc * sizeof(struct proc), 
		"proc array");

    /* count up process states and get pointers to interesting procs */

    total_procs = 0;
    active_procs = 0;
    bzero((char *)process_states, sizeof(process_states));
    prefp = pref;     
    for (pp = proc, i = 0; i < nproc; pp++, i++)
    {
	if (oldproc[i].p_pid == pp->p_pid) {
	    weighted_cpu(pp) = pctdouble(oldproc[i].p_pctcpu)*beta +
		               pctdouble(pp->p_pctcpu)*alpha;
	} else {
	    weighted_cpu(pp) = pctdouble(oldproc[i].p_pctcpu);
	}
	oldproc[i].p_pid = pp->p_pid;
	oldproc[i].p_pctcpu = pp->p_pctcpu;

        if (pp->p_stat != 0 &&
            (show_system || ((pp->p_flag & SSYS) == 0)))
        {
            total_procs++;
            process_states[pp->p_stat]++;
            if ((pp->p_stat != SZOMB) &&
                (show_idle || (pp->p_stat == SRUN)) &&
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


    lasttime = thistime;

    si->p_total = pref_len = total_procs;
    si->p_active = active_procs;

    /* pass back a handle */
    handle.next_proc = pref;
    handle.remaining = active_procs;
    return((caddr_t)&handle);
}

char fmt[128];		/* static area where result is built */


char *format_next_process(handle, get_userid)

caddr_t handle;
char *(*get_userid)();

{
    register struct proc *pp;
    register long cputime;
    register double pct;
    struct user u;
    struct handle *hp;
    long rrsize = 0;
    long size = 0;

    /* find and remember the next proc structure */
    hp = (struct handle *)handle;
    pp = *(hp->next_proc++);
    hp->remaining--;
    
    /* get the process's user struct and set cputime */
    if (getu(pp, &u) == -1)
    {
	(void) strcpy(u.u_comm, "<swapped>");
	cputime = 0;
	size = rrsize = 0;
    }
    else
    {
	/* set u_comm for system processes */
	if (u.u_comm[0] == '\0')
	{
	    if (pp->p_pid == 0)
	    {
		(void) strcpy(u.u_comm, "swappout");
	    }
	    else if (pp->p_pid == 1)
	    {
		(void) strcpy(u.u_comm, "init");
	    }
	    else if (pp->p_pid == 2)
	    {
		(void) strcpy(u.u_comm, "pageout");
	    }
	    else if (pp->p_pid == 3)
	    {
		(void) strcpy(u.u_comm, "swapin");
	    }
	    else if (pp->p_pid == 4)
	    {
		(void) strcpy(u.u_comm, "scheduler");
	    }
	    else if (pp->p_pid == 5)
	    {
		(void) strcpy(u.u_comm, "interrupt");
	    }
	}
	cputime = u.u_ru.ru_utime.tv_sec + u.u_ru.ru_stime.tv_sec;
	size = u.u_tsize + u.u_dsize + u.u_ssize;
        rrsize = u.u_ru.ru_maxrss;
    }

    /* calculate the base for cpu percentages */
    pct = pctdouble(pp->p_pctcpu);

    
    /* format this entry */
    sprintf(fmt,
	    Proc_format,
	    pp->p_pid,
	    (*get_userid)(pp->p_uid),
	    pp->p_pri,
	    pp->p_nice - NZERO,
	    format_k(pagetok(size)),
	    format_k(pagetok(rrsize)),
	    state_abbrev[pp->p_stat],
	    cputime / 60l,
	    cputime % 60l,
	    100.0 * weighted_cpu(pp),
	    100.0 * pct,
	    printable(u.u_comm));

    /* return the result */
    return(fmt);
}

getu(p, u)
register struct proc *p;
struct user *u;
{
    if (p->p_uaddr) {
	
        if (lseek(kmem, (long)p->p_uaddr, L_SET) != -1) {
            if (read(kmem, (char *)u, sizeof(struct user)) == 
		sizeof(struct user)) {
	        return 0;
	    }
	}
    }
    return -1;
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
    if (lseek(kmem, (long)offset, L_SET) == -1) {
        if (*refstr == '!')
            refstr++;
        (void) fprintf(stderr, "%s: lseek to %s: %s\n", KMEM,
                       refstr, strerror(errno));
        quit(23);
    }

    if (read(kmem, (char *)ptr, size) != size)
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
	}
    }
    return(1);
}
    
/* comparison routine for qsort */
/* NOTE: this is specific to the BSD proc structure, but it should
   give you a good place to start. */

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
		    result = 0;
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

int proc_owner(pid)

int pid;

{
    register int cnt;
    register struct proc **prefp;
    register struct proc *pp;

    prefp = pref;
    cnt =  pref_len;
    while (--cnt >= 0)
    {
	if ((pp = *prefp++)->p_pid == (pid_t)pid)
	{
	    return((int)pp->p_uid);
	}
    }
    return(-1);
}
