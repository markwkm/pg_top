/*  Copyright (c) 2007, Mark Wong */

#ifndef _PG_H_
#define _PG_H_

#include <libpq-fe.h>

PGconn *connect_to_db(char *);

#endif /* _PG_H_ */
