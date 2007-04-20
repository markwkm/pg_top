/* interface declaration for display.c */

#ifndef _DISPLAY_H
#define _DISPLAY_H

/* "type" argument for new_message function */

#define  MT_standout  1
#define  MT_delayed   2

int display_resize();
int display_init(struct statics *statics);
void i_loadave(int mpid, double *avenrun);
void u_loadave(int mpid, double *avenrun);
void i_minibar(int (*)(char *, int));
void u_minibar(int (*)(char *, int));
void i_uptime(time_t *bt, time_t *tod);
void u_uptime(time_t *bt, time_t *tod);
void i_timeofday(time_t *tod);
void i_procstates(int total, int *brkdn);
void u_procstates(int total, int *brkdn);
void i_cpustates(int *states);
void u_cpustates(int *states);
void z_cpustates();
void i_memory(long *stats);
void u_memory(long *stats);
void i_swap(long *stats);
void u_swap(long *stats);
void i_message();
void u_message();
void i_header(char *text);
void u_header(char *text);
void i_process(int line, char *thisline);
void u_process(int line, char *newline);
void u_endscreen(int hi);
void display_header(int t);
void new_message(int type, char *msgfmt, ...);
void error_message(char *msgfmt, ...);
void clear_message();
int readline(char *buffer, int size, int numeric);
void display_pagerstart();
void display_pagerend();
void display_pager(char *data);

#endif
