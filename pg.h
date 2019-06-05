/*	Copyright (c) 2007-2019, Mark Wong */

#ifndef _PG_H_
#define _PG_H_

#include <libpq-fe.h>

#define CHECK_FOR_STATEMENTS_X \
		"SELECT 1\n" \
		"FROM pg_extension\n" \
		"WHERE extname = 'pg_stat_statements'"

#define SELECT_STATEMENTS \
		"WITH aggs AS (\n" \
		"    SELECT sum(calls) AS calls_total\n" \
		"    FROM pg_stat_statements\n" \
		")\n" \
		"SELECT calls,\n" \
		"       calls / calls_total AS calls_percentage,\n" \
		"       to_char(INTERVAL '1 milliseconds' * total_time,\n" \
		"               'HH24:MI:SS.MS'),\n" \
		"       to_char(INTERVAL '1 milliseconds' * (total_time / calls),\n" \
		"               'HH24:MI:SS.MS') AS average_time,\n" \
		"       regexp_replace(query, E'[\\n\\r]+', ' ', 'g' ) AS query\n" \
		"FROM pg_stat_statements, aggs\n" \
		"ORDER BY %d ASC"

PGconn	   *connect_to_db(const char **);

int			pg_display_statements(const char **, int, int);

PGresult   *pg_locks(PGconn *, int);
PGresult   *pg_processes(PGconn *);
PGresult   *pg_query(PGconn *, int);

extern char *statement_ordernames[];

#endif   /* _PG_H_ */
