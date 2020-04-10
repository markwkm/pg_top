/*	Copyright (c) 2007-2019, Mark Wong */

#ifndef _PG_H_
#define _PG_H_

#include <libpq-fe.h>

struct pg_conninfo_ctx
{
	PGconn	   *connection;
	int			persistent;
	const char *values[6];
};

void		connect_to_db(struct pg_conninfo_ctx *);
void		disconnect_from_db(struct pg_conninfo_ctx *);

PGresult   *pg_locks(PGconn *, int);
PGresult   *pg_processes(PGconn *);
PGresult   *pg_replication(PGconn *);
PGresult   *pg_query(PGconn *, int);

enum BackendState
{
	STATE_UNDEFINED,
	STATE_IDLE,
	STATE_RUNNING,
	STATE_IDLEINTRANSACTION,
	STATE_FASTPATH,
	STATE_IDLEINTRANSACTION_ABORTED,
	STATE_DISABLED
};

enum pg_stat_activity
{
	PROC_PID = 0,
	PROC_QUERY,
	PROC_STATE,
	PROC_USENAME,
	PROC_XSTART,
	PROC_QSTART,
	PROC_LOCKS
};

enum pg_stat_replication
{
	REP_PID = 0,
	REP_USENAME,
	REP_APPLICATION_NAME,
	REP_CLIENT_ADDR,
	REP_STATE,
	REP_WAL_INSERT,
	REP_SENT,
	REP_WRITE,
	REP_FLUSH,
	REP_REPLAY,
	REP_SENT_LAG,
	REP_WRITE_LAG,
	REP_FLUSH_LAG,
	REP_REPLAY_LAG
};

#endif							/* _PG_H_ */
