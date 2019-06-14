/*
 * machine/m_common.c
 *
 * Functionalities common to all the platforms.
 *
 * Copyright (c) 2013 VMware, Inc. All Rights Reserved.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <libpq-fe.h>

#include "machine.h"

/* Query to fetch information about database activity */
#define QUERY_STAT_DB \
		"SELECT datid, datname, numbackends, xact_commit, xact_rollback, \n" \
		"       blks_read, blks_hit, tup_returned, tup_fetched, \n" \
		"       tup_inserted, tup_updated, tup_deleted, conflicts \n" \
		"FROM pg_stat_database;"

char *backendstatenames[] =
{
	"", "idle", "active", "idltxn", "fast", "abort", "disabl", NULL
};

char *procstatenames[] =
{
	"", " idle, ", " active, ", " idle txn, ", " fastpath, ", " aborted, ",
	" disabled, ", NULL
};

/*
 * Get database info via the above QUERY_STAT_DB info.
 * Returns rate info on the various statistics by comparing current
 * values with previous values.
 */
void
get_database_info(struct db_info *db_info, struct pg_conninfo_ctx *conninfo)
{
	struct timeval thistime;
	double		timediff;
	int			i;
	int			rows;
	PGresult   *pgresult = NULL;
	struct db_info cur_info;
	static struct timeval lasttime;
	static struct db_info last_db_info;

	/* calculate the time difference since our last check */
	gettimeofday(&thistime, 0);
	if (lasttime.tv_sec)
		timediff = ((thistime.tv_sec - lasttime.tv_sec) +
					(thistime.tv_usec - lasttime.tv_usec) * 1e-6);
	else
		timediff = 0;

	lasttime = thistime;

	rows = 0;
	connect_to_db(conninfo);
	if (conninfo->connection != NULL)
	{
		pgresult = PQexec(conninfo->connection, QUERY_STAT_DB);
		if (PQresultStatus(pgresult) == PGRES_TUPLES_OK)
			rows = PQntuples(pgresult);

	}
	if (rows == 0)
	{
		/* Database probably stopped, clear current and last */
		memset(&last_db_info, 0, sizeof(last_db_info));
	}
	memset(&cur_info, 0, sizeof(cur_info));
	for (i = 0; i < rows; i++)
	{
		PQgetvalue(pgresult, i, 2);
		/* Count all databases, even with no active backends */
		cur_info.numDb++;
		cur_info.numXact += atoi(PQgetvalue(pgresult, i, 3));
		cur_info.numRollback += atoi(PQgetvalue(pgresult, i, 4));
		cur_info.numBlockRead += atoi(PQgetvalue(pgresult, i, 5));
		cur_info.numBlockHit += atoi(PQgetvalue(pgresult, i, 6));
		cur_info.numTupleFetched += atoi(PQgetvalue(pgresult, i, 8));
		cur_info.numTupleAltered += atoi(PQgetvalue(pgresult, i, 9)) +
			atoi(PQgetvalue(pgresult, i, 10)) +
			atoi(PQgetvalue(pgresult, i, 11));
		cur_info.numConflict += atoi(PQgetvalue(pgresult, i, 12));
	}
	if (pgresult != NULL)
		PQclear(pgresult);
	disconnect_from_db(conninfo);
	if (timediff <= 0)
	{
		last_db_info = cur_info;
		memset(db_info, 0, sizeof(*db_info));
		return;
	}

	/* Compute the rate information */
	db_info->numDb = cur_info.numDb;
	db_info->numXact = (double)(cur_info.numXact - last_db_info.numXact) / timediff;
	db_info->numRollback = (double)(cur_info.numRollback - last_db_info.numRollback) / timediff;
	db_info->numBlockRead = (double)(cur_info.numBlockRead - last_db_info.numBlockRead) / timediff;
	db_info->numBlockHit = (double)(cur_info.numBlockHit - last_db_info.numBlockHit) / timediff;
	db_info->numTupleFetched = (double)(cur_info.numTupleFetched - last_db_info.numTupleFetched) / timediff;
	db_info->numTupleAltered = (double)(cur_info.numTupleAltered - last_db_info.numTupleAltered) / timediff;
	db_info->numConflict = (double)(cur_info.numConflict - last_db_info.numConflict) / timediff;
	last_db_info = cur_info;
}

void
update_state(int *pgstate, char *state)
{
	if (strcmp(state, "idle") == 0)
		*pgstate = STATE_IDLE;
	else if (strcmp(state, "active") == 0)
		*pgstate = STATE_RUNNING;
	else if (strcmp(state, "idle in transaction") == 0)
		*pgstate = STATE_IDLEINTRANSACTION;
	else if (strcmp(state, "fastpath function call") == 0)
		*pgstate = STATE_FASTPATH;
	else if (strcmp(state, "idle in transaction (aborted)") == 0)
		*pgstate = STATE_IDLEINTRANSACTION_ABORTED;
	else if (strcmp(state, "disabled") == 0)
		*pgstate = STATE_DISABLED;
	else
		*pgstate = STATE_UNDEFINED;
}

void
update_str(char **old, char *new)
{
	if (*old == NULL)
		*old = strdup(new);
	else if (strcmp(*old, new) != 0)
	{
		free(*old);
		*old = strdup(new);
	}
}
