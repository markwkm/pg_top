/*  Copyright (c) 2007, Mark Wong */

#include <stdlib.h>

#include "display.h"
#include "pg.h"

char *index_ordernames[] = {
		"idx_scan", "idx_tup_fetch", "idx_tup_read", NULL
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

struct index_node *get_index_stats(struct index_node *, long long);
struct index_node *insert_index_stats(struct index_node *, struct index_node *);
struct index_node *new_index_node(long long);
void update_index_stats(struct index_node *, long long, long long, long long);
struct index_node *upsert_index_stats(struct index_node *, long long,
		long long, long long, long long);

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
	for (i = 0; i < rows; i++) {
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
