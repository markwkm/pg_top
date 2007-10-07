/*  Copyright (c) 2007, Mark Wong */

#ifndef _PG_H_
#define _PG_H_

#include <libpq-fe.h>

#define SELECT_INDEX_STATS \
		"SELECT indexrelname, idx_scan, idx_tup_read, idx_tup_fetch\n" \
		"FROM pg_stat_user_indexes\n" \
		"ORDER BY indexrelname"

#define SELECT_TABLE_STATS \
		"SELECT relname, seq_scan, seq_tup_read, idx_scan, idx_tup_fetch, " \
		"       n_tup_ins, n_tup_upd, n_tup_del\n" \
		"FROM pg_stat_user_tables\n" \
		"ORDER BY relname"

PGconn *connect_to_db(char *);
void pg_display_index_stats(char *);
void pg_display_table_stats(char *);

#endif /* _PG_H_ */
