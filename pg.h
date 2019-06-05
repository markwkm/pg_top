/*	Copyright (c) 2007-2019, Mark Wong */

#ifndef _PG_H_
#define _PG_H_

#include <libpq-fe.h>

PGconn	   *connect_to_db(const char **);

PGresult   *pg_locks(PGconn *, int);
PGresult   *pg_processes(PGconn *);
PGresult   *pg_query(PGconn *, int);

#endif   /* _PG_H_ */
