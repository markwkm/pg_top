/*  Copyright (c) 2007, Mark Wong */

#ifndef _PG_H_
#define _PG_H_

#include <libpq-fe.h>

#define SELECT_INDEX_STATS \
		"SELECT indexrelid, indexrelname, idx_scan, idx_tup_read,\n" \
		"       idx_tup_fetch\n" \
		"FROM pg_stat_user_indexes\n" \
		"ORDER BY indexrelname"

#define SELECT_TABLE_STATS \
		"SELECT relname, seq_scan, seq_tup_read, idx_scan, idx_tup_fetch, " \
		"       n_tup_ins, n_tup_upd, n_tup_del\n" \
		"FROM pg_stat_user_tables\n" \
		"ORDER BY relname"

int compare_idx_scan(const void *, const void *);
int compare_idx_tup_fetch(const void *, const void *);
int compare_idx_tup_read(const void *, const void *);

PGconn *connect_to_db(char *);

void pg_display_index_stats(char *, int, int);
void pg_display_table_stats(char *);

extern char *index_ordernames[];

#endif /* _PG_H_ */
