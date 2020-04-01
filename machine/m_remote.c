/*
 * Copyright (c) 2008-2009, Mark Wong
 */

#include <stdlib.h>
#ifdef __linux__
#include <bsd/stdlib.h>
#include <bsd/sys/tree.h>
#endif							/* __linux__ */
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <libpq-fe.h>

#include "pg.h"

#include "remote.h"
#include "utils.h"

#define QUERY_CPUTIME \
		"SELECT user, nice, system, idle, iowait\n" \
		"FROM pg_cputime()"

#define QUERY_LOADAVG \
		"SELECT load1, load5, load15, last_pid\n" \
		"FROM pg_loadavg()"

#define QUERY_MEMUSAGE \
		"SELECT memused, memfree, memshared, membuffers, memcached,\n" \
		"       swapused, swapfree, swapcached\n" \
		"FROM pg_memusage()"

#define QUERY_PROCTAB \
		"WITH lock_activity AS\n" \
		"(\n" \
		"     SELECT pid, count(*) AS lock_count\n" \
		"     FROM pg_locks\n" \
		"     WHERE relation IS NOT NULL\n" \
		"     GROUP BY pid\n" \
		")\n" \
		"SELECT a.pid, comm, fullcomm, a.state, utime, stime,\n" \
		"       starttime, vsize, rss, usename, rchar, wchar,\n" \
		"       syscr, syscw, reads, writes, cwrites, b.state,\n" \
		"       extract(EPOCH FROM age(clock_timestamp(),\n" \
		"                              xact_start))::BIGINT,\n" \
		"       extract(EPOCH FROM age(clock_timestamp(),\n" \
		"                              query_start))::BIGINT,\n" \
		"       coalesce(lock_count, 0) AS lock_count\n" \
		"FROM pg_proctab() a LEFT OUTER JOIN pg_stat_activity b\n" \
		"                    ON a.pid = b.pid\n" \
		"     LEFT OUTER JOIN lock_activity c\n" \
		"  ON a.pid = c.pid;"

#define QUERY_PROCTAB_QUERY \
		"WITH lock_activity AS\n" \
		"(\n" \
		"     SELECT pid, count(*) AS lock_count\n" \
		"     FROM pg_locks\n" \
		"     GROUP BY pid\n" \
		")\n" \
		"SELECT a.pid, comm, query, a.state, utime, stime,\n" \
		"       starttime, vsize, rss, usename, rchar, wchar,\n" \
		"       syscr, syscw, reads, writes, cwrites, b.state,\n" \
		"       extract(EPOCH FROM age(clock_timestamp(),\n" \
		"                              xact_start))::BIGINT,\n" \
		"       extract(EPOCH FROM age(clock_timestamp(),\n" \
		"                              query_start))::BIGINT,\n" \
		"       coalesce(lock_count, 0) AS lock_count\n" \
		"FROM pg_proctab() a LEFT OUTER JOIN pg_stat_activity b\n" \
		"                    ON a.pid = b.pid\n" \
		"     LEFT OUTER JOIN lock_activity c\n" \
		"  ON a.pid = c.pid;"

#define QUERY_PG_PROC \
		"SELECT COUNT(*)\n" \
		"FROM pg_catalog.pg_proc\n" \
		"WHERE proname = '%s'"

enum column_cputime
{
	c_cpu_user, c_cpu_nice, c_cpu_system, c_cpu_idle,
	c_cpu_iowait
};
enum column_loadavg
{
	c_load1, c_load5, c_load15, c_last_pid
};
enum column_memusage
{
	c_memused, c_memfree, c_memshared, c_membuffers,
	c_memcached, c_swapused, c_swapfree, c_swapcached
};
enum column_proctab
{
	c_pid, c_comm, c_fullcomm, c_state, c_utime, c_stime,
	c_starttime, c_vsize, c_rss, c_username,
	c_rchar, c_wchar, c_syscr, c_syscw, c_reads, c_writes, c_cwrites,
	c_pgstate, c_xtime, c_qtime, c_locks
};

#define bytetok(x)  (((x) + 512) >> 10)

#define INITIAL_ACTIVE_SIZE (256)
#define PROCBLOCK_SIZE (32)

#define NCPUSTATES 5
#define NMEMSTATS 5
#define NSWAPSTATS 3

#define MEMUSED 0
#define MEMFREE 1
#define MEMSHARED 2
#define MEMBUFFERS 3
#define MEMCACHED 4
#define NMEMSTATS 5

#define SWAPUSED 0
#define SWAPFREE 1
#define SWAPCACHED 2

struct top_proc_r
{
	RB_ENTRY(top_proc_r) entry;
	pid_t		pid;
	char	   *name;
	char	   *usename;
	unsigned long size;
	unsigned long rss;			/* in k */
	int			state;
	int			pgstate;
	unsigned long time;
	unsigned long start_time;
	unsigned long xtime;
	unsigned long qtime;
	unsigned int locks;
	double		pcpu;

	/* The change in the previous values and current values. */
	long long	rchar_diff;
	long long	wchar_diff;
	long long	syscr_diff;
	long long	syscw_diff;
	long long	read_bytes_diff;
	long long	write_bytes_diff;
	long long	cancelled_write_bytes_diff;

	/* The absolute values. */
	long long	rchar;
	long long	wchar;
	long long	syscr;
	long long	syscw;
	long long	read_bytes;
	long long	write_bytes;
	long long	cancelled_write_bytes;

	/* Replication data */
	char	   *application_name;
	char	   *client_addr;
	char	   *repstate;
	char	   *primary;
	char	   *sent;
	char	   *write;
	char	   *flush;
	char	   *replay;
	long long	sent_lag;
	long long	write_lag;
	long long	flush_lag;
	long long	replay_lag;
};

static time_t boottime = -1;
static struct top_proc_r *pgrtable;
static int	proc_r_index;

int			topprocrcmp(struct top_proc_r *, struct top_proc_r *);

RB_HEAD(pgprocr, top_proc_r) head_proc_r = RB_INITIALIZER(&head_proc_r);
RB_PROTOTYPE(pgprocr, top_proc_r, entry, topprocrcmp)
RB_GENERATE(pgprocr, top_proc_r, entry, topprocrcmp)

static char *cpustatenames[NCPUSTATES + 1] =
{
	"user", "nice", "system", "idle", "iowait", NULL
};

static char *memorynames[NMEMSTATS + 1] =
{
	"K used, ", "K free, ", "K shared, ", "K buffers, ", "K cached", NULL
};

/* these are names given to allowed sorting orders -- first is default */
static char *ordernames[] =
{
	"cpu", "size", "res", "xtime", "qtime", "rchar", "wchar", "syscr",
	"syscw", "reads", "writes", "cwrites", "locks", "command", "flag",
	"rlag", "slag", "wlag", NULL
};

static char *swapnames[NSWAPSTATS + 1] =
{
	"K used, ", "K free, ", "K cached", NULL
};

static char fmt_header[] =
"  PID X         SIZE   RES STATE   XTIME  QTIME  %CPU LOCKS COMMAND";

char		fmt_header_io_r[] =
"  PID RCHAR WCHAR   SYSCR   SYSCW READS WRITES CWRITES COMMAND";

char		fmt_header_replication_r[] =
"  PID USERNAME APPLICATION          CLIENT STATE     PRIMARY    SENT       WRITE      FLUSH      REPLAY      SLAG  WLAG  FLAG  RLAG";

/* Now the array that maps process state to a weight. */

unsigned char sort_state_r[] =
{
	0,							/* empty */
	6,							/* run */
	3,							/* sleep */
	5,							/* disk wait */
	1,							/* zombie */
	2,							/* stop */
	4							/* swap */
};

static int64_t cpu_states[NCPUSTATES];
static long memory_stats[NMEMSTATS];
static int	process_states[NPROCSTATES];
static long swap_stats[NSWAPSTATS];

static struct timeval lasttime;

static int64_t cp_time[NCPUSTATES];
static int64_t cp_old[NCPUSTATES];
static int64_t cp_diff[NCPUSTATES];

#define ORDERKEY_PCTCPU  if ((result = (int)(p2->pcpu - p1->pcpu)) == 0)
#define ORDERKEY_STATE	 if ((result = p1->pgstate < p2->pgstate))
#define ORDERKEY_RSSIZE  if ((result = p2->rss - p1->rss) == 0)
#define ORDERKEY_LAG_FLUSH  if ((result = p2->flush_lag - p1->flush_lag) == 0)
#define ORDERKEY_LAG_REPLAY if ((result = p2->replay_lag - \
                                          p1->replay_lag) == 0)
#define ORDERKEY_LAG_SENT   if ((result = p2->sent_lag - p1->sent_lag) == 0)
#define ORDERKEY_LAG_WRITE  if ((result = p2->write_lag - p1->write_lag) == 0)
#define ORDERKEY_MEM	 if ((result = p2->size - p1->size) == 0)
#define ORDERKEY_NAME	if ((result = strcmp(p1->name, p2->name)) == 0)
#define ORDERKEY_RCHAR	 if ((result = p1->rchar - p2->rchar) == 0)
#define ORDERKEY_WCHAR	 if ((result = p1->wchar - p2->wchar) == 0)
#define ORDERKEY_SYSCR	 if ((result = p1->syscr - p2->syscr) == 0)
#define ORDERKEY_SYSCW	 if ((result = p1->syscw - p2->syscw) == 0)
#define ORDERKEY_READS	 if ((result = p1->read_bytes - p2->read_bytes) == 0)
#define ORDERKEY_WRITES	 if ((result = p1->write_bytes - p2->write_bytes) == 0)
#define ORDERKEY_CWRITES if ((result = p1->cancelled_write_bytes - p2->cancelled_write_bytes) == 0)
#define ORDERKEY_XTIME if ((result = p2->xtime - p1->xtime) == 0)
#define ORDERKEY_QTIME if ((result = p2->qtime - p1->qtime) == 0)
#define ORDERKEY_LOCKS if ((result = p2->locks - p1->locks) == 0)

int			check_for_function(PGconn *, char *);
static int	compare_cmd_r(const void *, const void *);
static int	compare_cpu_r(const void *, const void *);
static int	compare_cwrites_r(const void *, const void *);
static int	compare_lag_flush(const void *, const void *);
static int	compare_lag_replay(const void *, const void *);
static int	compare_lag_sent(const void *, const void *);
static int	compare_lag_write(const void *, const void *);
static int	compare_locks_r(const void *, const void *);
static int	compare_qtime_r(const void *, const void *);
static int	compare_rchar_r(const void *, const void *);
static int	compare_reads_r(const void *, const void *);
static int	compare_res_r(const void *, const void *);
static int	compare_size_r(const void *, const void *);
static int	compare_syscr_r(const void *, const void *);
static int	compare_syscw_r(const void *, const void *);
static int	compare_wchar_r(const void *, const void *);
static int	compare_writes_r(const void *, const void *);
static int	compare_xtime_r(const void *, const void *);

int
check_for_function(PGconn *pgconn, char *procname)
{
	PGresult   *pgresult = NULL;
	int			rows = 0;
	int			count;
	char		sql[128];

	sprintf(sql, QUERY_PG_PROC, procname);
	pgresult = PQexec(pgconn, sql);
	rows = PQntuples(pgresult);
	/* Don't need to clean up on error, the program will exit shortly after. */
	if (rows == 0)
	{
		fprintf(stderr, "Error executing '%s'.\n", sql);
		return -1;
	}
	count = atoi(PQgetvalue(pgresult, 0, 0));
	if (count == 0)
	{
		fprintf(stderr, "Stored function '%s' is missing.\n", procname);
		return -1;
	}
	if (pgresult != NULL)
		PQclear(pgresult);
	return 0;
}

int			(*proc_compares_r[]) () =
{
	compare_cpu_r,
		compare_size_r,
		compare_res_r,
		compare_xtime_r,
		compare_qtime_r,
		compare_rchar_r,
		compare_wchar_r,
		compare_syscr_r,
		compare_syscw_r,
		compare_reads_r,
		compare_writes_r,
		compare_cwrites_r,
		compare_locks_r,
		compare_cmd_r,
		compare_lag_flush,
		compare_lag_replay,
		compare_lag_sent,
		compare_lag_write,
		NULL
};

/* The comparison function for sorting by command name. */

static int
compare_cmd_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_NAME
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		ORDERKEY_RSSIZE
		ORDERKEY_MEM
		;

	return (result);
}

/* compare_cpu_r - the comparison function for sorting by cpu percentage */

static int
compare_cpu_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_PCTCPU
		ORDERKEY_STATE
		ORDERKEY_RSSIZE
		ORDERKEY_MEM
		;

	return (result);
}

static int
compare_cwrites_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_CWRITES
		ORDERKEY_RCHAR
		ORDERKEY_WCHAR
		ORDERKEY_SYSCR
		ORDERKEY_SYSCW
		ORDERKEY_READS
		ORDERKEY_WRITES
		ORDERKEY_NAME
		;

	return (result);
}

static int
compare_lag_flush(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_LAG_FLUSH
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		ORDERKEY_MEM
		ORDERKEY_RSSIZE
		;

	return (result);
}

static int
compare_lag_replay(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_LAG_REPLAY
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		ORDERKEY_MEM
		ORDERKEY_RSSIZE
		;

	return (result);
}

static int
compare_lag_sent(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_LAG_SENT
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		ORDERKEY_MEM
		ORDERKEY_RSSIZE
		;

	return (result);
}

static int
compare_lag_write(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_LAG_WRITE
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		ORDERKEY_MEM
		ORDERKEY_RSSIZE
		;

	return (result);
}

/*
 * compare_locks_r - the comparison function for sorting by total locks
 * acquired
 */

int
compare_locks_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_LOCKS
		ORDERKEY_QTIME
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		ORDERKEY_MEM
		ORDERKEY_RSSIZE
		;

	return (result);
}

/* compare_qtime_r - the comparison function for sorting by total cpu qtime */

static int
compare_qtime_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_QTIME
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		ORDERKEY_MEM
		ORDERKEY_RSSIZE
		;

	return (result);
}

/* The comparison function for sorting by resident set size. */

static int
compare_res_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_RSSIZE
		ORDERKEY_MEM
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		;

	return (result);
}

/* The comparison function for sorting by total memory usage. */

static int
compare_rchar_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_RCHAR
		ORDERKEY_WCHAR
		ORDERKEY_SYSCR
		ORDERKEY_SYSCW
		ORDERKEY_READS
		ORDERKEY_WRITES
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return (result);
}

static int
compare_reads_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_READS
		ORDERKEY_RCHAR
		ORDERKEY_WCHAR
		ORDERKEY_SYSCR
		ORDERKEY_SYSCW
		ORDERKEY_WRITES
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return (result);
}

static int
compare_size_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_MEM
		ORDERKEY_RSSIZE
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		;

	return (result);
}

static int
compare_syscr_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_SYSCR
		ORDERKEY_RCHAR
		ORDERKEY_WCHAR
		ORDERKEY_SYSCW
		ORDERKEY_READS
		ORDERKEY_WRITES
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return (result);
}

static int
compare_syscw_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_SYSCW
		ORDERKEY_RCHAR
		ORDERKEY_WCHAR
		ORDERKEY_SYSCR
		ORDERKEY_READS
		ORDERKEY_WRITES
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return (result);
}

/* compare_xtime_r - the comparison function for sorting by total cpu xtime */

static int
compare_xtime_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_XTIME
		ORDERKEY_PCTCPU
		ORDERKEY_STATE
		ORDERKEY_MEM
		ORDERKEY_RSSIZE
		;

	return (result);
}

static int
compare_wchar_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_WCHAR
		ORDERKEY_RCHAR
		ORDERKEY_SYSCR
		ORDERKEY_SYSCW
		ORDERKEY_READS
		ORDERKEY_WRITES
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return (result);
}

static int
compare_writes_r(const void *v1, const void *v2)
{
	struct top_proc_r *p1 = (struct top_proc_r *) v1;
	struct top_proc_r *p2 = (struct top_proc_r *) v2;
	int			result;

	ORDERKEY_WRITES
		ORDERKEY_RCHAR
		ORDERKEY_WCHAR
		ORDERKEY_SYSCR
		ORDERKEY_SYSCW
		ORDERKEY_READS
		ORDERKEY_CWRITES
		ORDERKEY_NAME
		;

	return (result);
}

char *
format_header_r(char *uname_field)
{
	int			uname_len = strlen(uname_field);

	if (uname_len > 8)
		uname_len = 8;
	memcpy(strchr(fmt_header, 'X'), uname_field, uname_len);
	return fmt_header;
}

char *
format_next_io_r(caddr_t handler)
{
	static char fmt[MAX_COLS];	/* static area where result is built */
	struct top_proc_r *p = &pgrtable[proc_r_index++];

	if (mode_stats == STATS_DIFF)
		snprintf(fmt, sizeof(fmt),
				 "%5d %5s %5s %7lld %7lld %5s %6s %7s %s",
				 (int) p->pid,
				 format_b(p->rchar_diff),
				 format_b(p->wchar_diff),
				 p->syscr_diff,
				 p->syscw_diff,
				 format_b(p->read_bytes_diff),
				 format_b(p->write_bytes_diff),
				 format_b(p->cancelled_write_bytes_diff),
				 p->name);
	else
		snprintf(fmt, sizeof(fmt),
				 "%5d %5s %5s %7lld %7lld %5s %6s %7s %s",
				 (int) p->pid,
				 format_b(p->rchar),
				 format_b(p->wchar),
				 p->syscr,
				 p->syscw,
				 format_b(p->read_bytes),
				 format_b(p->write_bytes),
				 format_b(p->cancelled_write_bytes),
				 p->name);

	return (fmt);
}

char *
format_next_process_r(caddr_t handler)
{
	static char fmt[MAX_COLS];	/* static area where result is built */
	struct top_proc_r *p = &pgrtable[proc_r_index++];

	snprintf(fmt, sizeof(fmt),
			 "%5d %-8.8s %5s %5s %-6s %5s %5s %5.1f %5d %s",
			 (int) p->pid,		/* Some OS's need to cast pid_t to int. */
			 p->usename,
			 format_k(p->size),
			 format_k(p->rss),
			 backendstatenames[p->pgstate],
			 format_time(p->xtime),
			 format_time(p->qtime),
			 p->pcpu * 100.0,
			 p->locks,
			 p->name);

	return (fmt);
}

char *
format_next_replication_r(caddr_t handle)
{
	static char fmt[MAX_COLS];	/* static area where result is built */
	struct top_proc_r *p = &pgrtable[proc_r_index++];

	snprintf(fmt, sizeof(fmt),
			 "%5d %-8.8s %-11.11s %15s %-9.9s %9s %9s %9s %9s %9s %5s %5s %5s %5s",
			 p->pid,
			 p->usename,
			 p->application_name,
			 p->client_addr,
			 p->repstate,
			 p->primary,
			 p->sent,
			 p->write,
			 p->flush,
			 p->replay,
			 format_b(p->sent_lag),
			 format_b(p->write_lag),
			 format_b(p->flush_lag),
			 format_b(p->replay_lag));

	/* return the result */
	return (fmt);
}

void
get_system_info_r(struct system_info *info, struct pg_conninfo_ctx *conninfo)
{
	PGresult   *pgresult = NULL;
	int			rows = 0;

	connect_to_db(conninfo);
	if (conninfo->connection != NULL)
	{
		pgresult = PQexec(conninfo->connection, QUERY_LOADAVG);
		rows = PQntuples(pgresult);
	}

	/* Get load averages. */
	if (rows > 0)
	{
		info->load_avg[0] = atof(PQgetvalue(pgresult, 0, c_load1));
		info->load_avg[1] = atof(PQgetvalue(pgresult, 0, c_load5));
		info->load_avg[2] = atof(PQgetvalue(pgresult, 0, c_load15));
		info->last_pid = atoi(PQgetvalue(pgresult, 0, c_last_pid));
	}
	else
	{
		info->load_avg[0] = 0;
		info->load_avg[1] = 0;
		info->load_avg[2] = 0;
		info->last_pid = 0;
	}

	/* Get processor time info. */
	if (conninfo->connection != NULL)
	{
		pgresult = PQexec(conninfo->connection, QUERY_CPUTIME);
		rows = PQntuples(pgresult);
	}
	if (rows > 0)
	{
		cp_time[0] = atol(PQgetvalue(pgresult, 0, c_cpu_user));
		cp_time[1] = atol(PQgetvalue(pgresult, 0, c_cpu_nice));
		cp_time[2] = atol(PQgetvalue(pgresult, 0, c_cpu_system));
		cp_time[3] = atol(PQgetvalue(pgresult, 0, c_cpu_idle));
		cp_time[4] = atol(PQgetvalue(pgresult, 0, c_cpu_iowait));

		/* convert cp_time counts to percentages */
		percentages(NCPUSTATES, cpu_states, cp_time, cp_old, cp_diff);
	}
	else
	{
		cpu_states[0] = 0;
		cpu_states[1] = 0;
		cpu_states[2] = 0;
		cpu_states[3] = 0;
		cpu_states[4] = 0;
	}

	/* Get system wide memory usage. */
	if (conninfo->connection != NULL)
	{
		pgresult = PQexec(conninfo->connection, QUERY_MEMUSAGE);
		rows = PQntuples(pgresult);
	}
	if (rows > 0)
	{
		memory_stats[MEMUSED] = atol(PQgetvalue(pgresult, 0, c_memused));
		memory_stats[MEMFREE] = atol(PQgetvalue(pgresult, 0, c_memfree));
		memory_stats[MEMSHARED] = atol(PQgetvalue(pgresult, 0, c_memshared));
		memory_stats[MEMBUFFERS] = atol(PQgetvalue(pgresult, 0, c_membuffers));
		memory_stats[MEMCACHED] = atol(PQgetvalue(pgresult, 0, c_memcached));
		swap_stats[SWAPUSED] = atol(PQgetvalue(pgresult, 0, c_swapused));
		swap_stats[SWAPFREE] = atol(PQgetvalue(pgresult, 0, c_swapfree));
		swap_stats[SWAPCACHED] = atol(PQgetvalue(pgresult, 0, c_swapcached));
	}
	else
	{
		memory_stats[MEMUSED] = 0;
		memory_stats[MEMFREE] = 0;
		memory_stats[MEMSHARED] = 0;
		memory_stats[MEMBUFFERS] = 0;
		memory_stats[MEMCACHED] = 0;
		swap_stats[SWAPUSED] = 0;
		swap_stats[SWAPFREE] = 0;
		swap_stats[SWAPCACHED] = 0;
	}

	info->cpustates = cpu_states;
	info->memory = memory_stats;
	info->swap.swap = swap_stats;

	if (pgresult != NULL)
		PQclear(pgresult);
	disconnect_from_db(conninfo);
}

caddr_t
get_process_info_r(struct system_info *si, struct process_select *sel,
				   int compare_index, struct pg_conninfo_ctx *conninfo, int mode)
{
	int			i;

	PGresult   *pgresult = NULL;
	int			rows;

	struct timeval thistime;
	double		timediff;

	int			active_procs = 0;
	int			total_procs = 0;

	int			show_idle = sel->idle;

	struct top_proc_r *n,
			   *p;

	memset(process_states, 0, sizeof(process_states));

	/* Calculate the time difference since our last check. */
	gettimeofday(&thistime, 0);
	if (lasttime.tv_sec)
	{
		timediff = ((thistime.tv_sec - lasttime.tv_sec) +
					(thistime.tv_usec - lasttime.tv_usec) * 1e-6);
	}
	else
	{
		timediff = 0;
	}
	lasttime = thistime;

	timediff *= HZ;				/* Convert to ticks. */

	connect_to_db(conninfo);
	if (conninfo->connection != NULL)
	{
		switch (mode)
		{
			case MODE_REPLICATION:
				pgresult = pg_replication(conninfo->connection);
				break;
			default:
				if (sel->fullcmd == 2)
				{
					pgresult = PQexec(conninfo->connection, QUERY_PROCTAB_QUERY);
				}
				else
				{
					pgresult = PQexec(conninfo->connection, QUERY_PROCTAB);
				}
		}
		rows = PQntuples(pgresult);
	}
	else
	{
		rows = 0;
	}

	if (rows > 0)
	{
		p = reallocarray(pgrtable, rows, sizeof(struct top_proc_r));
		if (p == NULL)
		{
			fprintf(stderr, "reallocarray error\n");
			if (pgresult != NULL)
				PQclear(pgresult);
			disconnect_from_db(conninfo);
			exit(1);
		}
		pgrtable = p;
	}

	for (i = 0; i < rows; i++)
	{
		unsigned long otime;
		long long	value;

		n = malloc(sizeof(struct top_proc_r));
		if (n == NULL)
		{
			fprintf(stderr, "malloc error\n");
			if (pgresult != NULL)
				PQclear(pgresult);
			disconnect_from_db(conninfo);
			exit(1);
		}
		memset(n, 0, sizeof(struct top_proc_r));
		n->pid = atoi(PQgetvalue(pgresult, i, c_pid));
		p = RB_INSERT(pgprocr, &head_proc_r, n);
		if (p != NULL)
		{
			free(n);
			n = p;
		}
		else
		{
			n->time = 0;
		}

		otime = n->time;

		switch (mode)
		{
			case MODE_REPLICATION:
				update_str(&n->usename, PQgetvalue(pgresult, i, 1));
				update_str(&n->application_name, PQgetvalue(pgresult, i, 2));
				update_str(&n->client_addr, PQgetvalue(pgresult, i, 3));
				update_str(&n->repstate, PQgetvalue(pgresult, i, 4));
				update_str(&n->primary, PQgetvalue(pgresult, i, 5));
				update_str(&n->sent, PQgetvalue(pgresult, i, 6));
				update_str(&n->write, PQgetvalue(pgresult, i, 7));
				update_str(&n->flush, PQgetvalue(pgresult, i, 8));
				update_str(&n->replay, PQgetvalue(pgresult, i, 9));
				n->sent_lag = atol(PQgetvalue(pgresult, i, 10));
				n->write_lag = atol(PQgetvalue(pgresult, i, 11));
				n->flush_lag = atol(PQgetvalue(pgresult, i, 12));
				n->replay_lag = atol(PQgetvalue(pgresult, i, 13));

				memcpy(&pgrtable[active_procs++], n, sizeof(struct top_proc_r));
				break;
			default:
				if (sel->fullcmd && PQgetvalue(pgresult, i, c_fullcomm))
					update_str(&n->name, PQgetvalue(pgresult, i, c_fullcomm));
				else
					update_str(&n->name, PQgetvalue(pgresult, i, c_comm));

				switch (PQgetvalue(pgresult, i, c_state)[0])
				{
					case 'R':
						n->state = 1;
						break;
					case 'S':
						n->state = 2;
						break;
					case 'D':
						n->state = 3;
						break;
					case 'Z':
						n->state = 4;
						break;
					case 'T':
						n->state = 5;
						break;
					case 'W':
						n->state = 6;
						break;
					case '\0':
						continue;
				}
				update_state(&n->pgstate, PQgetvalue(pgresult, i, c_pgstate));

				n->time = (unsigned long) atol(PQgetvalue(pgresult, i, c_utime));
				n->time += (unsigned long) atol(PQgetvalue(pgresult, i, c_stime));
				n->start_time = (unsigned long)
					atol(PQgetvalue(pgresult, i, c_starttime));
				n->size = bytetok((unsigned long)
								  atol(PQgetvalue(pgresult, i, c_vsize)));
				n->rss = bytetok((unsigned long)
								 atol(PQgetvalue(pgresult, i, c_rss)));

				update_str(&n->usename, PQgetvalue(pgresult, i, c_username));

				n->xtime = atol(PQgetvalue(pgresult, i, c_xtime));
				n->qtime = atol(PQgetvalue(pgresult, i, c_qtime));

				n->locks = atol(PQgetvalue(pgresult, i, c_locks));

				value = atoll(PQgetvalue(pgresult, i, c_rchar));
				n->rchar_diff = value - n->rchar;
				n->rchar = value;

				value = atoll(PQgetvalue(pgresult, i, c_wchar));
				n->wchar_diff = value - n->wchar;
				n->wchar = value;

				value = atoll(PQgetvalue(pgresult, i, c_syscr));
				n->syscr_diff = value - n->syscr;
				n->syscr = value;

				value = atoll(PQgetvalue(pgresult, i, c_syscw));
				n->syscw_diff = value - n->syscw;
				n->syscw = value;

				value = atoll(PQgetvalue(pgresult, i, c_reads));
				n->read_bytes_diff = value - n->read_bytes;
				n->read_bytes = value;

				value = atoll(PQgetvalue(pgresult, i, c_writes));
				n->write_bytes_diff = value - n->write_bytes;
				n->write_bytes = value;

				value = atoll(PQgetvalue(pgresult, i, c_cwrites));
				n->cancelled_write_bytes_diff = value - n->cancelled_write_bytes;
				n->cancelled_write_bytes = value;

				++total_procs;
				++process_states[n->pgstate];

				if (timediff > 0.0)
				{
					if ((n->pcpu = (n->time - otime) / timediff) < 0.0001)
						n->pcpu = 0;
				}

				if ((show_idle || n->pgstate != STATE_IDLE) &&
					(sel->usename[0] == '\0' ||
					 strcmp(n->usename, sel->usename) == 0))
					memcpy(&pgrtable[active_procs++], n, sizeof(struct top_proc_r));
		}
	}

	if (pgresult != NULL)
		PQclear(pgresult);
	disconnect_from_db(conninfo);

	si->p_active = active_procs;
	si->p_total = total_procs;
	si->procstates = process_states;

	/* Sort the "active" procs if specified. */
	if (compare_index >= 0 && si->p_active)
		qsort(pgrtable, si->p_active, sizeof(struct top_proc_r),
			  proc_compares_r[compare_index]);

	/* don't even pretend that the return value thing here isn't bogus */
	proc_r_index = 0;
	return 0;
}

int
machine_init_r(struct statics *statics, struct pg_conninfo_ctx *conninfo)
{
	/* Make sure the remote system has the stored function installed. */
	connect_to_db(conninfo);
	if (conninfo->connection == NULL)
	{
		fprintf(stderr, "Cannot connect to database.\n");
		return -1;
	}

	if (check_for_function(conninfo->connection, "pg_cputime") != 0)
		return -1;
	if (check_for_function(conninfo->connection, "pg_loadavg") != 0)
		return -1;
	if (check_for_function(conninfo->connection, "pg_memusage") != 0)
		return -1;
	if (check_for_function(conninfo->connection, "pg_proctab") != 0)
		return -1;
	disconnect_from_db(conninfo);

	/* fill in the statics information */
	statics->procstate_names = procstatenames;
	statics->cpustate_names = cpustatenames;
	statics->memory_names = memorynames;
	statics->swap_names = swapnames;
	statics->order_names = ordernames;
	statics->boottime = boottime;
	statics->flags.fullcmds = 1;
	statics->flags.warmup = 1;

	return 0;
}

int
topprocrcmp(struct top_proc_r *e1, struct top_proc_r *e2)
{
	return (e1->pid < e2->pid ? -1 : e1->pid > e2->pid);
}
