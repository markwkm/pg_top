/*
 *  top - a top users display for Unix 4.2
 *
 *  This file contains all the definitions necessary to use the hand-written
 *  screen package in "screen.c"
 */

/* includes for termcap */
#ifdef HAVE_TERMCAP_H
#include <termcap.h>
#else
int tputs(const char *, int, int (*)(int));
char *tgoto(const char *, int, int);
int tgetent(const char *, char *);
int tgetflag(const char *);
int tgetnum(const char *);
char *tgetstr(const char *, char **);
#endif

#define TCputs(str)	tputs(str, 1, putstdout)
#define putcap(str)	(void)((str) != NULL ? TCputs(str) : 0)
#define Move_to(x, y)	TCputs(tgoto(cursor_motion, x, y))

extern char ch_erase;		/* set to the user's erase character */
extern char ch_kill;		/* set to the user's kill  character */
extern char smart_terminal;     /* set if the terminal has sufficient termcap
				   capabilities for normal operation */

/* These are some termcap strings for use outside of "screen.c" */
extern char *cursor_motion;
extern char *clear_line;
extern char *clear_to_end;

/* rows and columns on the screen according to termcap */
extern int  screen_length;
extern int  screen_width;

int putstdout(int);
void get_screensize();
void init_termcap(int interactive);
void init_screen();
void end_screen();
void reinit_screen();
void standout(char *msg);
void clear();
int clear_eol(int len);
void go_home();
