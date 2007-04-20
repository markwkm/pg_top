/*
 * top - a top users display for Unix
 *
 * SYNOPSIS:  For a BSD/OS 2.X system (based on the 4.4BSD Lite system)
 *	      Note process resident sizes could be wrong, but ps shows
 *	      zero for them too..
 *
 * DESCRIPTION:
 * This is the machine-dependent module for BSD/OS 2.X (based on 4.4BSD Lite)
 * Works for:
 *	i386 and maybe sparc
 *
 * CFLAGS: -DHAVE_GETOPT
 *
 * LIBS: -lkvm
 *
 * AUTHOR:  Jeff Polk <polk@bsdi.com> based on the m_44bsd.c code 
 *          by Christos Zoulas <christos@ee.cornell.edu>
 */

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/param.h>

#include "os.h"
#include <stdio.h>
#include <nlist.h>
#include <math.h>
#include <kvm.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/cpustats.h>
#include <sys/resource.h>
#include <sys/dir.h>
#include <sys/dkstat.h>
#include <sys/file.h>
#include <sys/time.h>


#define DOSWAP

static int check_nlist __P((struct nlist *));
static int getkval __P((unsigned long, int *, int, char *));
extern char* printable __P((char *));

#include "top.h"
#include "machine.h"
#include "utils.h"

#define VMUNIX	"/bsd"
#define KMEM	"/dev/kmem"
#define MEM	"/dev/mem"
#ifdef DOSWAP
#define SWAP	"/dev/drum"
#endif

/* get_process_info passes back a handle.  This is what it looks like: */

struct handle
{
    struct kinfo_proc **next_proc;	/* points to next valid proc pointer */
    int remaining;		/* number of pointers remaining */
};

/* declarations for load_avg */
#include "loadavg.h"

#define PP(pp, field) ((pp)->kp_proc . field)
#define EP(pp, field) ((pp)->kp_eproc . field)
#define VP(pp, field) ((pp)->kp_eproc.e_vm . field)

/* define what weighted cpu is.  */
#define weighted_cpu(pct, pp) (PP((pp), p_swtime) == 0 ? 0.0 : \
			 ((pct) / (1.0 - exp(PP((pp), p_swtime) * logcpu))))

/* what we consider to be process size: */
#define PROCSIZE(pp) (VP((pp), vm_tsize) + VP((pp), vm_dsize) + VP((pp), vm_ssize))

/* definitions for indices in the nlist array */
#define X_CCPU		0
#define X_HZ		1

static struct nlist nlst[] = {
    { "_ccpu" },		/* 0 */
    { "_hz" },			/* 1 */
    { 0 }
};

/*
 *  These definitions control the format of the per-process area
 */

static char header[] =
  "  PID X        PRI NICE  SIZE   RES STATE   TIME   WCPU    CPU COMMAND";
/* 0123456   -- field to fill in starts at header+6 */
#define UNAME_START 6

#define Proc_format \
	"%5d %-8.8s %3d %4d %5s %5s %-5s %6s %5.2f%% %5.2f%% %.16s"


/* process state names for the "STATE" column of the display */
/* the extra nulls in the string "run" are for adding a slash and
   the processor number when needed */

char *state_abbrev[] =
{
    "", "sleep", "WAIT", "run\0\0\0", "start", "zomb", "stop"
};


static kvm_t *kd;

/* values that we stash away in _init and use in later routines */

static double logcpu;

/* these are retrieved from the kernel in _init */

static          long hz;
static load_avg  ccpu;


/* these are for calculating cpu state percentages */

static u_long cp_old[CPUSTATES];
static u_long cp_diff[CPUSTATES];

/* these are for detailing the process states */

int process_states[7];
char *procstatenames[] = {
    "", " starting, ", " running, ", " sleeping, ",
    " stopped, ", " zombie, ", 
    NULL
};

/* these are for detailing the cpu states */

int cpu_states[6];
char *cpustatenames[] = {
    "user", "nice", "system", "interrupt", "idle", NULL
};

/* these are for detailing the memory statistics */

int memory_stats[8];
char *memorynames[] = {
    "Real: ", "K/", "K ", "Virt: ", "K/",
    "K ", "Free: ", "K", NULL
};

/* these are for keeping track of the proc array */

static int nproc;
static int onproc = -1;
static int pref_len;
static struct kinfo_proc *pbase;
static struct kinfo_proc **pref;

/* these are for getting the memory statistics */

static int pageshift;		/* log base 2 of the pagesize */

/* define pagetok in terms of pageshift */

#define pagetok(size) ((size) << pageshift)

int
machine_init(statics)

struct statics *statics;

{
    register int i = 0;
    register int pagesize;

    if ((kd = kvm_open(VMUNIX, MEM, SWAP, O_RDONLY, "kvm_open")) == NULL)
	return -1;


    /* get the list of symbols we want to access in the kernel */
    (void) kvm_nlist(kd, nlst);
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
    (void) getkval(nlst[X_HZ].n_value,     (int *)(&hz),	sizeof(hz),
	    nlst[X_HZ].n_name);
    (void) getkval(nlst[X_CCPU].n_value,   (int *)(&ccpu),	sizeof(ccpu),
	    nlst[X_CCPU].n_name);

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

void
get_system_info(si)

struct system_info *si;

{
    u_long total;
    struct cpustats cpu;
    struct loadavg  load;

    /* get load averages */
    {
        size_t size = sizeof(load);
        static int mib[] = { CTL_VM, VM_LOADAVG };

        if (sysctl(mib, 2, &load, &size, NULL, 0) < 0) {
	    (void) fprintf(stderr, "top: sysctl failed: %s\n", strerror(errno))
;
	    bzero(&load, sizeof(load));
        }
    }

    /* convert load averages to doubles */
    {
	register int i;
	register double *infoloadp = si->load_avg;

	for (i = 0; i < 3; i++)
	    *infoloadp++ = ((double) load.ldavg[i]) / load.fscale;
    }

    /* get cp_time counts */
    {
        size_t size = sizeof(cpu);
        static int mib[] = { CTL_KERN, KERN_CPUSTATS };

        if (sysctl(mib, 2, &cpu, &size, NULL, 0) < 0) {
	    (void) fprintf(stderr, "top: sysctl failed: %s\n", strerror(errno))
;
	    bzero(&cpu, sizeof(cpu));
        }
    }

    /* convert cp_time counts to percentages */
    total = percentages(CPUSTATES, cpu_states, cpu.cp_time, cp_old, cp_diff);

    /* sum memory statistics */
    {
	struct vmtotal total;
	size_t size = sizeof(total);
	static int mib[] = { CTL_VM, VM_TOTAL };

	/* get total -- systemwide main memory usage structure */
	if (sysctl(mib, 2, &total, &size, NULL, 0) < 0) {
	    (void) fprintf(stderr, "top: sysctl failed: %s\n", strerror(errno))
;
	    bzero(&total, sizeof(total));
	}
	/* convert memory stats to Kbytes */
	memory_stats[0] = -1;
	memory_stats[1] = pagetok(total.t_arm);
	memory_stats[2] = pagetok(total.t_rm);
	memory_stats[3] = -1;
	memory_stats[4] = pagetok(total.t_avm);
	memory_stats[5] = pagetok(total.t_vm);
	memory_stats[6] = -1;
	memory_stats[7] = pagetok(total.t_free);
    }

    /* set arrays and strings */
    si->cpustates = cpu_states;
    si->memory = memory_stats;
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
    register struct kinfo_proc **prefp;
    register struct kinfo_proc *pp;

    /* these are copied out of sel for speed */
    int show_idle;
    int show_system;
    int show_uid;
    int show_command;

    
    pbase = kvm_getprocs(kd, KERN_PROC_ALL, 0, &nproc);
    if (nproc > onproc)
	pref = (struct kinfo_proc **) realloc(pref, sizeof(struct kinfo_proc *)
		* (onproc = nproc));
    if (pref == NULL || pbase == NULL) {
	(void) fprintf(stderr, "top: Out of memory.\n");
	quit(23);
    }
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
    memset((char *)process_states, 0, sizeof(process_states));
    prefp = pref;
    for (pp = pbase, i = 0; i < nproc; pp++, i++)
    {
	/*
	 *  Place pointers to each valid proc structure in pref[].
	 *  Process slots that are actually in use have a non-zero
	 *  status field.  Processes with SSYS set are system
	 *  processes---these get ignored unless show_sysprocs is set.
	 */
	if (PP(pp, p_stat) != 0 &&
	    (show_system || ((PP(pp, p_flag) & P_SYSTEM) == 0)))
	{
	    total_procs++;
	    process_states[(unsigned char) PP(pp, p_stat)]++;
	    if ((PP(pp, p_stat) != SZOMB) &&
		(show_idle || (PP(pp, p_pctcpu) != 0) || 
		 (PP(pp, p_stat) == SRUN)) &&
		(!show_uid || EP(pp, e_pcred.p_ruid) == (uid_t)sel->uid))
	    {
		*prefp++ = pp;
		active_procs++;
	    }
	}
    }

    /* if requested, sort the "interesting" processes */
    if (compare != NULL)
    {
	qsort((char *)pref, active_procs, sizeof(struct kinfo_proc *), compare)
;
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
    register struct kinfo_proc *pp;
    register long cputime;
    register double pct;
    struct handle *hp;

    /* find and remember the next proc structure */
    hp = (struct handle *)handle;
    pp = *(hp->next_proc++);
    hp->remaining--;
    

    /* get the process's user struct and set cputime */
    if ((PP(pp, p_flag) & P_INMEM) == 0) {
	/*
	 * Print swapped processes as <pname>
	 */
	char *comm = PP(pp, p_comm);
#define COMSIZ sizeof(PP(pp, p_comm))
	char buf[COMSIZ];
	(void) strncpy(buf, comm, COMSIZ);
	comm[0] = '<';
	(void) strncpy(&comm[1], buf, COMSIZ - 2);
	comm[COMSIZ - 2] = '\0';
	(void) strncat(comm, ">", COMSIZ - 1);
	comm[COMSIZ - 1] = '\0';
    }

#if 0
    /* This does not produce the correct results */
    cputime = PP(pp, p_uticks) + PP(pp, p_sticks) + PP(pp, p_iticks);
#endif
    cputime = PP(pp, p_rtime).tv_sec;	/* This does not count interrupts */

    /* calculate the base for cpu percentages */
    pct = pctdouble(PP(pp, p_pctcpu));

    /* format this entry */
    sprintf(fmt,
	    Proc_format,
	    PP(pp, p_pid),
	    (*get_userid)(EP(pp, e_pcred.p_ruid)),
	    PP(pp, p_priority) - PZERO,
	    PP(pp, p_nice) - NZERO,
	    format_k(pagetok(PROCSIZE(pp))),
	    format_k(pagetok(VP(pp, vm_rssize))),
	    state_abbrev[(unsigned char) PP(pp, p_stat)],
	    format_time(cputime),
	    100.0 * weighted_cpu(pct, pp),
	    100.0 * pct,
	    printable(PP(pp, p_comm)));

    /* return the result */
    return(fmt);
}


/*
 * check_nlist(nlst) - checks the nlist to see if any symbols were not
 *		found.  For every symbol that was not found, a one-line
 *		message is printed to stderr.  The routine returns the
 *		number of symbols NOT found.
 */

static int check_nlist(nlst)

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
	    (void) fprintf(stderr, "kernel: no symbol named `%s'\n",
			   nlst->n_name);
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

static int getkval(offset, ptr, size, refstr)

unsigned long offset;
int *ptr;
int size;
char *refstr;

{
    if (kvm_read(kd, offset, (char *) ptr, size) != size)
    {
	if (*refstr == '!')
	{
	    return(0);
	}
	else
	{
	    fprintf(stderr, "top: kvm_read for %s: %s\n",
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
 
int
proc_compare(pp1, pp2)

struct proc **pp1;
struct proc **pp2;

{
    register struct kinfo_proc *p1;
    register struct kinfo_proc *p2;
    register int result;
    register pctcpu lresult;

    /* remove one level of indirection */
    p1 = *(struct kinfo_proc **) pp1;
    p2 = *(struct kinfo_proc **) pp2;

    /* compare percent cpu (pctcpu) */
    if ((lresult = PP(p2, p_pctcpu) - PP(p1, p_pctcpu)) == 0)
    {
	/* use cpticks to break the tie */
	if ((result = PP(p2, p_cpticks) - PP(p1, p_cpticks)) == 0)
	{
	    /* use process state to break the tie */
	    if ((result = sorted_state[(unsigned char) PP(p2, p_stat)] -
			  sorted_state[(unsigned char) PP(p1, p_stat)])  == 0)
	    {
		/* use priority to break the tie */
		if ((result = PP(p2, p_priority) - PP(p1, p_priority)) == 0)
		{
		    /* use resident set size (rssize) to break the tie */
		    if ((result = VP(p2, vm_rssize) - VP(p1, vm_rssize)) == 0)
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
    register struct kinfo_proc **prefp;
    register struct kinfo_proc *pp;

    prefp = pref;
    cnt = pref_len;
    while (--cnt >= 0)
    {
	pp = *prefp++;	
	if (PP(pp, p_pid) == (pid_t)pid)
	{
	    return((int)EP(pp, e_pcred.p_ruid));
	}
    }
    return(-1);
}
