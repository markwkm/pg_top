/* 
 * This is a stripped-down and hacked version of task.h from NextStep 2.1
 * from the Informer.app by Max Tardiveau.
 *
 * tpugh 2/14/1996
 * I've modify this task structure to account some unknown variables in
 * NeXT's new (unpublished) task structure, so I can get to the utask structure.
 * tmp1[3] is in the right place, but tmp2[3] may not be in the right place.
 * So do not be surprised if any other variable in the structure, except utask,
 * is incorrectly aligned.
 *
 */


#import <mach/boolean.h>
#import <mach/port.h>
#import <mach/time_value.h>
#import <kernserv/lock.h>
#import <kernserv/queue.h>
#import <mach/mach_param.h>
#import <mach/mach_types.h>

struct task {
#ifdef NEXTSTEP40
	int tmp1[3];
#endif
	/* Synchronization/destruction information */
	char lock[4];
	int		ref_count;	/* Number of references to me */
	boolean_t	active;		/* Task has not been terminated */

	/* Miscellaneous */
	char	map[4];		/* Address space description */
	queue_chain_t	pset_tasks;	/* list of tasks assigned to pset */
	int		suspend_count;	/* Internal scheduling only */

	/* Thread information */
	queue_head_t	thread_list;	/* list of threads */
	int		thread_count;	/* number of threads */
	char thread_list_lock[4]; /* XXX thread_list lock */
	processor_set_t	processor_set;	/* processor set for new threads */
#ifdef NEXTSTEP40
	int tmp2[3];
#endif
	boolean_t	may_assign;	/* can assigned pset be changed? */
	boolean_t	assign_active;	/* waiting for may_assign */

	/* Garbage */
	struct utask	*u_address;
#if	NeXT
	struct proc	*proc;		/* corresponding process */
#else	NeXT
	int		proc_index;	/* corresponding process, by index */
#endif	NeXT

	/* User-visible scheduling information */
	int		user_stop_count;	/* outstanding stops */
	int		priority;		/* for new threads */

	/* Information for kernel-internal tasks */
#if	NeXT
	boolean_t	kernel_privilege; /* Is a kernel task */
#endif	NeXT
	boolean_t	kernel_ipc_space; /* Uses kernel's port names? */
	boolean_t	kernel_vm_space; /* Uses kernel's pmap? */

	/* Statistics */
	time_value_t	total_user_time;
				/* total user time for dead threads */
	time_value_t	total_system_time;
				/* total system time for dead threads */

	/* Special ports */
	port_t		task_self;	/* Port representing the task */
	port_t		task_tself;	/* What the task thinks is task_self */
	port_t		task_notify;	/* Where notifications get sent */
	port_t		exception_port;	/* Where exceptions are sent */
	port_t		bootstrap_port;	/* Port passed on for task startup */

	/* IPC structures */
	boolean_t	ipc_privilege;	/* Can use kernel resource pools? */
	char ipc_translation_lock[4];
	queue_head_t	ipc_translations; /* Per-task port naming */
	boolean_t	ipc_active;	/* Can IPC rights be added? */
	port_name_t	ipc_next_name;	/* Next local name to use */
#if	MACH_IPC_XXXHACK
	kern_set_t	ipc_enabled;	/* Port set for PORT_ENABLED */
#endif	MACH_IPC_XXXHACK

#if	MACH_IPC_TCACHE
#define OBJ_CACHE_MAX		010	/* Number of cache lines */
#define OBJ_CACHE_MASK		007	/* Mask for name->line */

	struct {
		port_name_t	name;
		kern_obj_t	object;
	}		obj_cache[OBJ_CACHE_MAX];
					/* Fast object translation cache */
#endif	MACH_IPC_TCACHE

	/* IPC compatibility garbage */
	boolean_t	ipc_intr_msg;	/* Send signal upon message arrival? */
#define TASK_PORT_REGISTER_MAX 4 	/* Number of "registered" ports */
	port_t		ipc_ports_registered[TASK_PORT_REGISTER_MAX];
};
