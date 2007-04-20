/*
 * top - a top users display for Unix
 *
 * SYNOPSIS:  Any Sun running SunOS 5.x (Solaris 2.x)
 *
 * DESCRIPTION:
 * This is the machine-dependent module for SunOS 5.x (Solaris 2).
 * There is some support for MP architectures.
 * This makes top work on all revisions of SunOS 5 from 5.0
 * through 5.9 (otherwise known as Solaris 9).  It has not been
 * tested on SunOS 5.10.
 *
 * LIBS: -lelf -lkvm -lkstat
 *
 * CFLAGS: -DHAVE_GETOPT -DORDER -DHAVE_STRERROR -DUSE_SIZE_T
 *
 *
 * AUTHORS:      Torsten Kasch 		<torsten@techfak.uni-bielefeld.de>
 *               Robert Boucher		<boucher@sofkin.ca>
 * CONTRIBUTORS: Marc Cohen 		<marc@aai.com>
 *               Charles Hedrick 	<hedrick@geneva.rutgers.edu>
 *	         William L. Jones 	<jones@chpc>
 *               Petri Kutvonen         <kutvonen@cs.helsinki.fi>
 *	         Casper Dik             <casper.dik@sun.com>
 *               Tim Pugh               <tpugh@oce.orst.edu>
 */

#define _KMEMUSER

#include "config.h"

#if (OSREV == 551)
#undef OSREV
#define OSREV 55
#endif

/* kernels 5.4 and above track pctcpu in the proc structure,
   but the results are less than desirable, so we continue to
   pretend that they don't and just calculate it on our own
*/
#if (OSREV >= 54)
/* #define PROC_HAS_PCTCPU */
#endif

#define USE_NEW_PROC
#if defined(USE_NEW_PROC) && OSREV >= 56
#define _STRUCTURED_PROC 1
#define prpsinfo psinfo
#include <sys/procfs.h>
#define pr_fill pr_nlwp
/* These require an ANSI C compiler "Reisser cpp" doesn't like this */
#define pr_state pr_lwp.pr_state
#define pr_oldpri pr_lwp.pr_oldpri
#define pr_nice pr_lwp.pr_nice
#define pr_pri pr_lwp.pr_pri
#define pr_onpro pr_lwp.pr_onpro
#define ZOMBIE(p)	((p)->pr_nlwp == 0)
#define SIZE_K(p)	(long)((p)->pr_size)
#define RSS_K(p)	(long)((p)->pr_rssize)
#else
#undef USE_NEW_PROC
#define ZOMBIE(p)	((p)->pr_zomb)
#define SIZE_K(p)	(long)((p)->pr_bysize/1024)
#define RSS_K(p)	(long)((p)->pr_byrssize/1024)
#define pr_onpro 	pr_filler[5]
#endif

#include "top.h"
#include "machine.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <nlist.h>
#include <string.h>
#include <kvm.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/fault.h>
#include <sys/sysinfo.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/procfs.h>
#include <sys/vm.h>
#include <sys/var.h>
#include <sys/cpuvar.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/priocntl.h>
#include <sys/tspriocntl.h>
#include <sys/processor.h>
#include <sys/swap.h>
#include <vm/anon.h>
#include <math.h>
#include <utmpx.h>
#include "utils.h"

#if OSREV >= 53
#define USE_KSTAT
#endif
#ifdef USE_KSTAT
#include <kstat.h>
/*
 * Some kstats are fixed at 32 bits, these will be specified as ui32; some
 * are "natural" size (32 bit on 32 bit Solaris, 64 on 64 bit Solaris
 * we'll make those unsigned long)
 * Older Solaris doesn't define KSTAT_DATA_UINT32, those are always 32 bit.
 */
# ifndef KSTAT_DATA_UINT32
#  define ui32 ul
# endif
#endif

#define UNIX "/dev/ksyms"
#define KMEM "/dev/kmem"
#define PROCFS "/proc"
#define CPUSTATES     5
#ifndef PRIO_MIN
#define PRIO_MIN	-20
#endif
#ifndef PRIO_MAX
#define PRIO_MAX	20
#endif

#ifndef FSCALE
#define FSHIFT  8		/* bits to right of fixed binary point */
#define FSCALE  (1<<FSHIFT)
#endif /* FSCALE */

#define loaddouble(la) ((double)(la) / FSCALE)
#define dbl_align(x)	(((unsigned long)(x)+(sizeof(double)-1)) & \
						~(sizeof(double)-1))
#ifdef PROC_HAS_PCTCPU
    /*
     * snarfed from <sys/procfs.h>:
     * The following percent numbers are 16-bit binary
     * fractions [0 .. 1] with the binary point to the
     * right of the high-order bit (one == 0x8000)
     */
#define percent_cpu(pp) (((double)pp->pr_pctcpu)/0x8000*100)
#define weighted_cpu(pp) (*(double *)dbl_align(pp->pr_filler))
#else
#define percent_cpu(pp) (*(double *)dbl_align(&pp->pr_filler[0]))
#define weighted_cpu(pp) (*(double *)dbl_align(&pp->pr_filler[2]))
#endif

/* definitions for indices in the nlist array */
#define X_V			 0
#define X_MPID			 1
#define X_ANONINFO		 2
#define X_MAXMEM		 3
#define X_FREEMEM		 4
#define X_AVENRUN		 5
#define X_CPU			 6
#define X_NPROC			 7
#define X_NCPUS		   	 8

static struct nlist nlst[] =
{
  {"v"},			/* 0 */	/* replaced by dynamic allocation */
  {"mpid"},			/* 1 */
#if OSREV >= 56
  /* this structure really has some extra fields, but the first three match */
  {"k_anoninfo"},		/* 2 */
#else
  {"anoninfo"},			/* 2 */
#endif
  {"maxmem"},			/* 3 */ /* use sysconf */
  {"freemem"},			/* 4 */	/* available from kstat >= 2.5 */
  {"avenrun"},			/* 5 */ /* available from kstat */
  {"cpu"},			/* 6 */ /* available from kstat */
  {"nproc"},			/* 7 */ /* available from kstat */
  {"ncpus"},			/* 8 */ /* available from kstat */
  {0}
};

static unsigned long avenrun_offset;
static unsigned long mpid_offset;
#ifdef USE_KSTAT
static kstat_ctl_t *kc = NULL;
static kid_t kcid = 0;
#else
static unsigned long *cpu_offset;
#endif
static unsigned long nproc_offset;
static unsigned long freemem_offset;
static unsigned long maxmem_offset;
static unsigned long anoninfo_offset;
static void reallocproc(int n);
static int maxprocs = 0;

/* get_process_info passes back a handle.  This is what it looks like: */
struct handle
  {
    struct prpsinfo **next_proc;/* points to next valid proc pointer */
    int remaining;		/* number of pointers remaining */
  };

/*
 * Structure for keeping track of CPU times from last time around
 * the program.  We keep these things in a hash table, which is
 * recreated at every cycle.
 */
struct oldproc
{
    pid_t oldpid;
    double oldtime;
    double oldpct;
};
int oldprocs;			/* size of table */
#define HASH(x) ((x << 1) % oldprocs)

/*
 * GCC assumes that all doubles are aligned.  Unfortunately it
 * doesn't round up the structure size to be a multiple of 8.
 * Thus we'll get a coredump when going through array.  The
 * following is a size rounded up to 8.
 */
#define PRPSINFOSIZE dbl_align(sizeof(struct prpsinfo))

/*
 *  These definitions control the format of the per-process area
 */
#if OSREV >= 58
static char header[] =
"   PID X        LWP PRI NICE  SIZE   RES STATE    TIME    CPU COMMAND";
/* 0123456   -- field to fill in starts at header+6 */
#define UNAME_START 7

#define Proc_format \
        "%6d %-8.8s %3d %3d %4d %5s %5s %-6s %6s %5s%% %s"
#else
static char header[] =
"  PID X        LWP PRI NICE  SIZE   RES STATE    TIME    CPU COMMAND";
/* 0123456   -- field to fill in starts at header+6 */
#define UNAME_START 6

#define Proc_format \
        "%5d %-8.8s %3d %3d %4d %5s %5s %-6s %6s %5s%% %s"
#endif

/* process state names for the "STATE" column of the display */
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
{"idle", "user", "kernel", "iowait", "swap", NULL};
#define CPUSTATE_IOWAIT 3
#define CPUSTATE_SWAP   4


/* these are for detailing the memory statistics */
long memory_stats[5];
char *memorynames[] =
{"K phys mem, ", "K free mem, ", "K total swap, ", "K free swap", NULL};
#define MEMORY_TOTALMEM  0
#define MEMORY_FREEMEM   1
#define MEMORY_TOTALSWAP 2
#define MEMORY_FREESWAP  3

/* these are names given to allowed sorting orders -- first is default */
char *ordernames[] = 
{"cpu", "size", "res", "time", NULL};

/* forward definitions for comparison functions */
int compare_cpu();
int compare_size();
int compare_res();
int compare_time();

int (*proc_compares[])() = {
    compare_cpu,
    compare_size,
    compare_res,
    compare_time,
    NULL };

kvm_t *kd;
static DIR *procdir;
static int nproc;

/* "cpucount" is used to store the value for the kernel variable "ncpus".
   But since <sys/cpuvar.h> actually defines a variable "ncpus" we need
   to use a different name here.   --wnl */
static int cpucount;

/* these are for keeping track of the proc array */
static struct prpsinfo *pbase;
static struct prpsinfo **pref;
static struct oldproc *oldbase;

/* pagetok function is really a pointer to an appropriate function */
static int pageshift;
static long (*p_pagetok) ();
#define pagetok(size) ((*p_pagetok)(size))

/* useful externals */
extern char *myname;
extern int check_nlist ();
extern int gettimeofday ();
extern void perror ();
extern void getptable ();
extern void quit ();
extern int nlist ();

/* p_pagetok points to one of the following, depending on which
   direction data has to be shifted: */

long pagetok_none(long size)

{
    return(size);
}

long pagetok_left(long size)

{
    return(size << pageshift);
}

long pagetok_right(long size)

{
    return(size >> pageshift);
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
getkval (unsigned long offset,
	 int *ptr,
	 int size,
	 char *refstr)
{
    dprintf("getkval(%08x, %08x, %d, %s)\n", offset, ptr, size, refstr);

    if (kvm_read (kd, offset, (char *) ptr, size) != size)
    {
	dprintf("getkval: read failed\n");
	if (*refstr == '!')
	{
	    return (0);
	}
	else
	{
	    fprintf (stderr, "top: kvm_read for %s: %s\n", refstr, strerror(errno));
	    quit (23);
	}
    }

    dprintf("getkval read %d (%08x)\n", *ptr);

    return (1);

}

int
machine_init (struct statics *statics)
{
    struct utmpx ut;
    struct utmpx *up;
    int i;
    char *p;
#ifndef USE_KSTAT
    int offset;
#endif

    /* There's a buffer overflow bug in curses that can be exploited when
       we run as root.  By making sure that TERMINFO is set to something
       this bug is avoided.  This code thanks to Casper */
    if ((p = getenv("TERMINFO")) == NULL || *p == '\0')
    {
        putenv("TERMINFO=/usr/share/lib/terminfo/");
    }

    /* perform the kvm_open - suppress error here */
    if ((kd = kvm_open (NULL, NULL, NULL, O_RDONLY, NULL)) == NULL)
    {
	/* save the error message: we may need it later */
	p = strerror(errno);
    }
    dprintf("kvm_open: fd %d\n", kd);

    /*
     * turn off super group/user privs - but beware; we might
     * want the privs back later and we still have a fd to
     * /dev/kmem open so we can't use setgid()/setuid() as that
     * would allow a debugger to attach to this process. CD
     */
    setegid(getgid());
    seteuid(getuid()); /* super user not needed for NEW_PROC */

#ifdef USE_KSTAT
    if ((kc = kstat_open()) == NULL)
    {
	fprintf(stderr, "Unable to open kstat.\n");
	return(-1);
    }
    kcid = kc->kc_chain_id;
    dprintf("kstat_open: chain %d\n", kcid);
#endif

    /* fill in the statics information */
    statics->procstate_names = procstatenames;
    statics->cpustate_names = cpustatenames;
    statics->memory_names = memorynames;
    statics->order_names = ordernames;
    statics->flags.fullcmds = 1;
    statics->flags.warmup = 1;

    /* get boot time */
    ut.ut_type = BOOT_TIME;
    if ((up = getutxid(&ut)) != NULL)
    {
	statics->boottime = up->ut_tv.tv_sec;
    }
    endutxent();

    /* if the kvm_open succeeded, get the nlist */
    if (kd)
    {
	if (kvm_nlist (kd, nlst) < 0)
        {
	    perror ("kvm_nlist");
	    return (-1);
        }
	if (check_nlist (nlst) != 0)
	    return (-1);
    }
#ifndef USE_KSTAT
    /* if KSTAT is not available to us and we can't open /dev/kmem,
       this is a serious problem.
    */
    else
    {
	/* Print the error message here */
	(void) fprintf(stderr, "kvm_open: %s\n", p);
	return (-1);
    }
#endif

    /* stash away certain offsets for later use */
    mpid_offset = nlst[X_MPID].n_value;
    nproc_offset = nlst[X_NPROC].n_value;
    avenrun_offset = nlst[X_AVENRUN].n_value;
    anoninfo_offset = nlst[X_ANONINFO].n_value;
    freemem_offset = nlst[X_FREEMEM].n_value;
    maxmem_offset = nlst[X_MAXMEM].n_value;


#ifndef USE_KSTAT
    (void) getkval (nlst[X_NCPUS].n_value, (int *) (&cpucount),
		    sizeof (cpucount), "ncpus");

    cpu_offset = (unsigned long *) malloc (cpucount * sizeof (unsigned long));
    for (i = offset = 0; i < cpucount; offset += sizeof(unsigned long)) {
        (void) getkval (nlst[X_CPU].n_value + offset,
                        (int *)(&cpu_offset[i]), sizeof (unsigned long),
                        nlst[X_CPU].n_name );
        if (cpu_offset[i] != 0)
            i++;
    }
#endif

    /* calculate pageshift value */
    i = sysconf(_SC_PAGESIZE);
    pageshift = 0;
    while ((i >>= 1) > 0)
    {
	pageshift++;
    }

    /* calculate an amount to shift to K values */
    /* remember that log base 2 of 1024 is 10 (i.e.: 2^10 = 1024) */
    pageshift -= 10;

    /* now determine which pageshift function is appropriate for the 
       result (have to because x << y is undefined for y < 0) */
    if (pageshift > 0)
    {
	/* this is the most likely */
	p_pagetok = pagetok_left;
    }
    else if (pageshift == 0)
    {
	p_pagetok = pagetok_none;
    }
    else
    {
	p_pagetok = pagetok_right;
	pageshift = -pageshift;
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

    /* all done! */
    return (0);
}

char *
format_header (register char *uname_field)
{
  register char *ptr;

  ptr = header + UNAME_START;
  while (*uname_field != '\0')
    *ptr++ = *uname_field++;

  return (header);
}

#ifdef USE_KSTAT

long
kstat_data_value_l(kstat_named_t *kn)

{
    switch(kn->data_type)
    {
    case KSTAT_DATA_INT32:
	return ((long)(kn->value.i32));
    case KSTAT_DATA_UINT32:
	return ((long)(kn->value.ui32));
    case KSTAT_DATA_INT64:
	return ((long)(kn->value.i64));
    case KSTAT_DATA_UINT64:
	return ((long)(kn->value.ui64));
    }
    return 0;
}

int
kstat_safe_retrieve(kstat_t **ksp,
		    char *module, int instance, char *name, void *buf)

{
    kstat_t *ks;
    kid_t new_kcid;
    int changed;

    dprintf("kstat_safe_retrieve(%08x -> %08x, %s, %d, %s, %08x)\n",
	    ksp, *ksp, module, instance, name, buf);

    ks = *ksp;
    do {
	changed = 0;
	/* if we dont already have the kstat, retrieve it */
	if (ks == NULL)
	{
	    if ((ks = kstat_lookup(kc, module, instance, name)) == NULL)
	    {
		return (-1);
	    }
	    *ksp = ks;
	}

	/* attempt to read it */
	new_kcid = kstat_read(kc, ks, buf);
	/* chance for an infinite loop here if kstat_read keeps 
	   returning -1 */

	/* if the chain changed, update it */
	if (new_kcid != kcid)
	{
	    dprintf("kstat_safe_retrieve: chain changed to %d...updating\n",
		    new_kcid);
	    changed = 1;
	    kcid = kstat_chain_update(kc);
	}
    } while (changed);

    return (0);
}

/*
 * int kstat_safe_namematch(int num, kstat_t *ksp, char *name, void *buf)
 *
 * Safe scan of kstat chain for names starting with "name".  Matches
 * are copied in to "ksp", and kstat_read is called on each match using
 * "buf" as a buffer of length "size".  The actual number of records
 * found is returned.  Up to "num" kstats are copied in to "ksp", but
 * no more.  If any kstat_read indicates that the chain has changed, then
 * the whole process is restarted.
 */

int
kstat_safe_namematch(int num, kstat_t **ksparg, char *name, void *buf, int size)

{
    kstat_t *ks;
    kstat_t **ksp;
    kid_t new_kcid;
    int namelen;
    int count;
    int changed;
    char *cbuf;

    dprintf("kstat_safe_namematch(%d, %08x, %s, %08x, %d)\n",
	    num, ksp, name, buf, size);

    namelen = strlen(name);

    do {
	/* initialize before the scan */
	cbuf = (char *)buf;
	ksp = ksparg;
	count = 0;
	changed = 0;

	/* scan the chain for matching kstats */
	for (ks = kc->kc_chain; ks != NULL; ks = ks->ks_next)
	{
	    if (strncmp(ks->ks_name, name, namelen) == 0)
	    {
		/* this kstat matches: save it if there is room */
		if (count++ < num)
		{
		    /* read the kstat */
		    new_kcid = kstat_read(kc, ks, cbuf);

		    /* if the chain changed, update it */
		    if (new_kcid != kcid)
		    {
			dprintf("kstat_safe_namematch: chain changed to %d...updating\n",
				new_kcid);
			changed = 1;
			kcid = kstat_chain_update(kc);

			/* there's no sense in continuing the scan */
			/* so break out of the for loop */
			break;
		    }

		    /* move to the next buffers */
		    cbuf += size;
		    *ksp++ = ks;
		}
	    }
	}
    } while(changed);

    dprintf("kstat_safe_namematch returns %d\n", count);

    return count;
}

static kstat_t *ks_system_misc = NULL;

#endif /* USE_KSTAT */


int
get_avenrun(int avenrun[3])

{
#ifdef USE_KSTAT
    int status;
    kstat_named_t *kn;

    dprintf("get_avenrun(%08x)\n", avenrun);

    if ((status = kstat_safe_retrieve(&ks_system_misc,
				      "unix", 0, "system_misc", NULL)) == 0)
    {
	if ((kn = kstat_data_lookup(ks_system_misc, "avenrun_1min")) != NULL)
	{
	    avenrun[0] = kn->value.ui32;
	}
	if ((kn = kstat_data_lookup(ks_system_misc, "avenrun_5min")) != NULL)
	{
	    avenrun[1] = kn->value.ui32;
	}
	if ((kn = kstat_data_lookup(ks_system_misc, "avenrun_15min")) != NULL)
	{
	    avenrun[2] = kn->value.ui32;
	}
    }
    dprintf("get_avenrun returns %d\n", status);
    return (status);

#else /* !USE_KSTAT */

    (void) getkval (avenrun_offset, (int *) avenrun, sizeof (int [3]), "avenrun");

    return 0;

#endif /* USE_KSTAT */
}

int
get_ncpus()

{
#ifdef USE_KSTAT
    kstat_named_t *kn;
    int ret = -1;

    if ((kn = kstat_data_lookup(ks_system_misc, "ncpus")) != NULL)
    {
	ret = (int)(kn->value.ui32);
    }

    return ret;
#else
    int ret;

    (void) getkval(nlst[X_NCPUS].n_value, (int *)(&ret), sizeof(ret), "ncpus");
    return ret;
#endif
}

int
get_nproc()

{
#ifdef USE_KSTAT
    kstat_named_t *kn;
    int ret = -1;

    if ((kn = kstat_data_lookup(ks_system_misc, "nproc")) != NULL)
    {
	ret = (int)(kn->value.ui32);
    }

    return ret;
#else
    int ret;

    (void) getkval (nproc_offset, (int *) (&ret), sizeof (ret), "nproc");
    return ret;
#endif
}

unsigned int
(*get_cpustats(int *cnt, unsigned int (*cp_stats)[CPUSTATES]))[CPUSTATES]

{
#ifdef USE_KSTAT
    static kstat_t **cpu_ks = NULL;
    static cpu_stat_t *cpu_stat = NULL;
    static unsigned int nelems = 0;
    cpu_stat_t *cpu_stat_p;
    int i, ret, cpu_num;
    unsigned int (*cp_stats_p)[CPUSTATES];

    dprintf("get_cpustats(%d -> %d, %08x)\n", cnt, *cnt, cp_stats);

    while (nelems > 0 ?
	   (cpu_num = kstat_safe_namematch(nelems,
					   cpu_ks,
					   "cpu_stat",
					   cpu_stat,
					   sizeof(cpu_stat_t))) > nelems :
	   (cpu_num = get_ncpus()) > 0)
    {
	/* reallocate the arrays */
	dprintf("realloc from %d to %d\n", nelems, cpu_num);
	nelems = cpu_num;
	if (cpu_ks != NULL)
	{
	    free(cpu_ks);
	}
	cpu_ks = (kstat_t **)calloc(nelems, sizeof(kstat_t *));
	if (cpu_stat != NULL)
	{
	    free(cpu_stat);
	}
	cpu_stat = (cpu_stat_t *)malloc(nelems * sizeof(cpu_stat_t));
    }

    /* do we have more cpus than our caller? */
    if (cpu_num > *cnt)
    {
	/* yes, so realloc their array, too */
	dprintf("realloc array from %d to %d\n", *cnt, cpu_num);
	*cnt = cpu_num;
	cp_stats = (unsigned int (*)[CPUSTATES])realloc(cp_stats,
			 cpu_num * sizeof(unsigned int) * CPUSTATES);
    }

    cpu_stat_p = cpu_stat;
    cp_stats_p = cp_stats;
    for (i = 0; i < cpu_num; i++)
    {
	dprintf("cpu %d %08x: idle %u, user %u, syscall %u\n", i, cpu_stat_p,
		cpu_stat_p->cpu_sysinfo.cpu[0],
		cpu_stat_p->cpu_sysinfo.cpu[1],
		cpu_stat_p->cpu_sysinfo.syscall);

	(*cp_stats_p)[CPU_IDLE] = cpu_stat_p->cpu_sysinfo.cpu[CPU_IDLE];
	(*cp_stats_p)[CPU_USER] = cpu_stat_p->cpu_sysinfo.cpu[CPU_USER];
	(*cp_stats_p)[CPU_KERNEL] = cpu_stat_p->cpu_sysinfo.cpu[CPU_KERNEL];
	(*cp_stats_p)[CPUSTATE_IOWAIT] = cpu_stat_p->cpu_sysinfo.wait[W_IO] +
	    cpu_stat_p->cpu_sysinfo.wait[W_PIO];
	(*cp_stats_p)[CPUSTATE_SWAP] = cpu_stat_p->cpu_sysinfo.wait[W_SWAP];
	cp_stats_p++;
	cpu_stat_p++;
    }

    cpucount = cpu_num;

    dprintf("get_cpustats sees %d cpus and returns %08x\n", cpucount, cp_stats);

    return (cp_stats);
#else /* !USE_KSTAT */
    int i;
    struct cpu cpu;
    unsigned int (*cp_stats_p)[CPUSTATES];

    /* do we have more cpus than our caller? */
    if (cpucount > *cnt)
    {
	/* yes, so realloc their array, too */
	dprintf("realloc array from %d to %d\n", *cnt, cpucount);
	*cnt = cpucount;
	cp_stats = (unsigned int (*)[CPUSTATES])realloc(cp_stats,
			 cpucount * sizeof(unsigned int) * CPUSTATES);
    }

    cp_stats_p = cp_stats;
    for (i = 0; i < cpucount; i++)
    {
	if (cpu_offset[i] != 0)
	{
	    /* get struct cpu for this processor */
	    (void) getkval (cpu_offset[i], (int *)(&cpu), sizeof (struct cpu), "cpu");

	    (*cp_stats_p)[CPU_IDLE] = cpu.cpu_stat.cpu_sysinfo.cpu[CPU_IDLE];
	    (*cp_stats_p)[CPU_USER] = cpu.cpu_stat.cpu_sysinfo.cpu[CPU_USER];
	    (*cp_stats_p)[CPU_KERNEL] = cpu.cpu_stat.cpu_sysinfo.cpu[CPU_KERNEL];
	    (*cp_stats_p)[CPUSTATE_IOWAIT] = cpu.cpu_stat.cpu_sysinfo.wait[W_IO] +
		cpu.cpu_stat.cpu_sysinfo.wait[W_PIO];
	    (*cp_stats_p)[CPUSTATE_SWAP] = cpu.cpu_stat.cpu_sysinfo.wait[W_SWAP];
	    cp_stats_p++;
	}
    }

    return (cp_stats);
#endif /* USE_KSTAT */
}

/*
 * void get_meminfo(long *total, long *fr)
 *
 * Get information about the system's physical memory.  Pass back values
 * for total available and amount of memory that is free (in kilobytes).
 * It returns 0 on success and -1 on any kind of failure.
 */

int
get_meminfo(long *total, long *fr)

{
    long freemem;
    static kstat_t *ks = NULL;
    kstat_named_t *kn;

    /* total comes from sysconf */
    *total = pagetok(sysconf(_SC_PHYS_PAGES));

    /* free comes from the kernel's freemem or from kstat */
    /* prefer kmem for this because kstat unix:0:system_pages
       can be slow on systems with lots of memory */
    if (kd)
    {
	(void) getkval(freemem_offset, (int *)(&freemem), sizeof(freemem),
		       "freemem");
    }
    else
    {
#ifdef USE_KSTAT
	/* only need to grab kstat chain once */
	if (ks == NULL)
	{
	    ks = kstat_lookup(kc, "unix", 0, "system_pages");
	}

	if (ks != NULL &&
	    kstat_read(kc, ks, 0) != -1 &&
	    (kn = kstat_data_lookup(ks, "freemem")) != NULL)
	{
	    freemem = kstat_data_value_l(kn);
	}
	else
	{
	    freemem = -1;
	}
#else
	freemem = -1;
#endif
    }

    *fr = freemem == -1 ? -1 : pagetok(freemem);

    return (0);
}

/*
 * void get_swapinfo(long *total, long *fr)
 *
 * Get information about the system's swap.  Pass back values for
 * total swap available and amount of swap that is free (in kilobytes).
 * It returns 0 on success and -1 on any kind of failure.
 */

int
get_swapinfo(long *total, long *fr)

{
    register int cnt, i;
    register long t, f;
    struct swaptable *swt;
    struct swapent *ste;
    static char path[256];

    /* preset values to 0 just in case we have to return early */
    *total = 0;
    *fr = 0;

    /* get total number of swap entries */
    if ((cnt = swapctl(SC_GETNSWP, 0)) == -1)
    {
	return (-1);
    }

    /* allocate enough space to hold count + n swapents */
    swt = (struct swaptable *)malloc(sizeof(int) +
				     cnt * sizeof(struct swapent));
    if (swt == NULL)
    {
	return (-1);
    }
    swt->swt_n = cnt;

    /* fill in ste_path pointers: we don't care about the paths, so we point
       them all to the same buffer */
    ste = &(swt->swt_ent[0]);
    i = cnt;
    while (--i >= 0)
    {
	ste++->ste_path = path;
    }

    /* grab all swap info */
    if (swapctl(SC_LIST, swt) == -1)
    {
	return (-1);
    }

    /* walk thru the structs and sum up the fields */
    t = f = 0;
    ste = &(swt->swt_ent[0]);
    i = cnt;
    while (--i >= 0)
    {
	/* dont count slots being deleted */
	if (!(ste->ste_flags & ST_INDEL) &&
	    !(ste->ste_flags & ST_DOINGDEL))
	{
	    t += ste->ste_pages;
	    f += ste->ste_free;
	}
	ste++;
    }

    /* fill in the results */
    *total = pagetok(t);
    *fr = pagetok(f);
    free(swt);

    /* good to go */
    return (0);
}

void
get_system_info (struct system_info *si)
{
    int avenrun[3];
    static long freemem;
    static long maxmem;
    static int swap_total;
    static int swap_free;

    static long cp_time[CPUSTATES];
    static long cp_old[CPUSTATES];
    static long cp_diff[CPUSTATES];
    static unsigned int (*cp_stats)[CPUSTATES] = NULL;
    static int cpus;
    register int j, i;

    /* get important information */
    get_avenrun(avenrun);

    /* get the cpu statistics arrays */
    cp_stats = get_cpustats(&cpus, cp_stats);

    /* zero the cp_time array */
    for (j = 0; j < CPUSTATES; j++)
    {
	cp_time[j] = 0;
    }

    /* sum stats in to a single array */
    for (i = 0; i < cpus; i++)
    {
	for (j = 0; j < CPUSTATES; j++)
	{
	    cp_time[j] += cp_stats[i][j];
	}
    }

    /* convert cp_time counts to percentages */
    (void) percentages (CPUSTATES, cpu_states, cp_time, cp_old, cp_diff);

    /* get mpid -- process id of last process */
    if (kd)
	(void) getkval(mpid_offset, &(si->last_pid), sizeof (si->last_pid), "mpid");
    else
	si->last_pid = -1;

    /* convert load averages to doubles */
    for (i = 0; i < 3; i++)
	si->load_avg[i] = loaddouble (avenrun[i]);

    /* get physical memory data */
    if (get_meminfo(&(memory_stats[MEMORY_TOTALMEM]),
		    &(memory_stats[MEMORY_FREEMEM])) == -1)
    {
	memory_stats[MEMORY_TOTALMEM] = memory_stats[MEMORY_FREEMEM] = -1;
    }

    /* get swap data */
    if (get_swapinfo(&(memory_stats[MEMORY_TOTALSWAP]),
		     &(memory_stats[MEMORY_FREESWAP])) == -1)
    {
	memory_stats[MEMORY_TOTALSWAP] = memory_stats[MEMORY_FREESWAP] = -1;
    }
  
    /* set arrays and strings */
    si->cpustates = cpu_states;
    si->memory = memory_stats;

    dprintf("get_system_info returns\n");
}

static struct handle handle;
static int show_fullcmd;

caddr_t
get_process_info (
		   struct system_info *si,
		   struct process_select *sel,
		   int compare_index)
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

    /* allocate enough space for twice our current needs */
    nproc = get_nproc();
    if (nproc > maxprocs)
    {
	reallocproc(2 * nproc);
    }

    /* read all the proc structures */
    getptable (pbase);

    /* get a pointer to the states summary array */
    si->procstates = process_states;

    /* set up flags which define what we are going to select */
    show_idle = sel->idle;
    show_system = sel->system;
    show_uid = sel->uid != -1;
    show_fullcmd = sel->fullcmd;

    /* count up process states and get pointers to interesting procs */
    total_procs = 0;
    active_procs = 0;
    (void) memset (process_states, 0, sizeof (process_states));
    prefp = pref;

    for (pp = pbase, i = 0; i < nproc;
	 i++, pp = (struct prpsinfo *) ((char *) pp + PRPSINFOSIZE))
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
	    if ((!ZOMBIE(pp)) &&
		(show_idle || percent_cpu (pp) || (pp->pr_state == SRUN) || (pp->pr_state == SONPROC)) &&
		(!show_uid || pp->pr_uid == (uid_t) sel->uid))
	    {
		*prefp++ = pp;
		active_procs++;
	    }
	}
    }

    /* if requested, sort the "interesting" processes */
    qsort ((char *) pref, active_procs, sizeof (struct prpsinfo *),
	   proc_compares[compare_index]);

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
  char sb[10];

  /* find and remember the next proc structure */
  hp = (struct handle *) handle;
  pp = *(hp->next_proc++);
  hp->remaining--;

  /* get the cpu usage and calculate the cpu percentages */
  cputime = pp->pr_time.tv_sec;
  pctcpu = percent_cpu (pp) / cpucount;

  if (pp->pr_state == SONPROC && cpucount > 1)
    sprintf(sb,"cpu/%-2d", pp->pr_onpro); /* XXX large #s may overflow colums */
  else
    *sb = '\0';

  /* format this entry */
#ifdef HAVE_SNPRINTF
  snprintf(fmt, sizeof(fmt),
#else
  sprintf (fmt,
#endif
	   Proc_format,
	   pp->pr_pid,
	   (*get_userid) (pp->pr_uid),
	   (u_short)pp->pr_fill < 999 ? (u_short)pp->pr_fill : 999,
	   pp->pr_pri,
	   pp->pr_nice - NZERO,
	   format_k(SIZE_K(pp)),
	   format_k(RSS_K(pp)),
	   *sb ? sb : state_abbrev[pp->pr_state],
	   format_time(cputime),
	   format_percent(pctcpu),
	   printable(show_fullcmd ? pp->pr_psargs : pp->pr_fname));

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
	    fprintf (stderr, "kernel: no symbol named `%s'\n", nlst->n_name);
	    i = 1;
	}
	nlst++;
    }
    return (i);
}


/* comparison routines for qsort */

/*
 * There are currently four possible comparison routines.  main selects
 * one of these by indexing in to the array proc_compares.
 *
 * Possible keys are defined as macros below.  Currently these keys are
 * defined:  percent cpu, cpu ticks, process state, resident set size,
 * total virtual memory usage.  The process states are ordered as follows
 * (from least to most important):  WAIT, zombie, sleep, stop, start, run.
 * The array declaration below maps a process state index into a number
 * that reflects this ordering.
 */

/* First, the possible comparison keys.  These are defined in such a way
   that they can be merely listed in the source code to define the actual
   desired ordering.
 */

#define ORDERKEY_PCTCPU  if (dresult = percent_cpu (p2) - percent_cpu (p1),\
			     (result = dresult > 0.0 ? 1 : dresult < 0.0 ? -1 : 0) == 0)
#define ORDERKEY_CPTICKS if ((result = p2->pr_time.tv_sec - p1->pr_time.tv_sec) == 0)
#define ORDERKEY_STATE   if ((result = (long) (sorted_state[p2->pr_state] - \
			       sorted_state[p1->pr_state])) == 0)
#define ORDERKEY_PRIO    if ((result = p2->pr_oldpri - p1->pr_oldpri) == 0)
#define ORDERKEY_RSSIZE  if ((result = p2->pr_rssize - p1->pr_rssize) == 0)
#define ORDERKEY_MEM     if ((result = (p2->pr_size - p1->pr_size)) == 0)

/* Now the array that maps process state to a weight */

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


/* compare_cpu - the comparison function for sorting by cpu percentage */

int
compare_cpu (
	       struct prpsinfo **pp1,
	       struct prpsinfo **pp2)
  {
    register struct prpsinfo *p1;
    register struct prpsinfo *p2;
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

/* compare_size - the comparison function for sorting by total memory usage */

int
compare_size (
	       struct prpsinfo **pp1,
	       struct prpsinfo **pp2)
  {
    register struct prpsinfo *p1;
    register struct prpsinfo *p2;
    register long result;
    double dresult;

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
compare_res (
	       struct prpsinfo **pp1,
	       struct prpsinfo **pp2)
  {
    register struct prpsinfo *p1;
    register struct prpsinfo *p2;
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
compare_time (
	       struct prpsinfo **pp1,
	       struct prpsinfo **pp2)
  {
    register struct prpsinfo *p1;
    register struct prpsinfo *p2;
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

/*
get process table
 V.4 only has a linked list of processes so we want to follow that
 linked list, get all the process structures, and put them in our own
 table
*/
void
getptable (struct prpsinfo *baseptr)
{
    struct prpsinfo *currproc;	/* pointer to current proc structure	*/
#ifndef USE_NEW_PROC
    struct prstatus prstatus;     /* for additional information */
#endif
    int numprocs = 0;
    int i;
    struct dirent *direntp;
    struct oldproc *op;
    static struct timeval lasttime =
	{0, 0};
    struct timeval thistime;
    double timediff;
    double alpha, beta;
    struct oldproc *endbase;

    gettimeofday (&thistime, NULL);
    /*
     * To avoid divides, we keep times in nanoseconds.  This is
     * scaled by 1e7 rather than 1e9 so that when we divide we
     * get percent.
     */
    if (lasttime.tv_sec)
	timediff = ((double) thistime.tv_sec * 1.0e7 +
		    ((double) thistime.tv_usec * 10.0)) -
	    ((double) lasttime.tv_sec * 1.0e7 +
	     ((double) lasttime.tv_usec * 10.0));
    else
	timediff = 1.0e7;

    /*
     * constants for exponential average.  avg = alpha * new + beta * avg
     * The goal is 50% decay in 30 sec.  However if the sample period
     * is greater than 30 sec, there's not a lot we can do.
     */
    if (timediff < 30.0e7)
    {
	alpha = 0.5 * (timediff / 30.0e7);
	beta = 1.0 - alpha;
    }
    else
    {
	alpha = 0.5;
	beta = 0.5;
    }

    endbase = oldbase + oldprocs;
    currproc = baseptr;

    /* before reading /proc files, turn on root privs */
    /* (we don't care if this fails since it will be caught later) */
#ifndef USE_NEW_PROC
    seteuid(0);
#endif

    for (rewinddir (procdir); (direntp = readdir (procdir));)
    {
	int fd;
	char buf[30];

	if (direntp->d_name[0] == '.')
	    continue;

#ifdef USE_NEW_PROC
#ifdef HAVE_SNPRINTF
	snprintf(buf, sizeof(buf), "%s/psinfo", direntp->d_name);
#else
	sprintf(buf, "%s/psinfo", direntp->d_name);
#endif

	if ((fd = open (buf, O_RDONLY)) < 0)
	    continue;

	if (read(fd, currproc, sizeof(psinfo_t)) != sizeof(psinfo_t))
	{
	    (void) close (fd);
	    continue;
	}
       
#else
	if ((fd = open (direntp->d_name, O_RDONLY)) < 0)
	    continue;

	if (ioctl (fd, PIOCPSINFO, currproc) < 0)
	{
	    (void) close (fd);
	    continue;
	}

	if (ioctl (fd, PIOCSTATUS, &prstatus) < 0)
	{
	    /* not a show stopper -- just fill in the needed values */
	    currproc->pr_fill = 0;
	    currproc->pr_onpro = 0;
	} else {
	    /* copy over the values we need from prstatus */
	    currproc->pr_fill = (short)prstatus.pr_nlwp;
	    currproc->pr_onpro = prstatus.pr_processor;
	}
#endif

	/*
	 * SVr4 doesn't keep track of CPU% in the kernel, so we have
	 * to do our own.  See if we've heard of this process before.
	 * If so, compute % based on CPU since last time.
	 * NOTE:  Solaris 2.4 and higher do maintain CPU% in prpsinfo.
	 */
	op = oldbase + HASH (currproc->pr_pid);
	while (1)
	{
	    if (op->oldpid == -1)	/* not there */
		break;
	    if (op->oldpid == currproc->pr_pid)
	    {			/* found old data */
#ifndef PROC_HAS_PCTCPU
		percent_cpu (currproc) =
		    ((currproc->pr_time.tv_sec * 1.0e9 +
		      currproc->pr_time.tv_nsec)
		     - op->oldtime) / timediff;
#endif
		weighted_cpu (currproc) =
		    op->oldpct * beta + percent_cpu (currproc) * alpha;

		break;
	    }
	    op++;			/* try next entry in hash table */
	    if (op == endbase)	/* table wrapped around */
		op = oldbase;
	}

	/* Otherwise, it's new, so use all of its CPU time */
	if (op->oldpid == -1)
	{
#ifdef PROC_HAS_PCTCPU
	    weighted_cpu (currproc) =
		percent_cpu (currproc);
#else
	    if (lasttime.tv_sec)
	    {
		percent_cpu (currproc) =
		    (currproc->pr_time.tv_sec * 1.0e9 +
		     currproc->pr_time.tv_nsec) / timediff;
		weighted_cpu (currproc) =
		    percent_cpu (currproc);
	    }
	    else
	    {			/* first screen -- no difference is possible */
		percent_cpu (currproc) = 0.0;
		weighted_cpu (currproc) = 0.0;
	    }
#endif
	}

	numprocs++;
	currproc = (struct prpsinfo *) ((char *) currproc + PRPSINFOSIZE);
	(void) close (fd);

	/* Atypical place for growth */
	if (numprocs >= maxprocs) {
	    reallocproc(2 * numprocs);
	    currproc = (struct prpsinfo *)
		((char *)baseptr + PRPSINFOSIZE * numprocs);
	}
    }

#ifndef USE_NEW_PROC
    /* turn off root privs */
    seteuid(getuid());
#endif

    if (nproc != numprocs)
	nproc = numprocs;

    /*
     * Save current CPU time for next time around
     * For the moment recreate the hash table each time, as the code
     * is easier that way.
     */
    oldprocs = 2 * nproc;
    endbase = oldbase + oldprocs;
    for (op = oldbase; op < endbase; op++)
	op->oldpid = -1;
    for (i = 0, currproc = baseptr;
	 i < nproc;
	 i++, currproc = (struct prpsinfo *) ((char *) currproc + PRPSINFOSIZE))
    {
	/* find an empty spot */
	op = oldbase + HASH (currproc->pr_pid);
	while (1)
	{
	    if (op->oldpid == -1)
		break;
	    op++;
	    if (op == endbase)
		op = oldbase;
	}
	op->oldpid = currproc->pr_pid;
	op->oldtime = (currproc->pr_time.tv_sec * 1.0e9 +
		       currproc->pr_time.tv_nsec);
	op->oldpct = weighted_cpu (currproc);
    }
    lasttime = thistime;
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
proc_owner (int pid)
{
    register struct prpsinfo *p;
    int i;
    for (i = 0, p = pbase; i < nproc;
	 i++, p = (struct prpsinfo *) ((char *) p + PRPSINFOSIZE))
    {
	if (p->pr_pid == (pid_t)pid)
	    return ((int)p->pr_uid);
    }
    return (-1);
}

/* older revisions don't supply a setpriority */
#if (OSREV < 55)
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
#endif


/*
 * When we reach a proc limit, we need to realloc the stuff.
 */
static void reallocproc(int n)
{
    int bytes;
    struct oldproc *op, *endbase;

    if (n < maxprocs)
	return;

    dprintf("reallocproc(%d): reallocating from %d\n", n, maxprocs);

    maxprocs = n;

    /* allocate space for proc structure array and array of pointers */
    bytes = maxprocs * PRPSINFOSIZE;
    pbase = (struct prpsinfo *) realloc(pbase, bytes);
    pref = (struct prpsinfo **) realloc(pref,
					maxprocs * sizeof(struct prpsinfo *));
    oldbase = (struct oldproc *) realloc(oldbase,
					 2 * maxprocs * sizeof(struct oldproc));

    /* Just in case ... */
    if (pbase == (struct prpsinfo *) NULL || pref == (struct prpsinfo **) NULL
	|| oldbase == (struct oldproc *) NULL)
    {
	fprintf (stderr, "%s: can't allocate sufficient memory\n", myname);
	quit(1);
    }

    /*
     * We're growing from 0 to some number, only then we need to
     * init the oldproc stuff
     */
    if (!oldprocs) {
	oldprocs = 2 * maxprocs;

	endbase = oldbase + oldprocs;
	for (op = oldbase; op < endbase; op++)
	    op->oldpid = -1;
    }
}
