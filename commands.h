/*
 * call specifications for commands.c
 *
 *	Copyright (c) 2007-2019, Mark Wong
 */

#ifndef _COMMANDS_H_
#define _COMMANDS_H_

#include "pg_top.h"

struct cmd
{
	int			ch;
	int			(*func) (struct pg_top_context *);
};

#define EXPLAIN 0
#define EXPLAIN_ANALYZE 1

int			cmd_activity(struct pg_top_context *);
#ifdef ENABLE_COLOR
int			cmd_color(struct pg_top_context *);
#endif							/* ENABLE_COLOR */
int			cmd_cmdline(struct pg_top_context *);
int			cmd_current_query(struct pg_top_context *);
int			cmd_delay(struct pg_top_context *);
int			cmd_displays(struct pg_top_context *);
int			cmd_explain(struct pg_top_context *);
int			cmd_explain_analyze(struct pg_top_context *);
int			cmd_help(struct pg_top_context *);
int			cmd_idletog(struct pg_top_context *);
int			cmd_indexes(struct pg_top_context *);
int			cmd_io(struct pg_top_context *);
int			cmd_locks(struct pg_top_context *);
int			cmd_number(struct pg_top_context *);
int			cmd_quit(struct pg_top_context *);
int			cmd_replication(struct pg_top_context *);
int			cmd_order(struct pg_top_context *);
int			cmd_redraw(struct pg_top_context *);
int			cmd_statements(struct pg_top_context *);
int			cmd_toggle(struct pg_top_context *);
int			cmd_update(struct pg_top_context *);
int			cmd_user(struct pg_top_context *);

int			execute_command(struct pg_top_context *, char);

void		show_help(struct statics *);
int			scanint(char *str, int *intp);
void		show_current_query(struct pg_conninfo_ctx *, int);
void		show_explain(struct pg_conninfo_ctx *, int, int);
void		show_locks(struct pg_conninfo_ctx *, int);

#endif							/* _COMMANDS_H_ */
