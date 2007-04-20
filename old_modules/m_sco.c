/*
 * top - a top users display for Unix
 *
 * SYNOPSIS:  SCO UNIX
 *
 * DESCRIPTION:
 * This is the machine-dependent module for SCO UNIX.
 * Originally written for BSD4.3 system by Christos Zoulas.
 * Works for:
 * SCO UNIX 3.2v4.2
 *
 * CFLAGS: -iBCS2
 *
 * You HAVE to use compiler option: -iBCS2
 *       (Enforces strict Intel Binary Compatibility Standard 2 conformance.)
 *
 * AUTHOR:  Gregory Shilin <shilin@onyx.co.il>
 *          Georgi Kuzmanov <georgi@sco.aubg.bg>
 */

#include <sys/types.h>
#include <sys/param.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <nlist.h>
#include <math.h>
#include <signal.h>

#include <sys/dir.h>
#include <sys/immu.h>
#include <sys/region.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysinfo.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/var.h>
#include <sys/sysi86.h>

#include "top.h"
#include "machine.h"
#include "utils.h"
#include "loadavg.h"

typedef unsigned long  ulong;
typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

#define VMUNIX	"/unix"
#define KMEM	"/dev/kmem"
#define MEM	"/dev/mem"

/* get_process_info passes back a handle. This is what it looks like: */
struct handle {
   struct proc **next_proc; /* points to next valid proc pointer */
   int           remaining; /* number of pointers remaining */
};

/* define what weighted cpu is */
#define weighted_cpu(pct, pp) ((pp)->p_time == 0 ? 0.0 : \
			 ((pct) / (1.0 - exp((pp)->p_time * logcpu))))

#define bytetok(bytes) ((bytes) >> 10)

/* what we consider to be process size: */
#define PROCSIZE(up) bytetok(ctob((up)->u_tsize + (up)->u_dsize +(up)->u_ssize))

/* definitions for indices in the nlist array */
#define X_V		0  /* System configuration information */
#define X_PROC		1  /* process tables */
#define X_FREEMEM	2  /* current free memory */
#define X_AVAILRMEM	3  /* available resident (not swappable) mem in pages */
#define X_AVAILSMEM	4  /* available swappable memory in pages */
#define X_MAXMEM	5  /* maximum available free memory in clicks */
#define X_PHYSMEM	6  /* physical memory in clicks */
#define X_NSWAP		7  /* size of swap space in blocks */
#define X_HZ		8  /* ticks/second of the clock */
#define X_MPID		9  /* last process id */
#define X_SYSINFO	10 /* system information (cpu states) */
#define X_CUR_CPU	11
#define X_AVENRUN       12 /* load averages */


static struct nlist nlst[] = {
   { "v" },		/* 0 */
   { "proc" },		/* 1 */
   { "freemem" },	/* 2 */
   { "availrmem" },	/* 3 */
   { "availsmem" },	/* 4 */
   { "maxmem" },	/* 5 */
   { "physmem" },	/* 6 */
   { "nswap" },		/* 7 */
   { "Hz" },		/* 8 */
   { "mpid" },		/* 9 */
   { "sysinfo" },	/* 10 */
   { "cur_cpu" },	/* 11 */
   { "avenrun" },	/* 12 */
   { NULL }
};

/*
 *  These definitions control the format of the per-process area
 */

static char header[] =
  "  PID X        PRI NICE   SIZE   RES  STATE   TIME  COMMAND";
/* 0123456   -- field to fill in starts at header+6 */
#define UNAME_START 6

#define Proc_format \
	"%5d %-8.8s %3d %4d  %5s %5dK %-5s %6s  %.28s"

static int kmem, mem;

static double logcpu;

/* these are retrieved from the kernel in _init */
static int   Hz;
static struct var   v;
static ulong proca;
static load_avg  cur_cpu;
static unsigned short int anr[NUM_AVERAGES];


/* these are for detailing the process states */
int process_states[8];
char *procstatenames[] = {
    "", " sleeping, ", " running, ", " zombie, ", " stopped, ",
    " created, ", " onproc, ", " xswapped, ",
    NULL
};

/* process state names for the "STATE" column of the display */
char *state_abbrev[] = {
   "", "sleep", "run", "zomb", "stop", "create", "onpr", "swap"
};

/* these are for calculating cpu state percentages */
#define CPUSTATES	5    /* definition from struct sysinfo */
static time_t cp_time[CPUSTATES];
static time_t cp_old[CPUSTATES];
static time_t cp_diff[CPUSTATES];

/* these are for detailing the cpu states */
int cpu_states[CPUSTATES];
char *cpustatenames[] = {
    "idle", "user", "system", "wait", "sxbrk",
    NULL
};

/* these are for detailing the memory statistics */
int memory_stats[6];
char *memorynames[] = {
    "K phys, ", "K max, ", "K free, ", "K locked, ", "K unlocked, ", "K swap, ", NULL
};

/* these are for keeping track of the proc array */
static int bytes;
static int pref_len;
static struct proc *pbase;
static struct proc **pref;

/* useful externals */
extern int errno;
extern char *sys_errlist[];

long time();
long percentages();

machine_init(statics)
struct statics *statics;
{
ulong ptr;

   if ((kmem = open(KMEM, O_RDONLY)) == -1) {
      perror(KMEM);
      return -1;
   }
   if ((mem = open(MEM, O_RDONLY)) == -1) {
      perror(MEM);
      return -1;
   }

   /* get the list of symbols we want to access in the kernel */
   if (nlist(VMUNIX, nlst) == -1) {
      fprintf(stderr, "top: nlist failed\n");
      return -1;
   }
   /* make sure they were all found */
   /*ZZ
   if (check_nlist(nlst) > 0)
      return -1;
   */

   proca = nlst[X_PROC].n_value;

   /* get the symbol values out of kmem */
   (void) getkval(nlst[X_CUR_CPU].n_value, (int *)(&cur_cpu), sizeof(cur_cpu),
                  nlst[X_CUR_CPU].n_name);
   (void) getkval(nlst[X_HZ].n_value,      (int *)(&Hz),      sizeof(Hz),
                  nlst[X_HZ].n_name);
   (void) getkval(nlst[X_V].n_value,       (int *)(&v),       sizeof(v),
                  nlst[X_V].n_name);

   /* this is used in calculating WCPU -- calculate it ahead of time */
   logcpu = log(fabs(loaddouble(cur_cpu)));

   /* allocate space for proc structure array and array of pointers */
   bytes = v.v_proc * sizeof(struct proc);
   pbase = (struct proc *)malloc(bytes);
   pref  = (struct proc **)malloc(v.v_proc * sizeof(struct proc *));
   if (pbase == (struct proc *)NULL || pref == (struct proc **)NULL) {
      fprintf(stderr, "top: cannot allocate sufficient memory\n");
      return -1;
   }

   /* fill in the statics information */
   statics->procstate_names = procstatenames;
   statics->cpustate_names = cpustatenames;
   statics->memory_names = memorynames;

   return 0;
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
int i;
long total;

   /* get process id of the last process */
   (void) getkval(nlst[X_MPID].n_value,  &(si->last_pid),  sizeof(si->last_pid),
                  nlst[X_MPID].n_name);
   /* get the cp_time array */
   (void) getkval(nlst[X_SYSINFO].n_value, (int *)cp_time, sizeof(cp_time),
                  nlst[X_SYSINFO].n_name);

   /* get the load averages and convert them... */
   (void) getkval(nlst[X_AVENRUN].n_value, (int *)(&anr[0]), sizeof(anr),
                  nlst[X_AVENRUN].n_name);

   for (i = 0; i < NUM_AVERAGES; i++)
       si->load_avg[i] = (double)anr[i]/256.0;


   /* convert cp_time counts to percentages */
   total = percentages(CPUSTATES, cpu_states, cp_time, cp_old, cp_diff);

   /* sum memory statistics */
   (void) getkval(nlst[X_PHYSMEM].n_value, &memory_stats[0],
                  sizeof(memory_stats[0]), nlst[X_PHYSMEM].n_name);
   (void) getkval(nlst[X_MAXMEM].n_value, &memory_stats[1],
                  sizeof(memory_stats[1]), nlst[X_MAXMEM].n_name);
   (void) getkval(nlst[X_FREEMEM].n_value, &memory_stats[2],
                  sizeof(memory_stats[2]), nlst[X_FREEMEM].n_name);
   (void) getkval(nlst[X_AVAILRMEM].n_value, &memory_stats[3],
                  sizeof(memory_stats[3]), nlst[X_AVAILRMEM].n_name);
   (void) getkval(nlst[X_AVAILSMEM].n_value, &memory_stats[4],
                  sizeof(memory_stats[4]), nlst[X_AVAILSMEM].n_name);
   (void) getkval(nlst[X_NSWAP].n_value, &memory_stats[5],
                  sizeof(memory_stats[5]), nlst[X_NSWAP].n_name);
   memory_stats[0] = bytetok(ctob(memory_stats[0]));    /* clicks -> bytes    */
   memory_stats[1] = bytetok(ctob(memory_stats[1]));    /* clicks -> bytes    */
   memory_stats[2] = bytetok(ctob(memory_stats[2]));    /* clicks -> bytes    */
   memory_stats[3] = bytetok(memory_stats[3] * NBPP);   /* # bytes per page   */
   memory_stats[4] = bytetok(memory_stats[4] * NBPP);   /* # bytes per page   */
   memory_stats[5] = bytetok(memory_stats[5] * NBPSCTR);/* # bytes per sector */

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

/* set up flags of what we are going to select */
/* these are copied out of sel for simplicity */
int show_idle = sel->idle;
int show_system = sel->system;
int show_uid = sel->uid != -1;
int show_command = sel->command != NULL;

   /* read all the proc structures in one fell swoop */
   (void) getkval(proca, (int *)pbase, bytes, "proc array");

   /* get a pointer to the states summary array */
   si->procstates = process_states;

   /* count up process states and get pointers to interesting procs */
   total_procs = active_procs = 0;
   memset((char *)process_states, 0, sizeof(process_states));
   prefp = pref;
   for (pp = pbase, i = 0; i < v.v_proc; pp++, i++) {
      /*
       * Place pointers to each valid proc structure in pref[].
       * Process slots that are actually in use have a non-zero
       * status field. Processes with SSYS set are system processes --
       * these are ignored unless show_system is set.
       */
      if (pp->p_stat && (show_system || ((pp->p_flag & SSYS) == 0))) {
         total_procs++;
         process_states[pp->p_stat]++;
	 if ((pp->p_stat != SZOMB) &&
	     (show_idle || (pp->p_stat == SRUN) || (pp->p_stat == SONPROC)) &&
	     (!show_uid || pp->p_uid == (ushort)sel->uid)) {
		*prefp++ = pp;
		active_procs++;
	 }
      }
   }

   /* if requested, sort the "interesting" processes */
   if (compare)
      qsort((char *)pref, active_procs, sizeof(struct proc *), compare);

   /* remember active and total counts */
   si->p_total = total_procs;
   si->p_active = pref_len = active_procs;

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
register time_t cputime;
register double pct;
int where;
struct user u;
struct handle *hp;
char command[29];

   /* find and remember the next proc structure */
   hp = (struct handle *)handle;
   pp = *(hp->next_proc++);
   hp->remaining--;

   /* get the process's user struct and set cputime */
   if ((where = sysi86(RDUBLK, pp->p_pid, &u, sizeof(struct user))) != -1)
      where = (pp->p_flag & SLOAD) ? 0 : 1;
   if (where == -1) {
      strcpy(command, "<swapped>");
      cputime = 0;
   } else {
      /* set u_comm for system processes */
      if (u.u_comm[0] == '\0') {
	 if (pp->p_pid == 0)
	    strcpy(command, "Swapper");
	 else if (pp->p_pid == 2)
	    strcpy(command, "Pager");
	 else if (pp->p_pid == 3)
	    strcpy(command, "Sync'er");
      } else if (where == 1) {
	 /* print swapped processes as <pname> */
	 register char *s1;

	 u.u_psargs[28 - 3] = '\0';
	 strcpy(command, "<");
	 strcat(command, strtok(u.u_psargs, " "));
	 strcat(command, ">");
	 while (s1 = (char *)strtok(NULL, " "))
	    strcat(command, s1);
      } else {
	 sprintf(command, "%s", u.u_psargs);
      }
      cputime = u.u_utime + u.u_stime;
   }
   /* calculate the base for cpu percentages */
   pct = pctdouble(pp->p_cpu);

   /* format this entry */
   sprintf(fmt,
	   Proc_format,
	   pp->p_pid,
	   (*get_userid)(pp->p_uid),
	   pp->p_pri - PZERO,
	   pp->p_nice - NZERO,
	   format_k(PROCSIZE(&u)),
	   0,
	   state_abbrev[pp->p_stat],
	   format_time(cputime / Hz),
	   printable(command));

   return(fmt);
}

/*
 * Checks the nlist to see if any symbols were not found.
 * For every symbol that was not found, a one-line message
 * is printed to stderr. The routine returns the number of
 * symbols NOT founded.
 */

int check_nlist(nlst)
    register struct nlist *nlst;
{
register int i = 0;

   while (nlst->n_name) {
      if (nlst->n_type == 0) {
	 fprintf(stderr, "kernel: no symbol named `%s'\n", nlst->n_name);
	 i++;
      }
      nlst++;
   }
   return i;
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
   if (lseek(kmem, (long)offset, SEEK_SET) == -1) {
      if (*refstr == '!')
         refstr++;
      fprintf(stderr, "%s: lseek to %s: %s\n", KMEM,
	       refstr, errmsg(errno));
      quit(23);
   }
   if (read(kmem, (char *)ptr, size) == -1) {
      if (*refstr == '!')
         return 0;
      fprintf(stderr, "%s: reading %s: %s\n", KMEM,
	       refstr, errmsg(errno));
      quit(23);
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
    5,	/* sleep		*/
    6,	/* run			*/
    2,	/* zombie		*/
    4,	/* stop			*/
    1,	/* start		*/
    7,	/* onpr    		*/
    3,	/* swap            	*/
};
 
proc_compare(pp1, pp2)
struct proc **pp1;
struct proc **pp2;

{
register struct proc *p1;
register struct proc *p2;
register int result;
register ulong lresult;

   /* remove one level of indirection */
   p1 = *pp1;
   p2 = *pp2;

   /* use process state to break the tie */
   if ((result = sorted_state[p2->p_stat] -
		 sorted_state[p1->p_stat])  == 0)
   {
      /* use priority to break the tie */
      if ((result = p2->p_pri - p1->p_pri) == 0)
      {
	 /* use time to break the tie */
	 if ((result = (p2->p_utime + p2->p_stime) -
		       (p1->p_utime + p1->p_stime)) == 0)
	 {
	    /* use resident set size (rssize) to break the tie */
	    if ((result = p2->p_size - p1->p_size) == 0)
	    {
	       result = 0;
	    }
	 }
      }
   }

    return(result);
}

/* returns uid of owner of process pid */
proc_owner(pid)
int pid;
{
register int cnt;
register struct proc **prefp;
register struct proc  *pp;

   prefp = pref;
   cnt = pref_len;
   while (--cnt >= 0) {
      if ((pp = *prefp++)->p_pid == (short)pid)
	 return ((int)pp->p_uid);
   }
   return(-1);
}

int setpriority(int dummy, int who, int nicewal)
{
   errno = 1;
   return -1;
}

/* sigblock is not POSIX conformant */
sigset_t sigblock (sigset_t mask)
{
sigset_t oset;

   sigemptyset(&oset);
   sigprocmask(SIG_BLOCK, &mask, &oset);
   return oset;
}

/* sigsetmask is not POSIX conformant */
sigsetmask(sigset_t mask)
{
sigset_t oset;

   sigemptyset(&oset);
   sigprocmask(SIG_SETMASK, &mask, &oset);
   return oset;
}

