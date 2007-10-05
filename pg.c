/*  Copyright (c) 2007, Mark Wong */

#include "display.h"
#include "pg.h"

PGconn *connect_to_db(char *conninfo)
{
	static int refresh = 0;
	PGconn *pgconn = NULL;

	pgconn = PQconnectdb(conninfo);
	if (PQstatus(pgconn) != CONNECTION_OK) {
		refresh = 1;
		new_message(MT_standout | MT_delayed,
				" Could not connect to PostgreSQL...");

		PQfinish(pgconn);
		return NULL;
	} else {
		/*
		 * FIXME: I don't know how expensive this is but I don't know how to
		 * get the header text to redisplay when it gets wipe out by the
		 * above's susequent new_message() calls.  The number of running
		 * processes seems to printed a litle funny when it is 0 too.
		 */
		if (refresh == 1) {
			reset_display();
			refresh = 0;
		}
	}
	return pgconn;
}

void pg_display_table_stats(char *conninfo)
{
	int i;
	int rows;
	PGconn *pgconn;
	PGresult *pgresult = NULL;
	static char line[512];

	/* Get the currently running query. */
	pgconn = connect_to_db(conninfo);
	if (pgconn != NULL) {
		pgresult = PQexec(pgconn, SELECT_TABLE_STATS);
		rows = PQntuples(pgresult);
	} else {
		PQfinish(pgconn);
		return;
	}

	for (i = 0; i < rows; i++) {
		snprintf(line, sizeof(line), "%9s %9s %9s %9s %9s %9s %9s %-9s",
				PQgetvalue(pgresult, i, 1),
				PQgetvalue(pgresult, i, 2),
				PQgetvalue(pgresult, i, 3),
				PQgetvalue(pgresult, i, 4),
				PQgetvalue(pgresult, i, 5),
				PQgetvalue(pgresult, i, 6),
				PQgetvalue(pgresult, i, 7),
				PQgetvalue(pgresult, i, 0));
		u_process(i, line);
	}

	if (pgresult != NULL) 
		PQclear(pgresult);
	PQfinish(pgconn);
}
