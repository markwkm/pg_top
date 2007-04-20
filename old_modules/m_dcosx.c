/*
 * top - a top users display for Unix
 *
 * SYNOPSIS:  For Pyramid DC/OSX
 *
 * DESCRIPTION:
 *      DC/OSX for MISserver
 *      DC/OSX for Nile
 *
 * LIBS:  -lelf -lext
 *
 * AUTHORS:  Phillip Wu         <pwu01@qantek.com.au>
 */

#include "top.h"
#include "machine.h"
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
#include <sys/tuneable.h>
#include <sys/statis.h>
#include <sys/proc.h>
#include <sys/procfs.h>
#include <sys/sysinfo.h>
#include <sys/immu.h>
#include <sys/sysmacros.h>
#include <sys/vmmeter.h>
#include <vm/anon.h>
#include <sys/priocntl.h>
#include <sys/rtpriocntl.h>
#include <sys/tspriocntl.h>
#include <sys/procset.h>
#include <sys/var.h>

#define UNIX "/stand/unix"
#define KMEM "/dev/kmem"
#define PROCFS "/proc"
#define MAXCPU 24
#define CPUSTATES	5

#ifndef PRIO_MAX
#define PRIO_MAX	20
#endif
#ifndef PRIO_MIN
#define PRIO_MIN	-20
#endif

#ifndef FSCALE
#define FSHIFT  8		/* bits to right of fixed binary point */
#define FSCALE  (1<<FSHIFT)
#endif

#define loaddouble(x) ((double)(x) / FSCALE)
#define percent_cpu(x) ((double)(x)->pr_cpu / FSCALE)
#define weighted_cpu(pct, pp) ( ((pp)->pr_time.tv_sec) == 0 ? 0.0 : \
        ((pp)->pr_cpu) / ((pp)->pr_time.tv_sec) )
#define pagetok(size) ctob(size) >> LOG1024

/* definitions for the index in the nlist array */
#define X_AVENRUN	0
#define X_MPID		1
#define X_V		2
#define X_NPROC		3
#define X_PHYSMEM	4

static struct nlist nlst[] =
{
  {"avenrun"},			/* 0 */
  {"mpid"},			/* 1 */
  {"v"},			/* 2 */
  {"nproc"},			/* 3 */
  {"physmem"},			/* 4 */
  {NULL}
};

static unsigned long avenrun_offset;
static unsigned long mpid_offset;
static unsigned long nproc_offset;
static unsigned long physmem_offset;

/* get_process_info passes back a handle.  This is what it looks like: */

struct handle
  {
    struct prpsinfo **next_proc;/* points to next valid proc pointer */
    int remaining;		/* number of pointers remaining */
  };

/*
 *  These definitions control the format of the per-process area
 */

static char header[] =
"  PID X        PRI NICE  SIZE   RES STATE   TIME   WCPU    CPU COMMAND";
/* 0123456   -- field to fill in starts at header+6 */
#define UNAME_START 6
#define Proc_format \
	"%5d %-8.8s %3d %4d %5s %5s %-5s %6s %3d.0%% %5.2f%% %.16s"

char *state_abbrev[] =
{"", "sleep", "run", "zombie", "stop", "start", "cpu", "swap"};

int process_states[8];
char *procstatenames[] =
{
  "", " sleeping, ", " running, ", " zombie, ", " stopped, ",
  " starting, ", " on cpu, ", " swapped, ",
  NULL
};

int cpu_states[CPUSTATES];
char *cpustatenames[] =
{"idle", "user", "kernel", "wait", "swap", NULL};

/* these are for detailing the memory statistics */

int memory_stats[5];
char *memorynames[] =
{"K real, ", "K active, ", "K free, ", "K swap, ", "K free swap", NULL};

static int kmem = -1;
static int nproc;
static int bytes;
static struct prpsinfo *pbase;
static struct prpsinfo **pref;
static DIR *xprocdir;

/* useful externals */
extern int errno;
extern char *sys_errlist[];
extern char *myname;
extern int check_nlist ();
extern int getkval ();
extern void perror ();
extern void getptable ();
extern void quit ();
extern int nlist ();

int
machine_init (struct statics *statics)
  {
    static struct var v;

    /* fill in the statics information */
    statics->procstate_names = procstatenames;
    statics->cpustate_names = cpustatenames;
    statics->memory_names = memorynames;

    /* get the list of symbols we want to access in the kernel */
    if (nlist (UNIX, nlst))
      {
	(void) fprintf (stderr, "Unable to nlist %s\n", UNIX);
	return (-1);
      }

    /* make sure they were all found */
    if (check_nlist (nlst) > 0)
      return (-1);

    /* open kernel memory */
    if ((kmem = open (KMEM, O_RDONLY)) == -1)
      {
	perror (KMEM);
	return (-1);
      }

    /* get the symbol values out of kmem */
    /* NPROC Tuning parameter for max number of processes */
    (void) getkval (nlst[X_V].n_value, &v, sizeof (struct var), nlst[X_V].n_name);
    nproc = v.v_proc;

    /* stash away certain offsets for later use */
    mpid_offset = nlst[X_MPID].n_value;
    nproc_offset = nlst[X_NPROC].n_value;
    avenrun_offset = nlst[X_AVENRUN].n_value;
    physmem_offset = nlst[X_PHYSMEM].n_value;

    /* allocate space for proc structure array and array of pointers */
    bytes = nproc * sizeof (struct prpsinfo);
    pbase = (struct prpsinfo *) malloc (bytes);
    pref = (struct prpsinfo **) malloc (nproc * sizeof (struct prpsinfo *));

    /* Just in case ... */
    if (pbase == (struct prpsinfo *) NULL || pref == (struct prpsinfo **) NULL)
      {
	(void) fprintf (stderr, "%s: can't allocate sufficient memory\n", myname);
	return (-1);
      }

    if (!(xprocdir = opendir (PROCFS)))
      {
	(void) fprintf (stderr, "Unable to open %s\n", PROCFS);
	return (-1);
      }

    if (chdir (PROCFS))
      {				/* handy for later on when we're reading it */
	(void) fprintf (stderr, "Unable to chdir to %s\n", PROCFS);
	return (-1);
      }

    /* all done! */
    return (0);
  }

char *
format_header (char *uname_field)
{
  register char *ptr;

  ptr = header + UNAME_START;
  while (*uname_field != '\0')
    *ptr++ = *uname_field++;

  return (header);
}

static int get_sysinfo_firsttime=0;
static int physmem;
static size_t   sysinfo_size, vmtotal_size, minfo_size;
static int ncpu;
void
get_system_info (struct system_info *si)
{
  long avenrun[3];
  static struct sysinfo sysinfo[MAXCPU];
  static struct vmtotal vmtotal;
  static struct minfo minfo;
  static time_t cp_time[CPUSTATES];
  static time_t cp_old[CPUSTATES];
  static time_t cp_diff[CPUSTATES];	/* for cpu state percentages */
  register int i, j, k, cpu;

  /* Get number of cpus and size of system information data structure
     but only first time */
  if( ! get_sysinfo_firsttime )
  {
     get_sysinfo_firsttime=1;
     if (statis("ncpu", STATIS_GET, &ncpu, sizeof(ncpu))== -1)
     {
      perror("failed to get sysinfo");
      exit(1);
     }
     if (statis("sysinfo", STATIS_SIZ, &sysinfo_size, sizeof(sysinfo_size))==-1)
     {
      perror("failed to get sysinfo");
      exit(1);
     }
     if (statis("vm total", STATIS_SIZ, &vmtotal_size, sizeof(vmtotal_size))==-1)
     {
      perror("failed to get vm total");
      exit(1);
     }
     if (statis("minfo", STATIS_SIZ, &minfo_size, sizeof(minfo_size))==-1)
     {
      perror("failed to get minfo");
      exit(1);
     }
     sysinfo_size *= ncpu;
    (void) getkval (physmem_offset, (int *)(&physmem), sizeof(int), "physmem");
    physmem=physmem<<2;
    memory_stats[0] = (physmem/1024)*1000;
   }
 
  /* Get system information  data structure from the kernel - one per cpu */
  if( statis("sysinfo", STATIS_GET,  sysinfo, sysinfo_size) != sysinfo_size )
  {
    perror("failed to get sysinfo");
    exit(1);
  }

  for( j = 0; j < CPUSTATES; j++)
  {
    cp_time[j] = 0;
    for( cpu = 1; cpu < ncpu; cpu++)
      cp_time[j] += sysinfo[cpu].cpu[j];
    cp_time[j] /= ncpu;
   }

  /* convert cp_time counts to percentages */
  (void) percentages (CPUSTATES, cpu_states, cp_time, cp_old, cp_diff);

  /* get mpid -- process id of last process */
  (void) getkval (mpid_offset, &(si->last_pid), sizeof (si->last_pid),
		  "mpid");

  /* get load average array */
  (void) getkval (avenrun_offset, (int *) avenrun, sizeof (avenrun), "avenrun");

  /* convert load averages to doubles */
  for (i = 0; i < 3; i++)
    si->load_avg[i] = loaddouble (avenrun[i]);

  /* get vmmeter and minfo */
  if( statis("vm total", STATIS_GET,  &vmtotal, vmtotal_size) != vmtotal_size )
  {
    perror("failed to get vm total");
    exit(1);
  }
  if( statis("minfo", STATIS_GET,  &minfo, minfo_size) != minfo_size )
  {
    perror("failed to get minfo");
    exit(1);
  }
  /* convert memory stats to Kbytes */
  memory_stats[1] = pagetok (vmtotal.t_arm);
  memory_stats[2] = pagetok (vmtotal.t_free);
  memory_stats[3] = pagetok (minfo.swap);
  memory_stats[4] = pagetok (minfo.freeswap);

  /* set arrays and strings */
  si->cpustates = cpu_states;
  si->memory = memory_stats;
}

static struct handle handle;

caddr_t
get_process_info (
		   struct system_info *si,
		   struct process_select *sel,
		   int (*compare) ())
{
  register int i;
  register int total_procs;
  register int active_procs;
  register struct prpsinfo **prefp;
  register struct prpsinfo *pp;

  /* these are copied out of sel for speed */
  int show_idle;
  int show_system;
  int show_uid;

  /* Get current number of processes */
  (void) getkval (nproc_offset, (int *) (&nproc), sizeof (nproc), "nproc");

  /* read all the proc structures */
  getptable (pbase);

  /* get a pointer to the states summary array */
  si->procstates = process_states;

  /* set up flags which define what we are going to select */
  show_idle = sel->idle;
  show_system = sel->system;
  show_uid = sel->uid != -1;

  /* count up process states and get pointers to interesting procs */
  total_procs = 0;
  active_procs = 0;
  (void) memset (process_states, 0, sizeof (process_states));
  prefp = pref;

  for (pp = pbase, i = 0; i < nproc; pp++, i++)
    {
      /*
	 *  Place pointers to each valid proc structure in pref[].
	 *  Process slots that are actually in use have a non-zero
	 *  status field.  Processes with SSYS set are system
	 *  processes---these get ignored unless show_sysprocs is set.
	 */
      if (pp->pr_state != 0 &&
	  (show_system || ((pp->pr_flag & SSYS) == 0)))
	{
	  total_procs++;
	  process_states[pp->pr_state]++;
	  if ((!pp->pr_zomb) &&
	      (show_idle || (pp->pr_state == SRUN) || (pp->pr_state == SONPROC)) &&
	      (!show_uid || pp->pr_uid == (uid_t) sel->uid))
	    {
	      *prefp++ = pp;
	      active_procs++;
	    }
	}
    }

  /* if requested, sort the "interesting" processes */
  if (compare != NULL)
      qsort ((char *) pref, active_procs, sizeof (struct prpsinfo *), compare);

  /* remember active and total counts */
  si->p_total = total_procs;
  si->p_active = active_procs;

  /* pass back a handle */
  handle.next_proc = pref;
  handle.remaining = active_procs;
  return ((caddr_t) & handle);
}

char fmt[MAX_COLS];			/* static area where result is built */

char *
format_next_process (
		      caddr_t handle,
		      char *(*get_userid) ())
{
  register struct prpsinfo *pp;
  struct handle *hp;
  register long cputime;
  register double pctcpu;

  /* find and remember the next proc structure */
  hp = (struct handle *) handle;
  pp = *(hp->next_proc++);
  hp->remaining--;

  /* get the cpu usage and calculate the cpu percentages */
  cputime = pp->pr_time.tv_sec;
  pctcpu = percent_cpu (pp);

  /* format this entry */
  (void) sprintf (fmt,
		  Proc_format,
		  pp->pr_pid,
		  (*get_userid) (pp->pr_uid),
		  pp->pr_pri - PZERO,
		  pp->pr_nice - NZERO,
		  format_k(pagetok (pp->pr_size)),
		  format_k(pagetok (pp->pr_rssize)),
		  state_abbrev[pp->pr_state],
		  format_time(cputime),
		  (pp->pr_cpu & 0377),
		  100.0 * pctcpu,
		  pp->pr_fname);

  /* return the result */
  return (fmt);
}

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

  /* check to see if we got ALL the symbols we requested */
  /* this will write one line to stderr for every symbol not found */

  i = 0;
  while (nlst->n_name != NULL)
    {
      if (nlst->n_type == 0)
	{
	  /* this one wasn't found */
	  (void) fprintf (stderr, "kernel: no symbol named `%s'\n", nlst->n_name);
	  i = 1;
	}
      nlst++;
    }
  return (i);
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
int
getkval (
	  unsigned long offset,
	  int *ptr,
	  int size,
	  char *refstr)
{
#ifdef MIPS
  if (lseek (kmem, (long) (offset & 0x7fffffff), 0) == -1)
#else
  if (lseek (kmem, (long) offset, 0) == -1)
#endif
    {
      if (*refstr == '!')
	refstr++;
      (void) fprintf (stderr, "%s: lseek to %s: %s\n",
		      myname, refstr, sys_errlist[errno]);
      quit (22);
    }
  if (read (kmem, (char *) ptr, size) == -1)
    if (*refstr == '!')
      /* we lost the race with the kernel, process isn't in memory */
      return (0);
    else
      {
	(void) fprintf (stderr, "%s: reading %s: %s\n",
			myname, refstr, sys_errlist[errno]);
	quit (23);
      }
  return (1);
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


unsigned char sorted_state[] =
{
  0,				/* not used		*/
  3,				/* sleep		*/
  6,				/* run			*/
  2,				/* zombie		*/
  4,				/* stop			*/
  5,				/* start		*/
  7,				/* run on a processor   */
  1				/* being swapped (WAIT)	*/
};

int
proc_compare (
	       struct prpsinfo **pp1,
	       struct prpsinfo **pp2)
  {
    register struct prpsinfo *p1;
    register struct prpsinfo *p2;
    register long result;

    /* remove one level of indirection */
    p1 = *pp1;
    p2 = *pp2;

    /* compare percent cpu (pctcpu) */
    if ((result = (long) (p2->pr_cpu - p1->pr_cpu)) == 0)
      {
	/* use cpticks to break the tie */
	if ((result = p2->pr_time.tv_sec - p1->pr_time.tv_sec) == 0)
	  {
	    /* use process state to break the tie */
	    if ((result = (long) (sorted_state[p2->pr_state] -
				  sorted_state[p1->pr_state])) == 0)
	      {
		/* use priority to break the tie */
		if ((result = p2->pr_oldpri - p1->pr_oldpri) == 0)
		  {
		    /* use resident set size (rssize) to break the tie */
		    if ((result = p2->pr_rssize - p1->pr_rssize) == 0)
		      {
			/* use total memory to break the tie */
			result = (p2->pr_size - p1->pr_size);
		      }
		  }
	      }
	  }
      }
    return (result);
  }

/*
get process table
*/
void
getptable (struct prpsinfo *baseptr)
{
  struct prpsinfo *currproc;	/* pointer to current proc structure	*/
  int numprocs = 0;
  struct dirent *direntp;

  for (rewinddir (xprocdir); direntp = readdir (xprocdir);)
    {
      int fd;

      if ((fd = open (direntp->d_name, O_RDONLY)) < 0)
	continue;

      currproc = &baseptr[numprocs];
      if (ioctl (fd, PIOCPSINFO, currproc) < 0)
	{
	  (void) close (fd);
	  continue;
	}

      numprocs++;
      (void) close (fd);
    }

  if (nproc != numprocs)
    nproc = numprocs;
}

/* return the owner of the specified process, for use in commands.c as we're
   running setuid root */
uid_t
proc_owner (pid_t pid)
{
  register struct prpsinfo *p;
  int i;
  for (i = 0, p = pbase; i < nproc; i++, p++)
    if (p->pr_pid == pid)
      return (p->pr_uid);

  return (-1);
}

int
setpriority (int dummy, int who, int niceval)
{
  int scale;
  int prio;
  pcinfo_t pcinfo;
  pcparms_t pcparms;
  tsparms_t *tsparms;

  strcpy (pcinfo.pc_clname, "TS");
  if (priocntl (0, 0, PC_GETCID, (caddr_t) & pcinfo) == -1)
    return (-1);

  prio = niceval;
  if (prio > PRIO_MAX)
    prio = PRIO_MAX;
  else if (prio < PRIO_MIN)
    prio = PRIO_MIN;

  tsparms = (tsparms_t *) pcparms.pc_clparms;
  scale = ((tsinfo_t *) pcinfo.pc_clinfo)->ts_maxupri;
  tsparms->ts_uprilim = tsparms->ts_upri = -(scale * prio) / 20;
  pcparms.pc_cid = pcinfo.pc_cid;

  if (priocntl (P_PID, who, PC_SETPARMS, (caddr_t) & pcparms) == -1)
    return (-1);

  return (0);
}
