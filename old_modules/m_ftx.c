/*
 * top - a top users display for Unix
 *
 * SYNOPSIS:  For FTX based System V Release 4
 *
 * DESCRIPTION:
 *      System V release 4.0.x for FTX (FTX 2.3 and greater)
 *
 * LIBS:  -lelf
 *
 * AUTHORS:  Andrew Herbert     <andrew@werple.apana.org.au>
 *           Robert Boucher     <boucher@sofkin.ca>
 *           Steve Scherf	<scherf@swdc.stratus.com>
 */

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
#include <sys/procfs.h>
#include <sys/sysmacros.h>
#include <sys/sysinfo.h>
#include <sys/vmmeter.h>
#include <vm/anon.h>
#include <sys/priocntl.h>
#include <sys/rtpriocntl.h>
#include <sys/tspriocntl.h>
#include <sys/procset.h>
#include <sys/var.h>
#include <sys/tuneable.h>
#include <sys/fs/rf_acct.h>
#include <sys/sar.h>
#include <sys/ftx/dcm.h>

#include "top.h"
#include "machine.h"

#define UNIX "/unix"
#define KMEM "/dev/kmem"
#define PROCFS "/proc"
#define SAR "/dev/sar"
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
#define pagetok(size) ctob(size) >> LOG1024
#define PRTOMS(pp) \
	((pp)->pr_time.tv_sec * 1000) + ((pp)->pr_time.tv_nsec / 1000000)

/* definitions for the index in the nlist array */
#define X_AVENRUN	0
#define X_MPID		1
#define X_V		2
#define X_NPROC		3
#define X_ANONINFO	4
#define X_TOTAL		5

static struct nlist nlst[] =
{
  {"avenrun"},			/* 0 */
  {"mpid"},			/* 1 */
  {"v"},			/* 2 */
  {"nproc"},			/* 3 */
  {"anoninfo"},			/* 4 */
  {"total"},			/* 5 */
  {NULL}
};

static unsigned long avenrun_offset;
static unsigned long mpid_offset;
static unsigned long nproc_offset;
static unsigned long anoninfo_offset;
static unsigned long total_offset;

/* get_process_info passes back a handle.  This is what it looks like: */

struct handle
  {
    struct prpsinfo **next_proc;/* points to next valid proc pointer */
    int remaining;		/* number of pointers remaining */
  };

#define MAXTIMEHIST	12
#define HASHSZ		512	/* This must be a power of 2. */
#define HASHMASK	(HASHSZ - 1)
#define TF_USED		0x01
#define TF_NEWPROC	0x02

#define TD_HASH(pid) \
	(timedata_t *)(&hash[(pid) & HASHMASK])

typedef struct hash {
	struct timedata *hnext;
	struct timedata *hlast;
} hash_t;

/* data for CPU and WCPU fields */
typedef struct timedata {
	struct timedata *hnext;
	struct timedata *hlast;
	struct timedata *lnext;
	struct timedata *llast;
	pid_t pid;
	char index;
	char cnt;
	char flags;
	long hist[MAXTIMEHIST];
	long time;
	long ltime;
} timedata_t;


/*
 *  These definitions control the format of the per-process area
 */

static char header[] =
"  PID X        PRI NICE   SIZE   RES STATE   TIME   WCPU    CPU COMMAND";
/* 0123456   -- field to fill in starts at header+6 */
#define UNAME_START 6
#define Proc_format \
	"%5d %-8.8s %3d %4d%6dK %4dK %-5s%4d:%02d %5.2f%% %5.2f%% %.16s"

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

static int kmem;
static int sar;
static int initted;
static int nproc;
static int bytes;
static struct prpsinfo *pbase;
static struct prpsinfo **pref;
static DIR *procdir;
static char cpu_state[MAX_LOG_CPU];
static struct sysinfo cpu_sysinfo[MAX_LOG_CPU];
static sar_percpu_args_t spa;
static timedata_t timedata;
static long total_time;
static double total_cpu;
static hash_t hash[HASHSZ];

/* useful externals */
extern int errno;
extern char *sys_errlist[];
extern char *myname;
extern long percentages ();
extern int check_nlist ();
extern int getkval ();
extern void perror ();
extern void getptable ();
extern void quit ();
extern int nlist ();

/* Prototypes. */
void getsysinfo(struct sysinfo *);
void add_time(struct prpsinfo *);
void get_cpu(struct prpsinfo *, double *, double *);
void clean_timedata(void);
timedata_t *get_timedata(struct prpsinfo *);


int
machine_init (struct statics *statics)
  {
    int i;
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

    /* Open the sar driver device node. */
    if ((sar = open(SAR, O_RDONLY)) == -1)
      {
        perror (SAR);
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
    anoninfo_offset = nlst[X_ANONINFO].n_value;
    total_offset = nlst[X_TOTAL].n_value;

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

    if (!(procdir = opendir (PROCFS)))
      {
	(void) fprintf (stderr, "Unable to open %s\n", PROCFS);
	return (-1);
      }

    if (chdir (PROCFS))
      {				/* handy for later on when we're reading it */
	(void) fprintf (stderr, "Unable to chdir to %s\n", PROCFS);
	return (-1);
      }

    /* Set up the pointers to the sysinfo data area. */
    spa.uvcp = (caddr_t) &cpu_state[0];
    spa.uvsp = (caddr_t) &cpu_sysinfo[0];

    timedata.lnext = &timedata;
    timedata.llast = &timedata;

    for (i = 0; i < HASHSZ; i++) {
      hash[i].hnext = (timedata_t *)&hash[i];
      hash[i].hlast = (timedata_t *)&hash[i];
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

void
get_system_info (struct system_info *si)
{
  long avenrun[3];
  struct sysinfo sysinfo;
  struct vmtotal total;
  struct anoninfo anoninfo;
  static time_t cp_old[CPUSTATES];
  static time_t cp_diff[CPUSTATES];	/* for cpu state percentages */
  register int i;

  getsysinfo(&sysinfo);

  /* convert cp_time counts to percentages */
  (void) percentages (CPUSTATES, cpu_states, sysinfo.cpu, cp_old, cp_diff);

  /* Find total CPU utilization, as a fraction of 1. */
  total_cpu = (cpu_states[CPU_USER] + cpu_states[CPU_KERNEL]) / 1000.0;

  /* get mpid -- process id of last process */
  (void) getkval (mpid_offset, &(si->last_pid), sizeof (si->last_pid),
		  "mpid");

  /* get load average array */
  (void) getkval (avenrun_offset, (int *) avenrun, sizeof (avenrun), "avenrun");

  /* convert load averages to doubles */
  for (i = 0; i < 3; i++)
    si->load_avg[i] = loaddouble (avenrun[i]);

  /* get total -- systemwide main memory usage structure */
  (void) getkval (total_offset, (int *) (&total), sizeof (total), "total");
  /* convert memory stats to Kbytes */
  memory_stats[0] = pagetok (total.t_rm);
  memory_stats[1] = pagetok (total.t_arm);
  memory_stats[2] = pagetok (total.t_free);
  (void) getkval (anoninfo_offset, (int *) (&anoninfo), sizeof (anoninfo),
		  "anoninfo");
  memory_stats[3] = pagetok (anoninfo.ani_max - anoninfo.ani_free);
  memory_stats[4] = pagetok (anoninfo.ani_max - anoninfo.ani_resv);

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
  total_time = 0;
  (void) memset (process_states, 0, sizeof (process_states));
  prefp = pref;

  clean_timedata();

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

      if (pp->pr_state != 0)
        add_time(pp);
    }

  /* Note that we've run this at least once. */
  initted++;

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

char fmt[128];			/* static area where result is built */

char *
format_next_process (
		      caddr_t handle,
		      char *(*get_userid) ())
{
  register struct prpsinfo *pp;
  struct handle *hp;
  register long cputime;
  double pctcpu;
  double pctwcpu;

  /* find and remember the next proc structure */
  hp = (struct handle *) handle;
  pp = *(hp->next_proc++);
  hp->remaining--;

  /* get the cpu usage and calculate the cpu percentages */
  cputime = pp->pr_time.tv_sec;
  get_cpu(pp, &pctcpu, &pctwcpu);

  /* format this entry */
  (void) sprintf (fmt,
		  Proc_format,
		  pp->pr_pid,
		  (*get_userid) (pp->pr_uid),
		  pp->pr_pri - PZERO,
		  pp->pr_nice - NZERO,
		  pagetok (pp->pr_size),
		  pagetok (pp->pr_rssize),
		  state_abbrev[pp->pr_state],
		  cputime / 60l,
		  cputime % 60l,
		  pctwcpu,
		  pctcpu,
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
  if (lseek (kmem, (long) offset, 0) == -1)
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
    register long d1;
    register long d2;
    register timedata_t *td;

    /* remove one level of indirection */
    p1 = *pp1;
    p2 = *pp2;

    td = get_timedata(p1);
    if (td->ltime == -1)
      d1 = 0;
    else
      d1 = td->time - td->ltime;

    td = get_timedata(p2);
    if (td->ltime == -1)
      d2 = 0;
    else
      d2 = td->time - td->ltime;

    /* compare cpu usage */
    if ((result = d2 - d1) == 0)
      {
	/* use cpticks to break the tie */
	if ((result = (PRTOMS(p2) - PRTOMS(p1))) == 0)
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

  for (rewinddir (procdir); direntp = readdir (procdir);)
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


/*
 * Per-process CPU calculation:
 *
 * We emulate actual % CPU usage calculation, since the statistics
 * kept by FTX are not valid for this purpose. We fake this calculation
 * by totalling the amount of CPU time used by all processes since the
 * last update, and dividing this into the CPU time used by the process
 * in question. For the WCPU value, we average the CPU calculations for the
 * process over the last td->cnt updates. This means that the first update
 * when starting top will always be 0% CPU (no big deal), and that WCPU will
 * be averaged over a varying amount of time (also no big deal). This is
 * probably the best we can do, since the kernel doesn't keep any of these
 * statistics itself.
 *
 * This method seems to yield good results. The only problems seem to be the
 * fact that the first update always shows 0%, and that the
 * sysinfo CPU data isn't always in sync with the per-process CPU usage
 * when a CPU-intensive process quits. This latter problem causes funny
 * results, because the remaining processes get credited with the residual
 * CPU time.
 *
 * This algorithm may seem CPU intensive, but it's actually very 
 * inexpensive. The expensive part is the ioctl call to the sar driver.
 * No amount of optimization in this program will reduce the sar overhead.
 */

void
getsysinfo (struct sysinfo *sysinfo)
{
	register int i;
	register int j;
	register int cpus;

	/* Get the per-CPU sysinfo data from sar. */
	if(ioctl(sar, SAR_SYSINFO, &spa)) {
		perror("ioctl(sar, SAR_SYSINFO)");
		quit(24);
	}

	(void)memset((char *)sysinfo, 0, sizeof(struct sysinfo));

	/* Average the state times to get systemwide values. */
	for(i = 0, cpus = 0; i < MAX_LOG_CPU; i++) {
		if(cpu_state[i] != SAR_CPU_RUNNING)
			continue;

		cpus++;

		for(j = 0; j < 5; j++)
			sysinfo->cpu[j] += cpu_sysinfo[i].cpu[j];
	}

	for(i = 0; i < 5; i++)
		sysinfo->cpu[i] /= cpus;
}


void
add_time (struct prpsinfo *pp)
{
	register timedata_t *td;

	td = get_timedata(pp);

	td->flags |= TF_USED;

	if(td->time == -1) {
		td->time = PRTOMS(pp);

		if(!(td->flags & TF_NEWPROC))
			return;

		td->flags &= ~TF_NEWPROC;
		td->ltime = 0;
	}
	else {
		td->ltime = td->time;
		td->time = PRTOMS(pp);
	}

	/* Keep track of the time spent by all processes. */
	total_time += td->time - td->ltime;
}


void
get_cpu(struct prpsinfo *pp, double *cpu, double *wcpu)
{
	register int i;
	register int j;
	register long t;
	register timedata_t *td;

	td = get_timedata(pp);

	/* No history, so return 0%. */
	if(td->ltime == -1) {
		*cpu = 0;
		*wcpu = 0;
		return;
	}

	i = td->index;
	td->index = (i + 1) % MAXTIMEHIST;
	td->cnt = MIN((td->cnt + 1), MAXTIMEHIST);

	/* Compute CPU usage (time diff from last update / total cpu time). */
	/* We don't want to div by 0. */
	if(total_time == 0) {
		td->hist[i] = 0;
		*cpu = 0.0;
	}
	else {
		t = (td->time - td->ltime) * 10000 / total_time * total_cpu;
		td->hist[i] = t;
		*cpu = t / 100.0;
	}

	/* Compute WCPU usage (average CPU % since oldest update). */
	for(j = 0, t = 0; j < td->cnt; j++) {
		t += td->hist[i];

		i--;
		if(i < 0)
			i = MAXTIMEHIST - 1;
	}
	*wcpu = t / j / 100.0;
}


timedata_t *
get_timedata(struct prpsinfo *pp)
{
	register timedata_t *t;
	register timedata_t *l;

	l = TD_HASH(pp->pr_pid);

	for(t = l->hnext; t != l; t = t->hnext)
		if(t->pid == pp->pr_pid)
			return t;

	t = (timedata_t *)malloc(sizeof(timedata_t));
	if(t == 0) {
		perror("malloc");
		quit(25);
	}

	t->pid = pp->pr_pid;
	t->index = 0;
	t->cnt = 0;
	t->time = -1;
	t->ltime = -1;

	if(initted)
		t->flags = TF_USED | TF_NEWPROC;
	else
		t->flags = TF_USED;

	/* Put struct on hash list. */
	t->hnext = l->hnext; 
	t->hlast = l; 
	l->hnext->hlast = t;
	l->hnext = t;

	/* Put struct on timedata list. */
	t->lnext = timedata.lnext; 
	t->llast = &timedata;
	timedata.lnext->llast = t;
	timedata.lnext = t;

	return t;
}


void
clean_timedata(void)
{
	register timedata_t *t;

	for(t = timedata.lnext; t != &timedata; t = t->lnext) {
		if(!(t->flags & TF_USED)) {
			t->hnext->hlast = t->hlast;
			t->hlast->hnext = t->hnext;
			t->lnext->llast = t->llast;
			t->llast->lnext = t->lnext;
			free(t);
		}
		else {
			t->flags &= ~TF_USED;
		}
	}
}
