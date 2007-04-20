/*
 * top - a top users display for Unix
 *
 * SYNOPSIS:  Encore Multimax running any release of UMAX 4.3
 *
 * DESCRIPTION:
 * This module makes top work on the following systems:
 *	Encore Multimax running UMAX 4.3 release 4.0 and later
 *
 * AUTHOR:  William LeFebvre <wnl@groupsys.com>
 */

/*
 * The winner of the "wow what a hack" award:
 * We don't really need the proc structure out of sys/proc.h, but we do
 * need many of the #defines.  So, we define a bogus "queue" structure
 * so that we don't have to include that mess of stuff in machine/*.h
 * just so that the proc struct will get defined cleanly.
 */

struct queue { int x };

#include <stdio.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <machine/cpu.h>
#include <inq_stats/statistics.h>
#include <inq_stats/cpustats.h>
#include <inq_stats/procstats.h>
#include <inq_stats/vmstats.h>

#include "top.h"
#include "display.h"
#include "machine.h"
#include "utils.h"

struct handle
{
    struct proc **next_proc;	/* points to next valid proc pointer */
    int remaining;		/* number of pointers remaining */
};

/* Log base 2 of 1024 is 10 (2^10 == 1024) */
#define LOG1024		10

/* Convert clicks (kernel pages) to kbytes ... */
#if PGSHIFT>10
#define pagetok(size)	((size) << (PGSHIFT - LOG1024))
#else
#define pagetok(size)	((size) >> (LOG1024 - PGSHIFT))
#endif

/* what we consider to be process size: */
#define PROCSIZE(pp) ((pp)->pd_tsize + (pp)->pd_dsize + (pp)->pd_ssize)

/* the ps_nrun array index is incremented every 12th of a minute */
#define	MINUTES(x)	((x) * 12)

/* convert a tv structure (seconds, microseconds) to a double */
#define TVTODOUBLE(tv) ((double)(tv).tv_sec + ((double)(tv).tv_usec / 1000000))

/*
 *  These definitions control the format of the per-process area
 */

static char header[] =
  "  PID X        PRI NICE  SIZE   RES STATE    TIME    %CPU COMMAND";
/* 0123456   -- field to fill in starts at header+6 */
#define UNAME_START 6

#define Proc_format \
	"%5d %-8.8s %3d %4d %5s %5s %-5s %6s %6.2f%% %s"

/* process state names for the "STATE" column of the display */

char *state_abbrev[] =
{
    "", "", "wait", "run", "start", "stop", "exec", "event"
};

/* these are for detailing the process states */

int process_states[5];
char *procstatenames[] = {
    " waiting, ",
#define P_SLEEP  0
    " running, ",
#define P_RUN    1
    " zombie, ",
#define P_ZOMBIE 2
    " stopped, ",
#define P_STOP   3
    " free slots",
#define P_FREE   4
    NULL
};

/* these are for detailing the cpu states */

int cpu_states[4];
char *cpustatenames[] = {
    "user", "nice", "system", "idle", NULL
};

/* these are for detailing the memory statistics */

int memory_stats[4];
char *memorynames[] = {
    "K available, ", "K free, ", "K locked, ", "K virtual", NULL
};

/* these detail per-process information */

static int nprocs;
static int pref_len;
static struct proc_detail *pd;
static struct proc_detail **pref;

/* inq_stats structures and the STAT_DESCRs that use them */

static struct proc_config stat_pc;
static struct vm_config stat_vm;
static struct class_stats stat_class;
static struct proc_summary stat_ps;
static struct cpu_stats stat_cpu;

static struct stat_descr sd_procconfig = {
    NULL,		/* sd_next */
    SUBSYS_PROC,	/* sd_subsys */
    PROCTYPE_CONFIG,	/* sd_type */
    0,			/* sd_options */
    0,			/* sd_objid */
    &stat_pc,		/* sd_addr */
    sizeof(stat_pc),	/* sd_size */
    0,			/* sd_status */
    0,			/* sd_sizeused */
    0			/* sd_time */
};

static struct stat_descr sd_memory = {
    NULL,		/* sd_next */
    SUBSYS_VM,		/* sd_subsys */
    VMTYPE_SYSTEM,	/* sd_type */
    0,			/* sd_options */
    0,			/* sd_objid */
    &stat_vm,		/* sd_addr */
    sizeof(stat_vm),	/* sd_size */
    0,			/* sd_status */
    0,			/* sd_sizeused */
    0			/* sd_time */
};

static struct stat_descr sd_class = {
    NULL,		/* sd_next */
    SUBSYS_CPU,		/* sd_subsys */
    CPUTYPE_CLASS,	/* sd_type */
    0,			/* sd_options */
    UMAXCLASS,		/* sd_objid */
    &stat_class,	/* sd_addr */
    sizeof(stat_class),	/* sd_size */
    0,			/* sd_status */
    0,			/* sd_sizeused */
    0			/* sd_time */
};

static struct stat_descr sd_procsummary = {
    NULL,		/* sd_next */
    SUBSYS_PROC,	/* sd_subsys */
    PROCTYPE_SUMMARY,	/* sd_type */
    0,			/* sd_options */
    0,			/* sd_objid */
    &stat_ps,		/* sd_addr */
    sizeof(stat_ps),	/* sd_size */
    0,			/* sd_status */
    0,			/* sd_sizeused */
    0			/* sd_time */
};

static struct stat_descr sd_procdetail = {
    NULL,		/* sd_next */
    SUBSYS_PROC,	/* sd_subsys */
    PROCTYPE_DETAIL,	/* sd_type */
    PROC_DETAIL_ALL | PROC_DETAIL_ALLPROC,	/* sd_options */
    0,			/* sd_objid */
    NULL,		/* sd_addr */
    0,			/* sd_size */
    0,			/* sd_status */
    0,			/* sd_sizeused */
    0			/* sd_time */
};

static struct stat_descr sd_cpu = {
    NULL,		/* sd_next */
    SUBSYS_CPU,		/* sd_subsys */
    CPUTYPE_CPU,	/* sd_type */
    0,			/* sd_options */
    0,			/* sd_objid */
    &stat_cpu,		/* sd_addr */
    sizeof(stat_cpu),	/* sd_size */
    0,			/* sd_status */
    0,			/* sd_sizeused */
    0			/* sd_time */
};

/* precomputed values */
static int numcpus;

machine_init(statics)

struct statics *statics;

{
    if (inq_stats(2, &sd_procconfig, &sd_class) == -1)
    {
	perror("proc config");
	return(-1);
    }

    if (sd_procconfig.sd_status != 0)
    {
	fprintf(stderr, "stats status %d\n", sd_procconfig.sd_status);
    }
	
#ifdef DEBUG
    printf("pc_nprocs = %d\n", stat_pc.pc_nprocs);
    printf("class_numcpus = %d\n", stat_class.class_numcpus);
#endif

    /* things to remember */
    numcpus = stat_class.class_numcpus;

    /* space to allocate */
    nprocs = stat_pc.pc_nprocs;
    pd = (struct proc_detail *)malloc(nprocs * sizeof(struct proc_detail));
    pref = (struct proc_detail **)malloc(nprocs * sizeof(struct proc_detail *));
    if (pd == NULL || pref == NULL)
    {
	fprintf(stderr, "top: can't allocate sufficient memory\n");
	return(-1);
    }

    /* pointers to assign */
    sd_procdetail.sd_addr = pd;
    sd_procdetail.sd_size = nprocs * sizeof(struct proc_detail);

    /* fill in the statics stuff */
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
    /* get all status information at once */
    inq_stats(1, &sd_memory);


    /* fill in the memory statistics, converting to K */
    memory_stats[0] = pagetok(stat_vm.vm_availmem);
    memory_stats[1] = pagetok(stat_vm.vm_freemem);
    memory_stats[2] = pagetok(stat_vm.vm_physmem - stat_vm.vm_availmem);
    memory_stats[3] = 0;   /* ??? */

    /* set array pointers */
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
    register int index;
    register int total;
    int active_procs;
    char show_idle;
    char show_system;
    char show_uid;
    char show_command;

    if (inq_stats(3, &sd_procsummary, &sd_cpu, &sd_procdetail) == -1)
    {
	perror("proc summary");
	return(NULL);
    }

    if (sd_procsummary.sd_status != 0)
    {
	fprintf(stderr, "stats status %d\n", sd_procsummary.sd_status);
    }

#ifdef DEBUG
    printf("nfree = %d\n", stat_ps.ps_nfree);
    printf("nzombies = %d\n", stat_ps.ps_nzombies);
    printf("nnrunnable = %d\n", stat_ps.ps_nrunnable);
    printf("nwaiting = %d\n", stat_ps.ps_nwaiting);
    printf("nstopped = %d\n", stat_ps.ps_nstopped);
    printf("curtime0 = %d.%d\n", stat_cpu.cpu_curtime.tv_sec, stat_cpu.cpu_curtime.tv_usec);
    printf("starttime0 = %d.%d\n", stat_cpu.cpu_starttime.tv_sec, stat_cpu.cpu_starttime.tv_usec);
    printf("usertime0 = %d.%d\n", stat_cpu.cpu_usertime.tv_sec, stat_cpu.cpu_usertime.tv_usec);
    printf("systime0 = %d.%d\n", stat_cpu.cpu_systime.tv_sec, stat_cpu.cpu_systime.tv_usec);
    printf("idletime0 = %d.%d\n", stat_cpu.cpu_idletime.tv_sec, stat_cpu.cpu_idletime.tv_usec);
    printf("intrtime0 = %d.%d\n", stat_cpu.cpu_intrtime.tv_sec, stat_cpu.cpu_intrtime.tv_usec);
#endif

    /* fill in the process related counts */
    process_states[P_SLEEP]  = stat_ps.ps_nwaiting;
    process_states[P_RUN]    = stat_ps.ps_nrunnable;
    process_states[P_ZOMBIE] = stat_ps.ps_nzombies;
    process_states[P_STOP]   = stat_ps.ps_nstopped;
    process_states[P_FREE]   = stat_ps.ps_nfree;
    si->procstates = process_states;
    si->p_total = stat_ps.ps_nzombies +
                  stat_ps.ps_nrunnable +
                  stat_ps.ps_nwaiting +
                  stat_ps.ps_nstopped;
    si->p_active = 0;
    si->last_pid = -1;

    /* calculate load averages, the ENCORE way! */
    /* this code was inspiried by the program cpumeter */
    i = total = 0;
    index = stat_ps.ps_nrunidx;

    /* we go in three cumulative steps:  one for each avenrun measure */
    /* we are (once again) sacrificing code size for speed */
    while (i < MINUTES(1))
    {
	if (index < 0)
	{
	    index = PS_NRUNSIZE - 1;
	}
	total += stat_ps.ps_nrun[index--];
	i++;
    }
    si->load_avg[0] = (double)total / MINUTES(1);
    while (i < MINUTES(5))
    {
	if (index < 0)
	{
	    index = PS_NRUNSIZE - 1;
	}
	total += stat_ps.ps_nrun[index--];
	i++;
    }
    si->load_avg[1] = (double)total / MINUTES(5);
    while (i < MINUTES(15))
    {
	if (index < 0)
	{
	    index = PS_NRUNSIZE - 1;
	}
	total += stat_ps.ps_nrun[index--];
	i++;
    }
    si->load_avg[2] = (double)total / (double)MINUTES(15);

    /* grab flags out of process_select for speed */
    show_idle = sel->idle;
    show_system = sel->system;
    show_uid = sel->uid != -1;
    show_command = sel->command != NULL;

    /*
     *  Build a list of pointers to interesting proc_detail structures.
     *  inq_stats will return a proc_detail structure for every currently
     *  existing process.
     */
    {
	register struct proc_detail *pp;
	register struct proc_detail **prefp;
	register double virttime;
	register double now;

	/* pointer to destination array */
	prefp = pref;
	active_procs = 0;

	/* calculate "now" based on inq_stats retrieval time */
	now = TVTODOUBLE(sd_procdetail.sd_time);

	/*
	 * Note: we will calculate the number of processes from
	 * procdetail.sd_sizeused just in case there is an inconsistency
	 * between it and the procsummary information.
	 */
	total = sd_procdetail.sd_sizeused / sizeof(struct proc_detail);
	for (pp = pd, i = 0; i < total; pp++, i++)
	{
	    /*
	     *  Place pointers to each interesting structure in pref[]
	     *  and compute something akin to %cpu usage.  Computing %cpu
	     *  is really hard with the information that inq_stats gives
	     *  us, so we do the best we can based on the "virtual time"
	     *  and cpu time fields.  We also need a place to store this
	     *  computation so that we only have to do it once.  So we will
	     *  borrow one of the int fields in the proc_detail, and set a
	     *  #define accordingly.
	     *
	     *  We currently have no good way to determine if a process is
	     *  "idle", so we ignore the sel->idle flag.
	     */
#define pd_pctcpu pd_swrss

	    if ((show_system || ((pp->pd_flag & SSYS) == 0)) &&
		((pp->pd_flag & SZOMBIE) == 0) &&
		(!show_uid || pp->pd_uid == (uid_t)sel->uid) &&
		(!show_command || strcmp(sel->command, pp->pd_command) == 0))
	    {
		/* calculate %cpu as best we can */
		/* first, calculate total "virtual" cputime */
		pp->pd_virttime = virttime = TVTODOUBLE(pp->pd_utime) +
					     TVTODOUBLE(pp->pd_stime);

		/* %cpu is total cpu time over total wall time */
		/* we express this as a percentage * 10 */
		pp->pd_pctcpu = (int)(1000 * (virttime /
				      (now - TVTODOUBLE(pp->pd_starttime))));

		/* store pointer to this record and move on */
		*prefp++ = pp;
		active_procs++;
	    }
	}
    }

    /* if requested, sort the "interesting" processes */
    if (compare != NULL)
    {
	qsort((char *)pref, active_procs,
	      sizeof(struct proc_detail *),
	      compare);
    }

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
    register struct proc_detail *pp;
    register long cputime;
    struct handle *hp;

    /* find and remember the next proc structure */
    hp = (struct handle *)handle;
    pp = *(hp->next_proc++);
    hp->remaining--;
    

    /* set the cputime */
    cputime = pp->pd_utime.tv_sec + pp->pd_stime.tv_sec;

    /* calculate the base for cpu percentages */

#ifdef notyet
    /*
     *  If there is more than one cpu then add the processor number to
     *  the "run/" string.  Note that this will only show up if the
     *  process is in the run state.  Also note:  this will break for
     *  systems with more than 9 processors since the string will then
     *  be more than 5 characters.  I'm still thinking about that one.
     */
    if (numcpus > 1)
    {
???	state_abbrev[SRUN][4] = (pp->p_cpuid & 0xf) + '0';
    }
#endif

    /* format this entry */
    sprintf(fmt,
	    Proc_format,
	    pp->pd_pid,
	    (*get_userid)(pp->pd_uid),
	    pp->pd_pri,			/* PZERO ??? */
	    pp->pd_nice,		/* NZERO ??? */
	    format_k(pagetok(PROCSIZE(pp))),
	    format_k(pagetok(pp->pd_rssize)),
	    state_abbrev[pp->pd_state],
	    format_time((long)(pp->pd_virttime)),
	    (double)pp->pd_pctcpu / 10.,
	    printable(pp->pd_command));

    /* return the result */
    return(fmt);
}

/*
 *  proc_compare - comparison function for "qsort"
 *	Compares the resource consumption of two processes using five
 *  	distinct keys.  The keys (in descending order of importance) are:
 *  	percent cpu, cpu ticks, state, resident set size, total virtual
 *  	memory usage.  The process states are ordered according to the
 *	premutation array "sorted_state" with higher numbers being sorted
 *	before lower numbers.
 */

static unsigned char sorted_state[] =
{
    0,	/* not used		*/
    0,	/* not used		*/
    1,	/* wait			*/
    6,	/* run			*/
    3,	/* start		*/
    4,	/* stop 		*/
    5,	/* exec			*/
    2	/* event		*/
};
 
proc_compare(pp1, pp2)

struct proc **pp1;
struct proc **pp2;

{
    register struct proc_detail *p1;
    register struct proc_detail *p2;
    register int result;

    /* remove one level of indirection */
    p1 = *pp1;
    p2 = *pp2;

    /* compare percent cpu (pctcpu) */
    if ((result = p2->pd_pctcpu - p1->pd_pctcpu) == 0)
    {
	/* use process state to break the tie */
	if ((result = sorted_state[p2->pd_state] -
		      sorted_state[p1->pd_state])  == 0)
	{
	    /* use priority to break the tie */
	    if ((result = p2->pd_pri - p1->pd_pri) == 0)
	    {
		/* use resident set size (rssize) to break the tie */
		if ((result = p2->pd_rssize - p1->pd_rssize) == 0)
		{
		    /* use total memory to break the tie */
		    result = PROCSIZE(p2) - PROCSIZE(p1);
		}
	    }
	}
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
    register struct proc_detail **prefp;
    register struct proc_detail *pp;

    prefp = pref;
    cnt = pref_len;
    while (--cnt >= 0)
    {
	if ((pp = *prefp++)->pd_pid == pid)
	{
	    return(pp->pd_uid);
	}
    }
    return(-1);
}
