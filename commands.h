/* call specifications for commands.c */

void show_help(struct statics *);
int scanint(char *str, int *intp);
int error_count();
void show_errors();
char *kill_procs(char *str);
char *renice_procs(char *str);
