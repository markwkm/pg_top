/*  Copyright (c) 2007, Mark Wong */

#include "display.h"
#include "pg.h"

PGconn *connect_to_db(char *conninfo)
{
	PGconn *pgconn = NULL;

	pgconn = PQconnectdb(conninfo);
	if (PQstatus(pgconn) != CONNECTION_OK) {
		/* FIXME: Figure out how to properly display this. */
		new_message(MT_standout, " Could not connect to PostgreSQL...");
		putchar('\r');
		fflush(stdout);

		PQfinish(pgconn);
		return NULL;
	}
	return pgconn;
}
