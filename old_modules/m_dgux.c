/*
 * top - a top users display for Unix
 *
 * SYNOPSIS:  for DG AViiON with DG/UX 5.4+
 *
 * DESCRIPTION:
 * A top module for DG/UX 5.4 systems.
 * Uses DG/UX system calls to get info from the kernel.
 * (NB. top DOES NOT need to be installed setuid root under DG/UX 5.4.2)
 *
 * AUTHOR:  Mike Williams <mike@inform.co.nz>
 */

/*
 * NOTE: This module will only work with top versions 3.1 and later!
 */

#include <stdlib.h> 
#include <unistd.h>
#include <stdio.h>

#include <sys/dg_sys_info.h>
#include <sys/dg_process_info.h>
#include <sys/systeminfo.h> 
#include <sys/sysmacros.h> 

#include "top.h"
#include "machine.h"
#include "utils.h"

/*--- process formatting --------------------------------------------------*/

static char header[] =
  "  PID X         PRI NICE C    SIZE STATE    TIME    CPU COMMAND";
/* ddddd ssssssss dddd ddd dd ddddddK ssssssdddd:dd dd.dd% sssssssssssssssss...
 * 0123456   -- field to fill in starts at header+6 */
#define UNAME_START 6

#define Proc_format \
     "%5d %-8.8s %4d %3d %2d %7s %-6s %6s %5.2f%% %.20s"

/*--- process states ------------------------------------------------------*/

static char* procStateNames[] = {
    "", " sleeping, ", " waiting, ", " running, ", " starting, ",
    " zombie, ", " stopped, ",
    NULL
};

static char* procStateAbbrevs[] = {
    "", "sleep", "wait", "run", "start", "zombie", "stop",
    NULL
};

#define N_PROCESS_STATES \
(sizeof (procStateNames) / sizeof (procStateNames[0]) - 1)

static int processStats[N_PROCESS_STATES];

/*--- cpu states ----------------------------------------------------------*/

enum {
    CPU_user,
    CPU_system,
    CPU_idle,
    CPU_io_wait,
};

static char* cpuStateNames[] = {
    "user", "system", "idle", "io_wait",
    NULL
};

#define N_CPU_STATES \
(sizeof (cpuStateNames) / sizeof (cpuStateNames[0]) - 1)

static int cpuStats[N_CPU_STATES];

/*--- memory statistics ---------------------------------------------------*/

enum {
    MEM_available,
    MEM_used,
    MEM_free,
    MEM_freeswap,
};

static char* memoryNames[] = {
    "K physical, ", "K in use, ", "K free, ", "K free swap, ", NULL
};

#define N_MEMORY_STATS \
(sizeof (memoryNames) / sizeof (memoryNames[0]) - 1)

static int memoryStats[N_MEMORY_STATS];

/*--- conversion macros ---------------------------------------------------*/

/* Convert clicks (kernel pages) to kbytes ... */
#define pagetok(size) ctob(size) >> LOG1024

/* Convert timeval's to double */
#define tvtod(tval) (1000000.0 * (tval).tv_sec + 1.0 * (tval).tv_usec)

/* Scale timeval's onto longs */
#define scaledtv(tval) (tvtod (tval) / 4096) 

/*--- process table -------------------------------------------------------*/

typedef struct _ProcInfo {
    struct dg_process_info    p_info;
    double 		      cpu_time;
    double 		      fraction_cpu;
} ProcInfo;

static ProcInfo* 	      processInfo;
static ProcInfo** 	      activeProcessInfo;

int 			      activeIndex;

typedef struct _ProcTime {
    pid_t 		      pid;
    double 		      cpu_time;
} ProcTime;

static ProcTime* 	      oldProcessTimes;
static int 		      n_oldProcessTimes;

static double 		      lastTime;
static double 		      thisTime;
static double 		      timeSlice;

/*=========================================================================*/
/*=== top "Callback" routines =============================================*/

static int IntCmp (i1, i2)
  int* 			i1;
  int* 			i2;
{
    return (*i2 - *i1);
}

/*=== Data collection =====================================================*/

int machine_init (statics)
  /*~~~~~~~~~~~~
   */
  struct statics *statics;
{
    struct dg_sys_info_pm_info pm_info;
    int 		      table_size;

    /* fill in the statics information */
    statics->procstate_names = procStateNames;
    statics->cpustate_names = cpuStateNames;
    statics->memory_names = memoryNames;

    dg_sys_info ((long *)&pm_info,
		 DG_SYS_INFO_PM_INFO_TYPE,
		 DG_SYS_INFO_PM_VERSION_0);
    table_size = pm_info.process_table_size + 1;

    processInfo = (ProcInfo *) 
	malloc (sizeof (processInfo[0]) * table_size);
    activeProcessInfo = (ProcInfo **) 
	malloc (sizeof (activeProcessInfo[0]) * table_size);
    oldProcessTimes = (ProcTime *) 
	malloc (sizeof (oldProcessTimes[0]) * table_size);

    lastTime = 0;

    return(0);
}

int get_system_info (si)
  /*~~~~~~~~~~~~~~~
   */
  struct system_info *si;
{
    struct dg_sys_info_vm_info    vm_info;
    struct dg_sys_info_pm_info    pm_info;
    struct dg_sys_info_load_info  load_info;

    static long cpu_time [N_CPU_STATES];
    static long cpu_old [N_CPU_STATES];
    static long cpu_diff [N_CPU_STATES];

    /* memory info */
    
    dg_sys_info ((long *)&vm_info,
		 DG_SYS_INFO_VM_INFO_TYPE,
		 DG_SYS_INFO_VM_VERSION_0);

    memoryStats[MEM_available] = sysconf (_SC_AVAILMEM);
    memoryStats[MEM_free]      = pagetok (vm_info.freemem);
    memoryStats[MEM_used]      = memoryStats[0] - memoryStats[2];
    memoryStats[MEM_freeswap]  = pagetok (vm_info.freeswap);
    si->memory 		       = memoryStats;

    /* process info */
    
    dg_sys_info ((long *)&pm_info,
		 DG_SYS_INFO_PM_INFO_TYPE,
		 DG_SYS_INFO_PM_VERSION_0);

    si->last_pid 	      = 0;
    si->p_total 	      = pm_info.process_count;
    si->p_active 	      = pm_info.bound_runnable_process_count;

    cpu_time[CPU_user]        = scaledtv (pm_info.user_time);
    cpu_time[CPU_system]      = scaledtv (pm_info.system_time);
    cpu_time[CPU_idle] 	      = scaledtv (pm_info.idle_time);
    cpu_time[CPU_io_wait]     = scaledtv (pm_info.io_wait_time);
    percentages (N_CPU_STATES, cpuStats, cpu_time, cpu_old, cpu_diff);
    si->cpustates 	      = cpuStats;

    /* calculate timescale */

    thisTime = tvtod (pm_info.current_time);
    timeSlice = thisTime - lastTime;
    lastTime = thisTime;
    
    /* load info */
    
    dg_sys_info ((long *)&load_info,
		 DG_SYS_INFO_LOAD_INFO_TYPE,
		 DG_SYS_INFO_LOAD_VERSION_0);

    si->load_avg[0] 	= load_info.one_minute;
    si->load_avg[1] 	= load_info.five_minute;
    si->load_avg[2] 	= load_info.fifteen_minute;

    return 1;
}

caddr_t get_process_info (si, sel, compare)
  /*    ~~~~~~~~~~~~~~~~ 
   */
  struct system_info* 	si;
  struct process_select* sel;
  int 			(*compare)();
{
    long 		key = DG_PROCESS_INFO_INITIAL_KEY;
 			
    int 		n_total = 0;
    int 		n_active = 0;

    ProcInfo* 		pp;
    int 		i;

    bzero((char *)processStats, sizeof(processStats));

    while (dg_process_info (DG_PROCESS_INFO_SELECTOR_ALL_PROCESSES, 0,
			    DG_PROCESS_INFO_CMD_NAME_ONLY,
			    &key,
			    &(processInfo[n_total].p_info),
			    DG_PROCESS_INFO_CURRENT_VERSION) == 1) {

	ProcInfo*       pp = &(processInfo[n_total++]);
	int 		pid = pp->p_info.process_id;
	ProcTime* 	old_time;

	/* Increment count for this process state */
	++processStats[pp->p_info.state];

	/* Calculate % CPU usage */
	pp->cpu_time = (tvtod (pp->p_info.system_time) + 
			tvtod (pp->p_info.user_time));
	old_time = (ProcTime *) 
	    bsearch (&pid, oldProcessTimes, 
		     n_oldProcessTimes, sizeof (ProcTime),
		     IntCmp);
	pp->fraction_cpu = (old_time 
			    ? ((pp->cpu_time - old_time->cpu_time)
			       / timeSlice) 
			    : 0.0);

	/* Skip if process not classed as "active" */
	if ((pp->p_info.state == DG_PROCESS_INFO_STATUS_TERMINATED) ||
	    (!sel->idle 
	     && (pp->p_info.state != DG_PROCESS_INFO_STATUS_RUNNING)
	     && (pp->p_info.state != DG_PROCESS_INFO_STATUS_WAITING)) ||
	    (sel->uid != -1 && pp->p_info.user_id != (uid_t)sel->uid) ||
	    (!sel->system && (pp->p_info.user_id == 0 &&
			     pp->p_info.parent_process_id == 1)) ||
	    (sel->command && strcmp (pp->p_info.cmd, sel->command) != 0))
	    continue;

	activeProcessInfo[n_active++] = pp;
	
    }

    activeProcessInfo[n_active] = NULL;

    si->p_total 	= n_total;
    si->p_active 	= n_active;
    si->procstates 	= processStats;

    /* If requested, sort the "interesting" processes */
    if (compare != NULL) qsort((void *)activeProcessInfo, 
			       n_active, 
			       sizeof (ProcInfo *), 
			       compare);

    /* Record scaled CPU totals, for calculating %CPU */
    n_oldProcessTimes = n_total;
    for (i = 0; i < n_oldProcessTimes; i++) {
	oldProcessTimes[i].pid = processInfo[i].p_info.process_id;
	oldProcessTimes[i].cpu_time = processInfo[i].cpu_time;
    }
    qsort (oldProcessTimes, n_oldProcessTimes, sizeof (ProcTime), IntCmp);

    /* pass back a handle */
    activeIndex = 0;
    return ((caddr_t) &activeIndex);
}

/*=== Process comparison routine ==========================================*/

/*
 * Sort keys are (in descending order of importance):
 *     - percent cpu
 *     - cpu ticks
 *     - state
 *     - resident set size
 *     
 * The process states are ordered as follows:
 *     - zombie
 *     - wait
 *     - sleep
 *     - stop
 *     - start
 *     - run
 */

static unsigned char sortedState[] =
{
    0,	                                /* not used */
    3,	                                /* sleep */
    1,	                                /* wait	*/
    6,	                                /* run */
    5,	                                /* start */
    2,	                                /* zombie */
    4,	                                /* stop */
};

int proc_compare(pp1, pp2)
  /*~~~~~~~~~~~~
   */
  ProcInfo** 		pp1;
  ProcInfo** 		pp2;
{
    register ProcInfo* 	p1;
    register ProcInfo* 	p2;
    register int 	result;
    register float 	lresult;

    register long 	p1_cpu;
    register long 	p2_cpu;

    /* remove one level of indirection */
    p1 = *pp1;
    p2 = *pp2;

    /* calculate cpu totals */
    p1_cpu = p1->p_info.system_time.tv_sec + p1->p_info.user_time.tv_sec;
    p2_cpu = p2->p_info.system_time.tv_sec + p2->p_info.user_time.tv_sec;

    /* Compare %CPU usage */
    if ((lresult = (p2->fraction_cpu - p1->fraction_cpu)) != 0)
	return lresult < 0 ? -1 : 1;

    /* Compare other fields until one differs */
    ((result = (p2->p_info.cpu_usage - p1->p_info.cpu_usage)) ||
     (result = (sortedState [p2->p_info.state] - 
		sortedState [p1->p_info.state])) ||
     (result = (p2->p_info.priority - p1->p_info.priority)) ||
     (result = (p2->p_info.resident_process_size - 
		p1->p_info.resident_process_size)) ||
     (result = (p1->p_info.process_id - p2->p_info.process_id)));

    return result;
}

/*=== Process owner validation ============================================*/

/*
 * proc_owner(pid) - returns the uid that owns process "pid", or -1 if
 *		the process does not exist.
 *		It is EXTREMLY IMPORTANT that this function work correctly.
 *		If top runs setuid root (as in SVR4), then this function
 *		is the only thing that stands in the way of a serious
 *		security problem.  It validates requests for the "kill"
 *		and "renice" commands.
 */

int proc_owner (pid)
  /*~~~~~~~~~~
   */
  int pid;
{
    register int      i;
    ProcInfo* 	      pp;

    for (i = 0; (pp = activeProcessInfo [i]); i++) {
	if (pp->p_info.process_id == pid) 
	    return (int)pp->p_info.user_id;
    }
    return(-1);
}

/*=== Output formatting ===================================================*/

char* format_header (uname_field)
  /*  ~~~~~~~~~~~~~
   */
  register char* 	uname_field;
{
    register char* 	ptr;

    ptr = header + UNAME_START;
    while (*uname_field != '\0')
    {
	*ptr++ = *uname_field++;
    }

    return(header);
}

char* format_next_process (index_ptr, get_userid)
  /*  ~~~~~~~~~~~~~~~~~~~
   */
  int* 			index_ptr;
  char* 		(*get_userid)();
{
    static char 	fmt[MAX_COLS];

    int 		proc_index;
    ProcInfo* 		pp;
    long 		proc_cpu;

    proc_index = (*index_ptr)++;
    pp = activeProcessInfo [proc_index];
    proc_cpu = pp->p_info.system_time.tv_sec + pp->p_info.user_time.tv_sec;

    /* format this entry */

    sprintf (fmt,
	     Proc_format,
	     pp->p_info.process_id,
	     (*get_userid) (pp->p_info.user_id),
	     pp->p_info.priority,
	     pp->p_info.nice_value,
	     pp->p_info.cpu_usage,
	     format_k(pagetok (pp->p_info.resident_process_size)),
	     procStateAbbrevs[pp->p_info.state],
	     format_time(proc_cpu),
	     100.0 * pp->fraction_cpu,
	     pp->p_info.cmd);
    
    return(fmt);
}

/*=== END of m_dgux.c =====================================================*/
