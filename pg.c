/*	Copyright (c) 2007-2019, Mark Wong */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "display.h"
#include "pg.h"
#include "pg_top.h"

#define QUERY_PROCESSES \
		"SELECT pid, query, state, usename\n" \
		"FROM pg_stat_activity;"

#define QUERY_PROCESSES_9_1 \
		"SELECT procpid, current_query\n" \
		"FROM pg_stat_activity;"

#define CURRENT_QUERY \
		"SELECT query\n" \
		"FROM pg_stat_activity\n" \
		"WHERE pid = %d;"

#define CURRENT_QUERY_9_1 \
		"SELECT current_query\n" \
		"FROM pg_stat_activity\n" \
		"WHERE procpid = %d;"

#define GET_LOCKS \
		"SELECT datname, relname, mode, granted\n" \
		"FROM pg_stat_activity, pg_locks\n" \
		"LEFT OUTER JOIN pg_class\n" \
		"ON relation = pg_class.oid\n"\
		"WHERE pg_stat_activity.pid = %d\n" \
		"  AND pg_stat_activity.pid = pg_locks.pid;"

#define GET_LOCKS_9_1 \
		"SELECT datname, relname, mode, granted\n" \
		"FROM pg_stat_activity, pg_locks\n" \
		"LEFT OUTER JOIN pg_class\n" \
		"ON relation = pg_class.oid\n"\
		"WHERE procpid = %d\n" \
		"  AND procpid = pid;"

int pg_version(PGconn *);

void
connect_to_db(struct pg_conninfo_ctx *conninfo)
{
	int i;
	const char *keywords[6] = {"host", "port", "user", "password", "dbname",
			NULL};

	if (conninfo->persistent && PQsocket(conninfo->connection) >= 0)
		return;

	conninfo->connection = PQconnectdbParams(keywords, conninfo->values, 1);
	if (PQstatus(conninfo->connection) != CONNECTION_OK)
	{
		new_message(MT_standout | MT_delayed, " %s",
				PQerrorMessage(conninfo->connection));

		PQfinish(conninfo->connection);
		conninfo->connection = NULL;
		return;
	}

	if (conninfo->persistent)
		for (i = 0; i <5; i++)
			if (conninfo->values[i] != NULL)
				free((void *) conninfo->values[i]);
}

void
disconnect_from_db(struct pg_conninfo_ctx *conninfo)
{
	if (conninfo->persistent)
		return;
	PQfinish(conninfo->connection);
}

PGresult *
pg_locks(PGconn *pgconn, int procpid)
{
	char *sql;
	PGresult *pgresult;

	if (pg_version(pgconn) >= 902)
	{
		sql = (char *) malloc(strlen(GET_LOCKS) + 7);
		sprintf(sql, GET_LOCKS, procpid);
	}
	else
	{
		sql = (char *) malloc(strlen(GET_LOCKS) + 7);
		sprintf(sql, GET_LOCKS_9_1, procpid);
	}
	pgresult = PQexec(pgconn, sql);
	free(sql);
	return pgresult;
}

PGresult *
pg_processes(PGconn *pgconn)
{
	PGresult *pgresult;
	PQexec(pgconn, "BEGIN;");
	PQexec(pgconn, "SET statement_timeout = '2s';");
	if (pg_version(pgconn) >= 902)
	{
		pgresult = PQexec(pgconn, QUERY_PROCESSES);
	}
	else
	{
		pgresult = PQexec(pgconn, QUERY_PROCESSES_9_1);
	}
	PQexec(pgconn, "ROLLBACK;;");
	return pgresult;
}

PGresult *
pg_query(PGconn *pgconn, int procpid)
{
	char *sql;
	PGresult *pgresult;

	if (pg_version(pgconn) >= 902)
	{
		sql = (char *) malloc(strlen(CURRENT_QUERY) + 7);
		sprintf(sql, CURRENT_QUERY, procpid);
	}
	else
	{
		sql = (char *) malloc(strlen(CURRENT_QUERY_9_1) + 7);
		sprintf(sql, CURRENT_QUERY_9_1, procpid);
	}
	pgresult = PQexec(pgconn, sql);
	free(sql);

	return pgresult;
}

int
pg_version(PGconn *pgconn)
{
	return PQserverVersion(pgconn) / 100;
}
