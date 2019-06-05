/*	Copyright (c) 2007-2019, Mark Wong */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "display.h"
#include "pg.h"
#include "pg_top.h"

#define QUERY_PROCESSES \
		"SELECT pid, query\n" \
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

float pg_version(PGconn *);

PGconn *
connect_to_db(const char *values[])
{
	PGconn	   *pgconn = NULL;
	const char *keywords[6] = {"host", "port", "user", "password", "dbname",
			NULL};

	pgconn = PQconnectdbParams(keywords, values, 1);
	if (PQstatus(pgconn) != CONNECTION_OK)
	{
		new_message(MT_standout | MT_delayed, " %s", PQerrorMessage(pgconn));

		PQfinish(pgconn);
		return NULL;
	}
	return pgconn;
}

PGresult *
pg_locks(PGconn *pgconn, int procpid)
{
	char *sql;
	PGresult *pgresult;

	if (pg_version(pgconn) >= 9.2)
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
	if (pg_version(pgconn) >= 9.2)
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

	if (pg_version(pgconn) >= 9.2)
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

/* Query the version string and just return the major.minor as a float. */
float
pg_version(PGconn *pgconn)
{
	PGresult *pgresult = NULL;

	char *version_string;
	float version;

	pgresult = PQexec(pgconn, "SHOW server_version;");
	version_string = PQgetvalue(pgresult, 0, 0);
	sscanf(version_string, "%f%*s", &version);
	/* Deal with rounding problems by adding 0.01. */
	version += 0.01;
	PQclear(pgresult);

	return version;
}
