/*
 * top - a top users display for Unix
 * NEXTSTEP v.0.3  2/14/1996 tpugh
 *
 * SYNOPSIS:  any m68k or intel NEXTSTEP v3.x system
 *
 * DESCRIPTION:
 *	This is the machine-dependent module for NEXTSTEP v3.x
 *	Reported to work for NEXTSTEP v3.0, v3.2, and v3.3 Mach OS:
 *		NEXTSTEP v3.0 on Motorola machines.
 *		NEXTSTEP v3.2 on Intel and Motorola machines.
 *		NEXTSTEP v3.3 on Intel and Motorola machines.
 *	Problem with command column for (Choose next40 for fix):
 *		NEXTSTEP v3.2 on HP machines.
 *		NEXTSTEP v3.3 on HP and Sparc machines.
 *	Has not been tested for NEXTSTEP v2.x machines, although it should work.
 *	Has not been tested for NEXTSTEP v3.1 machines, although it should work.
 *	Install "top" with the permissions 4755.
 *		hostname# chmod 4755 top
 *		hostname# ls -lg top
 *		-rwsr-xr-x  1 root     kmem      121408 Sep  1 10:14 top*
 *	With the kmem group sticky bit set, we can read kernal memory without problems,
 *	but to communicate with the Mach kernal for task and thread info, it requires
 *	root privileges. Therefore, "top" must be setuid 4755 with the owner as root.
 *
 * LIBS: 
 *
 * Need the compiler flag, "-DSHOW_UTT", to see the user task and thread task
 * data structures to report process info.
 *
 * CFLAGS: -DSHOW_UTT
 *
 *
 * AUTHORS:		Tim Pugh <tpugh@oce.orst.edu>
 */

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/param.h>

#include <stdio.h>
#include <nlist.h>
#include <math.h>
#include <sys/dir.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/dk.h>
#include <sys/vm.h>
#include <sys/file.h>
#include <sys/time.h>
#import <mach/mach.h>
#include <sys/vmmeter.h>
#import <mach/vm_statistics.h>

#import "machine/m_next_task.h"

/*  Problems on NS/HPPA machines.  Also, not currently used by source code.
 *#define DOSWAP
 */

#include "top.h"
#include "machine.h"
#include "utils.h"

extern int errno, sys_nerr;
extern char *sys_errlist[];
#define strerror(e) (((e) >= 0 && (e) < sys_nerr) ? sys_errlist[(e)] : "Unknown error")

#define VMUNIX  "/mach"
#define KMEM    "/dev/kmem"
#define MEM     "/dev/mem"
#ifdef DOSWAP
#define SWAP	"/dev/drum"
#endif

/* NeXT BSD process structure does not contain locations to hold info such as
 * cpu usage, memory usage, resident core memory, or cpu time data.  So I've made
 * a new process structure which composites the NeXT structure and the missing
 * system info.
 */
struct proc_unix {
	struct proc *p_self;		/* Each p_self points to a element in pbase. */
	int p_pctcpu;				/* Scaled percent cpu usage. */
	int p_vsize;				/* Total VM memory usage. */
	int p_rsize;				/* Resident core memory usage. */
	int p_cptime;				/* scaled CPU Time */
	int run_state;				/* Task run state. */
	int flags;					/* Task state flags. */
	int nthreads;				/* Number of threads per Task. */
	int cur_priority;			/* Current main thread priority */
};

/* Contains the list of processes. */
struct handle
{
    struct proc_unix *list;		/* points to list of valid proc pointer */
    int count;					/* number of pointers */
	int current;				/* Index of the current process formatting */
};

/* declarations for load_avg */
#include "loadavg.h"
#define LSCALE	1000	/* scaling for "fixed point" arithmetic - <sys/kernel.h> */

/* define what weighted cpu is. */
/*
 *#define weighted_cpu(pct, pp) ((pp)->p_time == 0 ? 0.0 : \
 *			 ((pct) / (1.0 - exp((pp)->p_time * logcpu))))
 */

/*  The following three variables are not defined in NeXT's process structure.
 *	So they are zeroed until other ways of obtaining the info are found.
 */
/* what we consider to be process size: */
/* #define PROCSIZE(pp)	((pp)->p_tsize + (pp)->p_dsize + (pp)->p_ssize) */
#define PROCSIZE(pp)	(0)

/* #define P_RSSIZE(pp)	((pp)->p_rssize) */
#define	P_RSSIZE(pp)	(0)

/* #define P_CPTICKS(pp)	((pp)->p_cpticks) */
#define P_CPTICKS(pp)	(0)


extern int thread_stats(int p_pid, struct thread_basic_info *info, int *count);
extern int mach_load_avg(void);
extern kern_return_t task_stats(int p_pid, struct task_basic_info *info);

/* definitions for indices in the nlist array */
#define X_AVENRUN	0
#define X_CCPU		1
#define X_NPROC		2
#define X_PROC		3
#define X_TOTAL		4
#define X_CP_TIME	5
#define X_MPID		6
#define X_HZ		7

static struct nlist nlst[] = {
    { "_avenrun" },		/* 0 */
    { "_cpu_clk" },		/* 1 */
    { "_max_proc" },	/* 2 */
    { "_allproc" },		/* 3 */
    { "_total" },		/* 4 */
    { "_cp_time" },		/* 5 */
    { "_mpid" },		/* 6 */
    { "_hz" },			/* 7 */
    { 0 }
};

/*
 *  These definitions control the format of the per-process area
 */

static char header[] =
  "  PID X        STATE PRI NICE  THR VSIZE RSIZE   %MEM   %CPU   TIME COMMAND";
/* static char header[] =
 * "  PID X        STATE PRI NICE  THR VSIZE RSIZE   %MEM  %WCPU   %CPU   TIME COMMAND"; 
 */

/* 0123456   -- field to fill in starts at header+6 */
#define UNAME_START 6

#define Proc_format \
	"%5d %-8.8s %-5s %3d %4d %4d %5s %5s %6.2f %6.2f %6s %.14s"
/* #define Proc_format \
 *	"%5d %-8.8s %-5s %3d %4d %4d %5s %5s %6.2f %6.2f %6.2f %6s %.14s"
 */


/* process state names for the "STATE" column of the display */
/* the extra nulls in the string "run" are for adding a slash and
   the processor number when needed */
char *state_abbrev[] =
{
    "", "sleep", "WAIT", "run\0\0\0", "start", "zomb", "stop"
};
char *mach_state[] =
{
    "", "R", "T", "S", "U", "H"
};
char *flags_state[] =
{
    "", "W", "I"
};

/* these are for detailing the process states */
int process_states[7];
/* char *procstatenames[] = {
 *    "", " sleeping, ", " ABANDONED, ", " running, ", " starting, ",
 *    " zombie, ", " stopped, ",
 *    NULL
 *};
 */
char *procstatenames[] = {
    "", " running, ", " stopped, ", " sleeping, ", " uninterruptable, ",
    " halted, ", " zombie ", NULL
};


static int kmem, mem;
#ifdef DOSWAP
static int swap;
#endif

/* values that we stash away in _init and use in later routines */

/* static double logcpu; */

/* these are retrieved from the kernel in _init */

static unsigned long proc;
static          int  nproc;
static          long hz;
static load_avg  ccpu;
static          int  ncpu = 0;

/* these are offsets obtained via nlist and used in the get_ functions */

static unsigned long avenrun_offset;
static unsigned long mpid_offset;
static unsigned long total_offset;
static unsigned long cp_time_offset;

/* these are for calculating cpu state percentages */

static long cp_time[CPUSTATES];
static long cp_old[CPUSTATES];
static long cp_diff[CPUSTATES];

/* these are for detailing the cpu states */

int cpu_states[4];
char *cpustatenames[] = {
    "user", "nice", "system", "idle", NULL
};

/* these are for detailing the memory statistics */
int memory_stats[7];
/* char *memorynames[] = {
 *   "Real: ", "K/", "K act/tot  ", "Virtual: ", "K/",
 *    "K act/tot  ", "Free: ", "K", NULL
 * };
 */
char *memorynames[] = {
    "K Tot, ", "K Act, ", "K Inact, ", "K Wired, ", "K Free, ", "K in, ", "K out ", NULL
};

/* these are for keeping track of the proc array */
static int bytes;
static int pref_count;
static struct proc *pbase;
static struct proc_unix *pref;

/* these are for getting the memory statistics */

static int pageshift;		/* log base 2 of the pagesize */

/* define pagetok in terms of pageshift */
#define pagetok(size) ((size) << pageshift)

/* useful externals */
extern int errno;
extern char *sys_errlist[];

long lseek();
long time();

machine_init(struct statics *statics)
{
    register int i = 0;
    register int pagesize;
	
    if ((kmem = open(KMEM, O_RDONLY)) == -1) {
	perror(KMEM);
	return(-1);
    }
    if ((mem = open(MEM, O_RDONLY)) == -1) {
	perror(MEM);
	return(-1);
    }

#ifdef DOSWAP
    if ((swap = open(SWAP, O_RDONLY)) == -1) {
	perror(SWAP);
	return(-1);
    }
#endif

    /* get the list of symbols we want to access in the kernel */
    (void) nlist(VMUNIX, nlst);
    if (nlst[0].n_type == 0)
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
    (void) getkval(nlst[X_PROC].n_value, (int *)(&proc), sizeof(proc),
	    			nlst[X_PROC].n_un.n_name);
    (void) getkval(nlst[X_NPROC].n_value, &nproc, sizeof(nproc),
	    			nlst[X_NPROC].n_un.n_name);
    (void) getkval(nlst[X_HZ].n_value, (int *)(&hz), sizeof(hz),
	    			nlst[X_HZ].n_un.n_name);
/*    (void) getkval(nlst[X_CCPU].n_value, (int *)(&ccpu), sizeof(ccpu),
 *	    			nlst[X_CCPU].n_un.n_name);
 */

    /* stash away certain offsets for later use */
    mpid_offset = nlst[X_MPID].n_value;
    avenrun_offset = nlst[X_AVENRUN].n_value;
    total_offset = nlst[X_TOTAL].n_value;
    cp_time_offset = nlst[X_CP_TIME].n_value;
	

    /* this is used in calculating WCPU -- calculate it ahead of time */
/*	ccpu = mach_load_avg();
 *   logcpu = log((double)(ccpu)/LOAD_SCALE);
 */

    /* allocate space for proc structure array and array of pointers */
    bytes = nproc * sizeof(struct proc);
    pbase = (struct proc *)malloc(bytes);
    pref  = (struct proc_unix *)malloc((nproc+1) * sizeof(struct proc_unix *));

    /* Just in case ... */
    if (pbase == (struct proc *)NULL || pref == (struct proc_unix *)NULL)
    {
	fprintf(stderr, "top: can't allocate sufficient memory\n");
	return(-1);
    }

    /* get the page size with "getpagesize" and calculate pageshift from it */
    pagesize = getpagesize();
    pageshift = ceil(log(pagesize)/log(2.0));

    /* we only need the amount of log(2)1024 for our conversion */
    pageshift -= LOG1024;

    /* fill in the statics information */
    statics->procstate_names = procstatenames;
    statics->cpustate_names = cpustatenames;
    statics->memory_names = memorynames;

    /* all done! */
    return(0);
}

char *format_header(register char *uname_field)
{
    register char *ptr;

    ptr = header + UNAME_START;
    while (*uname_field != '\0')
    {
	*ptr++ = *uname_field++;
    }

    return(header);
}

static int swappgsin = -1;
static int swappgsout = -1;
static vm_statistics_data_t vm_stats;
static host_basic_info_data_t  host_stats;

get_system_info(struct system_info *si)
{
    long avenrun[3];
    long total;

    /* get the cp_time array */
    (void) getkval(cp_time_offset, (int *)cp_time, sizeof(cp_time),
		   "_cp_time");

    /* get load average array */
    (void) getkval(avenrun_offset, (int *)avenrun, sizeof(avenrun),
		   "_avenrun");

    /* get mpid -- process id of last process */
    (void) getkval(mpid_offset, &(si->last_pid), sizeof(si->last_pid),
		   "_mpid");

    /* convert load averages to doubles */
    {
	register int i;
	for(i=0; i<3; i++)
		si->load_avg[i] = ((double)avenrun[i])/LSCALE;
    }

    /* convert cp_time counts to percentages */
    total = percentages(CPUSTATES, cpu_states, cp_time, cp_old, cp_diff);

    /* sum memory statistics */
    {
		/* get total -- systemwide main memory usage structure */
		/* Does not work on NeXT system.  Use vm_statistics() for paging info. */
		/* struct vmtotal total;
		 * (void) getkval(total_offset, (int *)(&total), sizeof(total),
		 *	       "_total");
		 */
		/* convert memory stats to Kbytes */
		/* memory_stats[0] = -1;
		 * memory_stats[1] = pagetok(total.t_arm);
		 * memory_stats[2] = pagetok(total.t_rm);
		 * memory_stats[3] = -1;
		 * memory_stats[4] = pagetok(total.t_avm);
		 * memory_stats[5] = pagetok(total.t_vm);
		 * memory_stats[6] = -1;
		 * memory_stats[7] = pagetok(total.t_free);
		 */
		kern_return_t status;
		unsigned int count=HOST_BASIC_INFO_COUNT;
		status = vm_statistics(task_self(), &vm_stats);
#ifdef DEBUG
		if(status != KERN_SUCCESS)
	    	mach_error("An error calling vm_statistics()!", status);
#endif
		status = host_info(host_self(), HOST_BASIC_INFO, (host_info_t)&host_stats, &count);
#ifdef DEBUG
		if(status != KERN_SUCCESS)
	    	mach_error("An error calling host_info()!", status);
#endif
		/* convert memory stats to Kbytes */
		memory_stats[0] = pagetok(host_stats.memory_size / vm_stats.pagesize);
		memory_stats[1] = pagetok(vm_stats.active_count);
		memory_stats[2] = pagetok(vm_stats.inactive_count);
		memory_stats[3] = pagetok(vm_stats.wire_count);
		memory_stats[4] = pagetok(vm_stats.free_count);
        if (swappgsin < 0)
		{
			memory_stats[5] = 1;
			memory_stats[6] = 1;
		} else {
			memory_stats[5] = pagetok(((vm_stats.pageins - swappgsin)));
			memory_stats[6] = pagetok(((vm_stats.pageouts - swappgsout)));
		}
		swappgsin = vm_stats.pageins;
		swappgsout = vm_stats.pageouts;
    }

    /* set arrays and strings */
    si->cpustates = cpu_states;
    si->memory = memory_stats;
}

static struct handle handle;

caddr_t get_process_info(struct system_info *si, 
						 struct process_select *sel, 
						 int (*compare)())
{
    int i, j;
    int total_procs;
    int active_procs;
    struct proc *pp;
	struct task_basic_info taskInfo;
	struct thread_basic_info threadInfo;
	kern_return_t thread_status;
	kern_return_t task_status;
	int threadCount;

    /* these are copied out of sel for speed */
    int show_idle;
    int show_system;
    int show_uid;
    int show_command;

    /* get a pointer to the states summary array */
    si->procstates = process_states;

    /* set up flags which define what we are going to select */
    show_idle = sel->idle;
    show_system = sel->system;
    show_uid = sel->uid != -1;
    show_command = sel->command != NULL;

    (void) getkval(nlst[X_PROC].n_value, (int *)(&proc), sizeof(proc),
	    			nlst[X_PROC].n_un.n_name);

    /* count up process states and get pointers to interesting procs */
    total_procs = 0;
    active_procs = 0;
    memset((char *)process_states, 0, sizeof(process_states));
	i = 0;
	j = 0;
	do {
		if(i == 0) {
   			/* read first proc structure */
    		(void) getkval(proc, (int *)&pbase[i], sizeof(struct proc), "first proc");
 		} else {
    		(void) getkval(pp->p_nxt, (int *)&pbase[i], sizeof(struct proc), "nxt proc");
		}
		pp = &pbase[i];

		thread_status = thread_stats(pp->p_pid, &threadInfo, &threadCount);
		task_status = task_stats(pp->p_pid, &taskInfo);
	/*
	 *  Process slots that are actually in use have a non-zero
	 *  status field.  Processes with SSYS set are system
	 *  processes---these get ignored unless show_sysprocs is set.
	 */
		if (pp->p_stat != 0 &&
	    	(show_system || ((pp->p_flag & SSYS) == 0)))
		{
	    	total_procs++;
/* Using thread info for process states. */
/*	    	process_states[pp->p_stat]++; */
			if(thread_status==KERN_SUCCESS)
	    		process_states[threadInfo.run_state]++;
	    	if ((pp->p_stat != SZOMB) &&
				(show_idle || (pp->p_stat == SRUN)) &&
				(!show_uid || pp->p_uid == (uid_t)sel->uid))
	    	{
				pref[j].p_self = pp;
				if(thread_status==KERN_SUCCESS)
				{
					pref[j].run_state = threadInfo.run_state;
					pref[j].flags = threadInfo.flags;
					pref[j].p_pctcpu = threadInfo.cpu_usage;
					pref[j].p_cptime = threadInfo.user_time.seconds + 
				  					   threadInfo.system_time.seconds;
					pref[j].cur_priority = threadInfo.cur_priority;
					pref[j].nthreads = threadCount;
				} else {
					pref[j].run_state = 0;
					pref[j].flags = 0;
					pref[j].p_pctcpu = 0;
					pref[j].p_cptime = 0;
				}
				/* Get processes memory usage and cputime */
				if(task_status==KERN_SUCCESS)
				{
					pref[j].p_rsize = taskInfo.resident_size/1024;
					pref[j].p_vsize = taskInfo.virtual_size/1024;
				} else {
					pref[j].p_rsize = 0;
					pref[j].p_vsize = 0;
				}
				active_procs++;
				j++;
	    	}
		}
		i++;
	} while(pp->p_nxt != 0);
	pref[j].p_self = NULL;  /*  End list of processes with NULL */

    /* if requested, sort the "interesting" processes */
     if (compare != NULL)
    {
		qsort((char *)pref, active_procs, sizeof(struct proc_unix), compare);
    }

    /* remember active and total counts */
    si->p_total = total_procs;
    si->p_active = pref_count = active_procs;

    /* pass back a handle */
    handle.list = pref;
    handle.count = active_procs;
    handle.current = 0;
    return((caddr_t)&handle);
}

char fmt[MAX_COLS];		/* static area where result is built */

char *format_next_process(caddr_t handle, char *(*get_userid)())
{
    register struct proc *pp;
    register long cputime;
    register double pct, wcpu, pctmem;
    int where;
    struct user u;
    struct handle *hp;
	register int p_pctcpu;
	register int rm_size;
	register int vm_size;
	register int run_state;
	register int flags;
	register int nthreads;
	register int cur_priority;
	char state_str[10];

    /* find and remember the next proc structure */
    hp = (struct handle *)handle;
	pp = hp->list[hp->current].p_self;
    p_pctcpu = hp->list[hp->current].p_pctcpu;
    cputime = hp->list[hp->current].p_cptime;
    rm_size = hp->list[hp->current].p_rsize;
    vm_size = hp->list[hp->current].p_vsize;
	run_state = hp->list[hp->current].run_state;
	flags = hp->list[hp->current].flags;
	nthreads = hp->list[hp->current].nthreads;
	cur_priority = hp->list[hp->current].cur_priority;
	hp->current++;
    hp->count--;

    /* get the process's user struct and set cputime */
    where = getu(pp, &u);
    if (where == -1)
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
		if (where == 1) {
	    	/*
	     	* Print swapped processes as <pname>
	     	*/
	    	char buf[sizeof(u.u_comm)];
	    	(void) strncpy(buf, u.u_comm, sizeof(u.u_comm));
	    	u.u_comm[0] = '<';
	    	(void) strncpy(&u.u_comm[1], buf, sizeof(u.u_comm) - 2);
	    	u.u_comm[sizeof(u.u_comm) - 2] = '\0';
	    	(void) strncat(u.u_comm, ">", sizeof(u.u_comm) - 1);
	    	u.u_comm[sizeof(u.u_comm) - 1] = '\0';
		}
/*	User structure does not work.  Use Thread Info to get cputime for process. */
/*		cputime = u.u_ru.ru_utime.tv_sec + u.u_ru.ru_stime.tv_sec; */
    }


    /* calculate the base for cpu percentages */
    pct = (double)(p_pctcpu)/TH_USAGE_SCALE;
/*	wcpu = weighted_cpu(pct, pp);
 */
	pctmem = (double)(rm_size*1024.) / (double)(host_stats.memory_size);
	
	/* Get process state description */
	if(run_state)
	{
		strcpy(state_str, mach_state[run_state]);
		strcat(state_str, flags_state[flags]);
	} else {
		strcpy(state_str, state_abbrev[pp->p_stat]);
	}
	
    /* format this entry */
    sprintf(fmt,
	    Proc_format,
	    pp->p_pid,
	    (*get_userid)(pp->p_uid),
	    state_str,
		cur_priority,
/*	    pp->p_pri - PZERO, */
	    pp->p_nice - NZERO,
		nthreads,
	    format_k(vm_size),
	    format_k(rm_size),
	    100.0 * pctmem,
/*	    100.0 * wcpu, */
	    100.0 * pct,
	    format_time(cputime),
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

getu(register struct proc *p, struct user *u)
{
    register int nbytes, n;
	struct task task;
	struct utask utask;
	struct uthread thread;

    /*
     *  Check if the process is currently loaded or swapped out.  The way we
     *  get the u area is totally different for the two cases.  For this
     *  application, we just don't bother if the process is swapped out.
     */
	/* NEXTSTEP proc.h
	 * One structure allocated per active
	 * process. It contains all data needed
	 * about the process while the
	 * process may be swapped out.
	 * Other per process data (user.h)
	 * is swapped with the process.
	 */

    if ((p->p_flag & SLOAD) == 0) {
/* User info is always in core.
 * #ifdef DOSWAP
 * 		if (lseek(swap, (long)dtob(p->p_swaddr), 0) == -1) {
 * 	    	perror("lseek(swap)");
 * 	    	return(-1);
 * 		}
 * 		if (read(swap, (char *) u, sizeof(struct user)) != sizeof(struct user))  {
 * 	    	perror("read(swap)");
 * 	    	return(-1);
 * 		}
 * 		return (1);
 * #else
 */
		return(-1);
/*#endif
 */
    }

    /*
     *  Process is currently in memory, we hope!
     */
	if(!getkval(p->task, (int *)&task, sizeof(struct task), "task")) {
#ifdef DEBUG
		perror("getkval(p->task)");
#endif
		/* we can't seem to get to it, so pretend it's swapped out */
		return(-1);
	}

	if(!getkval(task.u_address, (int *)&utask, sizeof(struct utask), "task.u_address")) {
#ifdef DEBUG
		perror("getkval(task->utask)");
#endif
		/* we can't seem to get to it, so pretend it's swapped out */
		return(-1);
	}

	/* Copy utask and uthread info into struct user *u */
	/*  This is incomplete.  Only copied info needed. */
	u->u_procp = utask.uu_procp;
	u->u_ar0 = utask.uu_ar0;
	u->u_ru = utask.uu_ru;
	strcpy(u->u_comm, utask.uu_comm);
	nbytes = strlen(u->u_comm);
	for(n=nbytes; n<MAXCOMLEN; n++)
		u->u_comm[n] = ' ';
	u->u_comm[MAXCOMLEN] = '\0';
	return(0);
}

/*
 * check_nlist(nlst) - checks the nlist to see if any symbols were not
 *		found.  For every symbol that was not found, a one-line
 *		message is printed to stderr.  The routine returns the
 *		number of symbols NOT found.
 */

int check_nlist(register struct nlist *nlst)
{
    register int i;

    /* check to see if we got ALL the symbols we requested */
    /* this will write one line to stderr for every symbol not found */

    i = 0;
    while (nlst->n_un.n_name != NULL)
    {
	if (nlst->n_type == 0 && nlst->n_value == 0)
	{
	    /* this one wasn't found */
	    fprintf(stderr, "kernel: no symbol named `%s'\n", nlst->n_un.n_name);
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

getkval(unsigned long offset, int *ptr, int size, char *refstr)
{
    if (lseek(kmem, (long)offset, L_SET) == -1) {
        if (*refstr == '!')
            refstr++;
        (void) fprintf(stderr, "%s: lseek to %s: %s\n", KMEM, 
		       refstr, strerror(errno));
        quit(23);
    }
    if (read(kmem, (char *) ptr, size) == -1) {
        if (*refstr == '!') 
            return(0);
        else {
            (void) fprintf(stderr, "%s: reading %s: %s\n", KMEM, 
			   refstr, strerror(errno));
            quit(23);
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
 
proc_compare(struct proc_unix *pp1, struct proc_unix *pp2)
{
    register struct proc *p1 = pp1->p_self;
    register struct proc *p2 = pp2->p_self;
    register int result;
    register pctcpu lresult;

    /* compare percent cpu (pctcpu) */
    if ((lresult = pp2->p_pctcpu - pp1->p_pctcpu) == 0)
    {
	/* use cpticks to break the tie */
	if ((result = P_CPTICKS(p2) - P_CPTICKS(p1)) == 0)
	{
	    /* use process state to break the tie */
	    if ((result = sorted_state[p2->p_stat] - sorted_state[p1->p_stat])  == 0)
	    {
		/* use priority to break the tie */
		if ((result = p2->p_pri - p1->p_pri) == 0)
		{
		    /* use resident set size (rssize) to break the tie */
		    if ((result = pp2->p_rsize - pp1->p_rsize) == 0)
		    {
			/* use total memory to break the tie */
			result = pp2->p_vsize - pp1->p_vsize;
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

int proc_owner(int pid)
{
    register int cnt;
    register struct proc *pp;

    cnt = pref_count;
    while (--cnt >= 0)
    {
		pp = pref[cnt].p_self;
		if( pp->p_pid == pid ) 	/* Modified (pid_t)pid to pid, compiler error. */
		{
			return((int)pp->p_uid);
		}
    }
    return(-1);
}

int thread_stats(int pid, struct thread_basic_info *info, int *thread_count)
{
	int 					  i;
	kern_return_t             status;
	kern_return_t			  status_dealloc;
	task_t					  p_task;
	thread_array_t			  thread_list, list;
	struct thread_basic_info  threadInfo;
	unsigned int              info_count = THREAD_BASIC_INFO_COUNT;

	/* Get the task pointer for the process. */
	status = task_by_unix_pid( task_self(), pid, &p_task);
	if (status!=KERN_SUCCESS)
	{
#ifdef DEBUG
		printf("pid = %i\n", pid);
    	mach_error("Error calling task_by_unix_pid()", status);
#endif
		return status;
	}
	
	/* Get the list of threads for the task. */
	status = task_threads(p_task, &thread_list, thread_count);
	if (status!=KERN_SUCCESS)
	{
#ifdef DEBUG
    	mach_error("Error calling task_threads()", status);
#endif
		return status;
	}

	/* Get the pctcpu value for each thread and sum the values */
	info->user_time.seconds = 0;
	info->user_time.microseconds = 0;
	info->system_time.seconds = 0;
	info->system_time.microseconds = 0;
	info->cpu_usage = 0;
	info->sleep_time = 0;

	for(i=0; i<*thread_count; i++)
	{
		status = thread_info(thread_list[i], THREAD_BASIC_INFO, 
						(thread_info_t)&threadInfo, &info_count);
		if (status!=KERN_SUCCESS)
		{
#ifdef DEBUG
    		mach_error("Error calling thread_info()", status);
#endif
			break; 
		} else {
			if(i==0)
			{
				info->base_priority = threadInfo.base_priority;
				info->cur_priority = threadInfo.cur_priority;
				info->run_state = threadInfo.run_state;
				info->flags = threadInfo.flags;
				info->suspend_count = threadInfo.suspend_count;
				info->sleep_time += threadInfo.sleep_time;
			}
			info->user_time.seconds += threadInfo.user_time.seconds;
			info->user_time.microseconds += threadInfo.user_time.microseconds;
			info->system_time.seconds += threadInfo.system_time.seconds;
			info->system_time.microseconds += threadInfo.system_time.microseconds;
			info->cpu_usage += threadInfo.cpu_usage;
		}
	}

	/* Deallocate the list of threads. */
    status_dealloc = vm_deallocate(task_self(), (vm_address_t)thread_list,
						   sizeof(thread_list)*(*thread_count));
    if (status_dealloc != KERN_SUCCESS)
	{
#ifdef DEBUG
        mach_error("Trouble freeing thread_list", status_dealloc);
#endif
		status = status_dealloc;
	}
	return status;
}

int mach_load_avg(void)
{
	kern_return_t                    status;
	host_t                           host;
	unsigned int                     info_count;
	struct processor_set_basic_info  info;
	processor_set_t                  default_set;

	status=processor_set_default(host_self(), &default_set);
	if (status!=KERN_SUCCESS){
    	mach_error("Error calling processor_set_default", status);
    	exit(1);
	}

	info_count=PROCESSOR_SET_BASIC_INFO_COUNT;
	status=processor_set_info(default_set, PROCESSOR_SET_BASIC_INFO,
   							&host, (processor_set_info_t)&info, &info_count);
#ifdef DEBUG
	if (status != KERN_SUCCESS)
    	mach_error("Error calling processor_set_info", status);
#endif
	return info.load_average;
}

kern_return_t task_stats(int pid, struct task_basic_info *info)
{
	kern_return_t             status;
	task_t					  p_task;
	unsigned int              info_count=TASK_BASIC_INFO_COUNT;

	/* Get the task pointer for the process. */
	status = task_by_unix_pid( task_self(), pid, &p_task);
	if (status!=KERN_SUCCESS) {
#ifdef DEBUG
		printf("pid = %i\n", pid);
    	mach_error("Error calling task_by_unix_pid()", status);
#endif
		return(status);
	}

	status=task_info(p_task, TASK_BASIC_INFO, (task_info_t)info, &info_count);
	if (status!=KERN_SUCCESS) {
#ifdef DEBUG
    	mach_error("Error calling task_info()", status);
#endif
		return(status);
	}		
	return(KERN_SUCCESS);
}
