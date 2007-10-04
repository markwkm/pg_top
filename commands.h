/* call specifications for commands.c */

#ifndef _COMMANDS_H_
#define _COMMANDS_H_

void show_help(struct statics *);
int scanint(char *str, int *intp);
int error_count();
void show_errors();
char *kill_procs(char *str);
char *renice_procs(char *str);
void show_current_query(char *, int);
void show_explain(char *, int);
void show_locks(char *, int);

#endif /* _COMMANDS_H_ */
