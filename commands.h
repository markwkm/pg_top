/* call specifications for commands.c */

#ifndef _COMMANDS_H_
#define _COMMANDS_H_

#include "pg_top.h"

/* these defines enumerate the "strchr"s of the commands in command_chars */
#define CMD_redraw	0
#define CMD_update	1
#define CMD_quit	2
#define CMD_help1	3
#define CMD_help2	4
#define CMD_OSLIMIT 4			/* terminals with OS can only handle commands */
#define CMD_errors	5			/* less than or equal to CMD_OSLIMIT	  */
#define CMD_number1 6
#define CMD_number2 7
#define CMD_delay	8
#define CMD_displays	9
#define CMD_kill	10
#define CMD_renice	11
#define CMD_idletog		12
#define CMD_io	13
#define CMD_user	14
#define CMD_cmdline		15
#define CMD_order		16
#define CMD_color		17
#define CMD_order_pid	18
#define CMD_order_cpu	19
#define CMD_order_mem	20
#define CMD_order_time	21
#define CMD_current_query 22
#define CMD_locks 23
#define CMD_explain 24
#define CMD_tables 25
#define CMD_indexes 26
#define CMD_explain_analyze 27
#define CMD_toggle 28
#define CMD_statements 29


#define EXPLAIN 0
#define EXPLAIN_ANALYZE 1

#ifdef ENABLE_COLOR
void cmd_color(struct pg_top_context *);
#endif /* ENABLE_COLOR */
void cmd_cmdline(struct pg_top_context *);
void cmd_current_query(struct pg_top_context *);
void cmd_delay(struct pg_top_context *);
void cmd_displays(struct pg_top_context *);
void cmd_errors(struct pg_top_context *);
void cmd_explain(struct pg_top_context *);
void cmd_explain_analyze(struct pg_top_context *);
void cmd_help(struct pg_top_context *);
void cmd_idletog(struct pg_top_context *);
void cmd_indexes(struct pg_top_context *);
void cmd_io(struct pg_top_context *);
#ifdef ENABLE_KILL
void cmd_kill(struct pg_top_context *);
#endif /* ENABLE_KILL */
void cmd_locks(struct pg_top_context *);
void cmd_number(struct pg_top_context *);
void cmd_quit(struct pg_top_context *);
void cmd_order(struct pg_top_context *);
void cmd_order_cpu(struct pg_top_context *);
void cmd_order_mem(struct pg_top_context *);
void cmd_order_pid(struct pg_top_context *);
void cmd_order_time(struct pg_top_context *);
void cmd_redraw(struct pg_top_context *);
#ifdef ENABLE_KILL
void cmd_renice(struct pg_top_context *);
#endif /* ENABLE_KILL */
void cmd_statements(struct pg_top_context *);
void cmd_tables(struct pg_top_context *);
void cmd_toggle(struct pg_top_context *);
void cmd_update(struct pg_top_context *);
void cmd_user(struct pg_top_context *);

void show_help(struct statics *);
int scanint(char *str, int *intp);
int error_count();
void show_errors();
char *kill_procs(char *str);
char *renice_procs(char *str);
void show_current_query(char *, int);
void show_explain(char *, int, int);
void show_locks(char *, int);

#endif   /* _COMMANDS_H_ */
