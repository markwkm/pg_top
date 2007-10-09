/*  Copyright (c) 2007, Mark Wong */

#include <stdlib.h>

#include "display.h"
#include "pg.h"

char *index_ordernames[] = {
		"idx_scan", "idx_tup_fetch", "idx_tup_read", NULL
};

char *table_ordernames[] = {
		"seq_scan", "seq_tup_read", "idx_scan", "idx_tup_fetch", "n_tup_ins",
		"n_tup_upd", "n_tup_del", NULL
};

int (*table_compares[])() = {
		compare_seq_scan,
		compare_seq_tup_read,
		compare_idx_scan_t,
		compare_idx_tup_fetch_t,
		compare_n_tup_ins,
		compare_n_tup_upd,
		compare_n_tup_del,
		NULL
};

int (*index_compares[])() = {
		compare_idx_scan,
		compare_idx_tup_fetch,
		compare_idx_tup_read,
		NULL
};

struct index_node {
	long long indexrelid;

	/* Index to the index name in the PGresult object. */
	int name_index;

	/* The change in the previous values and current values. */
	long long diff_idx_scan;
	long long diff_idx_tup_read;
	long long diff_idx_tup_fetch;

	/* The previous values. */
	long long old_idx_scan;
	long long old_idx_tup_read;
	long long old_idx_tup_fetch;

	struct index_node *next;
};

struct table_node {
	long long relid;

	/* Index to the relation name in the PGresult object. */
	int name_index;

	/* The change in the previous values and current values. */
	long long diff_idx_scan;
	long long diff_idx_tup_fetch;
	long long diff_n_tup_del;
	long long diff_n_tup_ins;
	long long diff_n_tup_upd;
	long long diff_seq_scan;
	long long diff_seq_tup_read;

	/* The previous values. */
	long long old_idx_scan;
	long long old_idx_tup_fetch;
	long long old_n_tup_del;
	long long old_n_tup_ins;
	long long old_n_tup_upd;
	long long old_seq_scan;
	long long old_seq_tup_read;

	struct table_node *next;
};

struct index_node *get_index_stats(struct index_node *, long long);
struct index_node *insert_index_stats(struct index_node *, struct index_node *);
struct index_node *new_index_node(long long);
void update_index_stats(struct index_node *, long long, long long, long long);
struct index_node *upsert_index_stats(struct index_node *, long long,
		long long, long long, long long);

struct table_node *get_table_stats(struct table_node *, long long);
struct table_node *insert_table_stats(struct table_node *, struct table_node *);
struct table_node *new_table_node(long long);
void update_table_stats(struct table_node *, long long, long long, long long,
		long long, long long, long long, long long);
struct table_node *upsert_table_stats(struct table_node *, long long,
		long long, long long, long long, long long, long long, long long,
		long long);

int compare_idx_scan(const void *vp1, const void *vp2)
{
	struct index_node **pp1 = (struct index_node **) vp1;
	struct index_node **pp2 = (struct index_node **) vp2;
	struct index_node *p1 = *pp1;
	struct index_node *p2 = *pp2;

	if (p1->diff_idx_scan < p2->diff_idx_scan) {
		return -1;
	} else if (p1->diff_idx_scan > p2->diff_idx_scan) {
		return 1;
	} else {
		return 0;
	}
}

int compare_idx_scan_t(const void *vp1, const void *vp2)
{
	struct table_node **pp1 = (struct table_node **) vp1;
	struct table_node **pp2 = (struct table_node **) vp2;
	struct table_node *p1 = *pp1;
	struct table_node *p2 = *pp2;

	if (p1->diff_idx_scan < p2->diff_idx_scan) {
		return -1;
	} else if (p1->diff_idx_scan > p2->diff_idx_scan) {
		return 1;
	} else {
		return 0;
	}
}

int compare_idx_tup_fetch(const void *vp1, const void *vp2)
{
	struct index_node **pp1 = (struct index_node **) vp1;
	struct index_node **pp2 = (struct index_node **) vp2;
	struct index_node *p1 = *pp1;
	struct index_node *p2 = *pp2;

	if (p1->diff_idx_tup_fetch < p2->diff_idx_tup_fetch) {
		return -1;
	} else if (p1->diff_idx_tup_fetch > p2->diff_idx_tup_fetch) {
		return 1;
	} else {
		return 0;
	}
}

int compare_idx_tup_fetch_t(const void *vp1, const void *vp2)
{
	struct table_node **pp1 = (struct table_node **) vp1;
	struct table_node **pp2 = (struct table_node **) vp2;
	struct table_node *p1 = *pp1;
	struct table_node *p2 = *pp2;

	if (p1->diff_idx_tup_fetch < p2->diff_idx_tup_fetch) {
		return -1;
	} else if (p1->diff_idx_tup_fetch > p2->diff_idx_tup_fetch) {
		return 1;
	} else {
		return 0;
	}
}

int compare_idx_tup_read(const void *vp1, const void *vp2)
{
	struct index_node **pp1 = (struct index_node **) vp1;
	struct index_node **pp2 = (struct index_node **) vp2;
	struct index_node *p1 = *pp1;
	struct index_node *p2 = *pp2;

	if (p1->diff_idx_tup_read < p2->diff_idx_tup_read) {
		return -1;
	} else if (p1->diff_idx_tup_read > p2->diff_idx_tup_read) {
		return 1;
	} else {
		return 0;
	}
}

int compare_n_tup_del(const void *vp1, const void *vp2)
{
	struct table_node **pp1 = (struct table_node **) vp1;
	struct table_node **pp2 = (struct table_node **) vp2;
	struct table_node *p1 = *pp1;
	struct table_node *p2 = *pp2;

	if (p1->diff_n_tup_del < p2->diff_n_tup_del) {
		return -1;
	} else if (p1->diff_n_tup_del > p2->diff_n_tup_del) {
		return 1;
	}
	return 0;
}

int compare_n_tup_ins(const void *vp1, const void *vp2)
{
	struct table_node **pp1 = (struct table_node **) vp1;
	struct table_node **pp2 = (struct table_node **) vp2;
	struct table_node *p1 = *pp1;
	struct table_node *p2 = *pp2;

	if (p1->diff_n_tup_ins < p2->diff_n_tup_ins) {
		return -1;
	} else if (p1->diff_n_tup_ins > p2->diff_n_tup_ins) {
		return 1;
	}
	return 0;
}

int compare_n_tup_upd(const void *vp1, const void *vp2)
{
	struct table_node **pp1 = (struct table_node **) vp1;
	struct table_node **pp2 = (struct table_node **) vp2;
	struct table_node *p1 = *pp1;
	struct table_node *p2 = *pp2;

	if (p1->diff_n_tup_upd < p2->diff_n_tup_upd) {
		return -1;
	} else if (p1->diff_n_tup_upd > p2->diff_n_tup_upd) {
		return 1;
	}
	return 0;
}

int compare_seq_scan(const void *vp1, const void *vp2)
{
	struct table_node **pp1 = (struct table_node **) vp1;
	struct table_node **pp2 = (struct table_node **) vp2;
	struct table_node *p1 = *pp1;
	struct table_node *p2 = *pp2;

	if (p1->diff_seq_scan < p2->diff_seq_scan) {
		return -1;
	} else if (p1->diff_seq_scan > p2->diff_seq_scan) {
		return 1;
	}
	return 0;
}

int compare_seq_tup_read(const void *vp1, const void *vp2)
{
	struct table_node **pp1 = (struct table_node **) vp1;
	struct table_node **pp2 = (struct table_node **) vp2;
	struct table_node *p1 = *pp1;
	struct table_node *p2 = *pp2;

	if (p1->diff_seq_tup_read < p2->diff_seq_tup_read) {
		return -1;
	} else if (p1->diff_seq_tup_read > p2->diff_seq_tup_read) {
		return 1;
	}
	return 0;
}

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

struct index_node *get_index_stats(struct index_node *head,
		long long indexrelid)
{
	struct index_node *c = head;

	while (c != NULL) {
		if (c->indexrelid == indexrelid) {
			break;
		}
		c = c->next;
	}
	return c;
}

struct table_node *get_table_stats(struct table_node *head, long long relid)
{
	struct table_node *c = head;

	while (c != NULL) {
		if (c->relid == relid) {
			break;
		}
		c = c->next;
	}
	return c;
}

void pg_display_index_stats(char *conninfo, int compare_index, int max_topn)
{
	int i;
	int rows;
	PGconn *pgconn;
	PGresult *pgresult = NULL;
	static char line[512];

	static struct index_node *head = NULL;
	static struct index_node **procs = NULL;

	int max_lines;

	/* Get the currently running query. */
	pgconn = connect_to_db(conninfo);
	if (pgconn != NULL) {
		pgresult = PQexec(pgconn, SELECT_INDEX_STATS);
		rows = PQntuples(pgresult);
	} else {
		PQfinish(pgconn);
		return;
	}

	max_lines = rows < max_topn ? rows : max_topn;

	procs = (struct index_node **) realloc(procs,
			rows * sizeof(struct index_node *));

	/* Calculate change in values. */
	for (i = max_lines - 1; i > -1; i--) {
		head = upsert_index_stats(head,
				atoll(PQgetvalue(pgresult, i, 0)),
				atoll(PQgetvalue(pgresult, i, 2)),
				atoll(PQgetvalue(pgresult, i, 3)),
				atoll(PQgetvalue(pgresult, i, 4)));
	}

	/* Sort stats. */
	for (i = 0; i < rows; i++) {
		procs[i] = get_index_stats(head, atoll(PQgetvalue(pgresult, i, 0)));
		procs[i]->name_index = i;
	}
	qsort(procs, rows, sizeof(struct index_node *),
			index_compares[compare_index]);

	/* Display stats. */
	for (i = max_lines - 1; i > -1; i--) {
		snprintf(line, sizeof(line), "%9lld %9lld %9lld %s",
				procs[i]->diff_idx_scan,
				procs[i]->diff_idx_tup_read,
				procs[i]->diff_idx_tup_fetch,
				PQgetvalue(pgresult, procs[i]->name_index, 1));
		u_process(max_lines - i - 1, line);
	}

	if (pgresult != NULL) 
		PQclear(pgresult);
	PQfinish(pgconn);
}

void pg_display_table_stats(char *conninfo, int compare_index, int max_topn)
{
	int i;
	int rows;
	PGconn *pgconn;
	PGresult *pgresult = NULL;
	static char line[512];

	static struct table_node *head = NULL;
	static struct table_node **procs = NULL;

	int max_lines;

	/* Get the currently running query. */
	pgconn = connect_to_db(conninfo);
	if (pgconn != NULL) {
		pgresult = PQexec(pgconn, SELECT_TABLE_STATS);
		rows = PQntuples(pgresult);
	} else {
		PQfinish(pgconn);
		return;
	}

	max_lines = rows < max_topn ? rows : max_topn;

	procs = (struct table_node **) realloc(procs,
			rows * sizeof(struct table_node *));

	/* Calculate change in values. */
	for (i = 0; i < rows; i++) {
		head = upsert_table_stats(head,
				atoll(PQgetvalue(pgresult, i, 0)),
				atoll(PQgetvalue(pgresult, i, 2)),
				atoll(PQgetvalue(pgresult, i, 3)),
				atoll(PQgetvalue(pgresult, i, 4)),
				atoll(PQgetvalue(pgresult, i, 5)),
				atoll(PQgetvalue(pgresult, i, 6)),
				atoll(PQgetvalue(pgresult, i, 7)),
				atoll(PQgetvalue(pgresult, i, 8)));
	}

	/* Sort stats. */
	for (i = 0; i < rows; i++) {
		procs[i] = get_table_stats(head, atoll(PQgetvalue(pgresult, i, 0)));
		procs[i]->name_index = i;
	}
	qsort(procs, rows, sizeof(struct table_node *),
			table_compares[compare_index]);

	for (i = max_lines - 1; i > -1; i--) {
		snprintf(line, sizeof(line),
				"%9lld %9lld %9lld %9lld %9lld %9lld %9lld %s",
				procs[i]->diff_seq_scan,
				procs[i]->diff_seq_tup_read,
				procs[i]->diff_idx_scan,
				procs[i]->diff_idx_tup_fetch,
				procs[i]->diff_n_tup_ins,
				procs[i]->diff_n_tup_upd,
				procs[i]->diff_n_tup_del,
				PQgetvalue(pgresult, procs[i]->name_index, 1));
		u_process(max_lines - i - 1, line);
	}

	if (pgresult != NULL) 
		PQclear(pgresult);
	PQfinish(pgconn);
}

struct index_node *insert_index_stats(struct index_node *head,
		struct index_node *node)
{
	struct index_node *c = head;
	struct index_node *p = NULL;

	/* Check the head of the list as a special case. */
	if (node->indexrelid < head->indexrelid) {
		node->next = head;
		head = node;
		return head;
	}

	c = head->next;
	p = head;
	while (c != NULL) {
		if (node->indexrelid < c->indexrelid) {
			node->next = c;

			p->next = node;

			return head;
		}
		p = c;
		c = c->next;
	}

	/*
	 * The node to be inserted has the highest indexrelid so it goes on the
	 * end.
	 */
	if (c == NULL) {
		p->next = node;
	}
	return head;
}

struct table_node *insert_table_stats(struct table_node *head,
		struct table_node *node)
{
	struct table_node *c = head;
	struct table_node *p = NULL;

	/* Check the head of the list as a special case. */
	if (node->relid < head->relid) {
		node->next = head;
		head = node;
		return head;
	}

	c = head->next;
	p = head;
	while (c != NULL) {
		if (node->relid < c->relid) {
			node->next = c;

			p->next = node;

			return head;
		}
		p = c;
		c = c->next;
	}

	/*
	 * The node to be inserted has the highest relid so it goes on the
	 * end.
	 */
	if (c == NULL) {
		p->next = node;
	}
	return head;
}

struct index_node *new_index_node(long long indexrelid)
{
	struct index_node * node;

	node = (struct index_node *) malloc(sizeof(struct index_node));
	node->indexrelid = indexrelid;
	node->old_idx_scan = 0;
	node->old_idx_tup_read = 0;
	node->old_idx_tup_fetch = 0;
	node->next = NULL;

	return node;
}

struct table_node *new_table_node(long long relid)
{
	struct table_node * node;

	node = (struct table_node *) malloc(sizeof(struct table_node));
	node->relid = relid;
	node->old_idx_scan = 0;
	node->old_idx_tup_fetch = 0;
	node->old_n_tup_del = 0;
	node->old_n_tup_ins = 0;
	node->old_n_tup_upd = 0;
	node->old_seq_scan = 0;
	node->old_seq_tup_read = 0;
	node->next = NULL;

	return node;
}

void update_index_stats(struct index_node *node, long long idx_scan,
		long long idx_tup_read, long long idx_tup_fetch)
{
	/* Calculate difference between previous and current values. */
	node->diff_idx_scan = idx_scan - node->old_idx_scan;
	node->diff_idx_tup_read = idx_tup_read - node->old_idx_tup_read;
	node->diff_idx_tup_fetch = idx_tup_fetch - node->old_idx_tup_fetch;

	/* Save the current values as previous values. */
	node->old_idx_scan = idx_scan;
	node->old_idx_tup_read = idx_tup_read;
	node->old_idx_tup_fetch = idx_tup_fetch;
}

void update_table_stats(struct table_node *node, long long seq_scan,
		long long seq_tup_read, long long idx_scan, long long idx_tup_fetch,
		long long n_tup_ins, long long n_tup_upd, long long n_tup_del)
{
	/* Calculate difference between previous and current values. */
	node->diff_idx_scan = idx_scan - node->old_idx_scan;
	node->diff_idx_tup_fetch = idx_tup_fetch - node->old_idx_tup_fetch;
	node->diff_n_tup_del = n_tup_del - node->old_n_tup_del;
	node->diff_n_tup_ins = n_tup_ins - node->old_n_tup_ins;
	node->diff_n_tup_upd = n_tup_upd - node->old_n_tup_upd;
	node->diff_seq_scan = seq_scan - node->old_seq_scan;
	node->diff_seq_tup_read = seq_tup_read - node->old_seq_tup_read;

	/* Save the current values as previous values. */
	node->old_idx_scan = idx_scan;
	node->old_idx_tup_fetch = idx_tup_fetch;
	node->old_n_tup_del = n_tup_del;
	node->old_n_tup_ins = n_tup_ins;
	node->old_n_tup_upd = n_tup_upd;
	node->old_seq_scan = seq_scan;
	node->old_seq_tup_read = seq_tup_read;
}

/*
 * Determine if indexrelid exists in the list and update it if it does.
 * Otherwise Create a new node and insert it into the list.  Sort this
 * list by indexrelid.
 */
struct index_node *upsert_index_stats(struct index_node *head,
		long long indexrelid, long long idx_scan, long long idx_tup_read,
		long long idx_tup_fetch)
{
	struct index_node *c = head;

	/* List is empty, create a new node. */
	if (head == NULL) {
		head = new_index_node(indexrelid);
		update_index_stats(head, idx_scan, idx_tup_read, idx_tup_fetch);
		return head;
	}

	/* Check if this indexrelid exists already. */
	while (c != NULL) {
		if (c->indexrelid == indexrelid) {
			/* Found an existing node with same indexrelid, update it. */
			update_index_stats(c, idx_scan, idx_tup_read, idx_tup_fetch);
			return head;
		}
		c = c->next;
	}

	/*
	 * Didn't find indexrelid.  Create a new node, save the data and insert
	 * it.
	 */
	c = new_index_node(indexrelid);
	update_index_stats(c, idx_scan, idx_tup_read, idx_tup_fetch);
	head = insert_index_stats(head, c);
	return head;
}

/*
 * Determine if relid exists in the list and update it if it does.
 * Otherwise Create a new node and insert it into the list.  Sort this
 * list by relid.
 */
struct table_node *upsert_table_stats(struct table_node *head,
		long long relid, long long seq_scan, long long seq_tup_read,
		long long idx_scan, long long idx_tup_fetch, long long n_tup_ins,
		long long n_tup_upd, long long n_tup_del)
{
	struct table_node *c = head;

	/* List is empty, create a new node. */
	if (head == NULL) {
		head = new_table_node(relid);
		update_table_stats(head, seq_scan, seq_tup_read, idx_scan,
				idx_tup_fetch, n_tup_ins, n_tup_upd, n_tup_del);
		return head;
	}

	/* Check if this relid exists already. */
	while (c != NULL) {
		if (c->relid == relid) {
			/* Found an existing node with same relid, update it. */
			update_table_stats(c, seq_scan, seq_tup_read, idx_scan,
				idx_tup_fetch, n_tup_ins, n_tup_upd, n_tup_del);
			return head;
		}
		c = c->next;
	}

	/*
	 * Didn't find relid.  Create a new node, save the data and insert
	 * it.
	 */
	c = new_table_node(relid);
	update_table_stats(c, seq_scan, seq_tup_read, idx_scan, idx_tup_fetch,
			n_tup_ins, n_tup_upd, n_tup_del);
	head = insert_table_stats(head, c);
	return head;
}
