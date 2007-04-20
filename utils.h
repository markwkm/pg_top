/*
 *  Top users/processes display for Unix
 *  Version 3
 *
 *  This program may be freely redistributed,
 *  but this entire comment MUST remain intact.
 *
 *  Copyright (c) 1984, 1989, William LeFebvre, Rice University
 *  Copyright (c) 1989, 1990, 1992, William LeFebvre, Northwestern University
 */

/* prototypes for functions found in utils.c */

int atoiwi(char *);
char *itoa(int);
char *itoa7(int);
int digits(int);
char *printable(char *);
char *strecpy(char *, char *);
char *homogenize(char *);
int string_index(char *, char **);
char **argparse(char *, int *);
long percentages(int, int *, long *, long *, long *);
char *errmsg(int);
char *format_percent(double);
char *format_time(long);
char *format_k(long);
char *string_list(char **);
void debug_set(int);
#ifdef DEBUG
#define dprintf xdprintf
void xdprintf(char *fmt, ...);
#else
#define dprintf if (0)
#endif
