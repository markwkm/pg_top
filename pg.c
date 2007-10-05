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
