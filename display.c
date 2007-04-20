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

/*
 *  This file contains the routines that display information on the screen.
 *  Each section of the screen has two routines:  one for initially writing
 *  all constant and dynamic text, and one for only updating the text that
 *  changes.  The prefix "i_" is used on all the "initial" routines and the
 *  prefix "u_" is used for all the "updating" routines.
 *
 *  ASSUMPTIONS:
 *        None of the "i_" routines use any of the termcap capabilities.
 *        In this way, those routines can be safely used on terminals that
 *        have minimal (or nonexistant) terminal capabilities.
 *
 *        The routines are called in this order:  *_loadave, i_timeofday,
 *        *_procstates, *_cpustates, *_memory, *_message, *_header,
 *        *_process, u_endscreen.
 */

#include "os.h"
#include <ctype.h>
#include <stdarg.h>

#include "top.h"
#include "machine.h"
#include "screen.h"		/* interface to screen package */
#include "layout.h"		/* defines for screen position layout */
#include "display.h"
#include "boolean.h"
#include "utils.h"

#ifdef ENABLE_COLOR
#include "color.h"
#endif

#define CURSOR_COST 8

/* imported from screen.c */
extern int overstrike;

static int lmpid = -1;
static int display_width = MAX_COLS;

/* cursor positions of key points on the screen are maintained here */
/* layout.h has static definitions, but we may change our minds on some
   of the positions as we make decisions about what needs to be displayed */

static int x_lastpid = X_LASTPID;
static int y_lastpid = Y_LASTPID;
static int x_loadave = X_LOADAVE;
static int y_loadave = Y_LOADAVE;
static int x_minibar = X_MINIBAR;
static int y_minibar = Y_MINIBAR;
static int x_uptime = X_UPTIME;
static int y_uptime = Y_UPTIME;
static int x_procstate = X_PROCSTATE;
static int y_procstate = Y_PROCSTATE;
static int x_brkdn = X_BRKDN;
static int y_brkdn = Y_BRKDN;
static int x_cpustates = X_CPUSTATES;
static int y_cpustates = Y_CPUSTATES;
static int x_mem = X_MEM;
static int y_mem = Y_MEM;
static int x_swap = -1;
static int y_swap = -1;
static int y_message = Y_MESSAGE;
static int x_header = X_HEADER;
static int y_header = Y_HEADER;
static int x_idlecursor = X_IDLECURSOR;
static int y_idlecursor = Y_IDLECURSOR;
static int y_procs = Y_PROCS;

/* buffer and colormask that describes the content of the screen */
/* these are singly dimensioned arrays -- the row boundaries are
   determined on the fly.
*/
static char *screenbuf = NULL;
static char *colorbuf = NULL;
static char scratchbuf[MAX_COLS];
static int bufsize = 0;

/* lineindex tells us where the beginning of a line is in the buffer */
#define lineindex(l) ((l)*MAX_COLS)

/* screen's cursor */
static int curr_x, curr_y;
static int curr_color;

/* virtual cursor */
static int virt_x, virt_y;

static char **procstate_names;
static char **cpustate_names;
static char **memory_names;
static char **swap_names;

static int num_procstates;
static int num_cpustates;
static int num_memory;
static int num_swap;

static int *lprocstates;
static int *lcpustates;

static int *cpustate_columns;
static int cpustate_total_length;

static enum { OFF, ON, ERASE } header_status = ON;

#ifdef ENABLE_COLOR
static int load_cidx[3];
static int header_cidx;
static int *cpustate_cidx;
static int *memory_cidx;
static int *swap_cidx;
#endif
static int header_color = 0;


/* internal support routines */

/*
 * static int string_count(char **pp)
 *
 * Pointer "pp" points to an array of string pointers, which is
 * terminated by a NULL.  Return the number of string pointers in
 * this array.
 */

static int
string_count(char **pp)

{
    register int cnt = 0;

    if (pp != NULL)
    {
	while (*pp++ != NULL)
	{
	    cnt++;
	}
    }
    return(cnt);
}

void
display_clear()

{
    dprintf("display_clear\n");
    clear();
    memzero(screenbuf, bufsize);
    memzero(colorbuf, bufsize);
    curr_x = curr_y = 0;
}

/*
 * void display_move(int x, int y)
 *
 * Efficiently move the cursor to x, y.  This assumes the cursor is
 * currently located at curr_x, curr_y, and will only use cursor
 * addressing when it is less expensive than overstriking what's
 * already on the screen.
 */

void
display_move(int x, int y)

{
    char buff[128];
    char *p;
    char *bufp;
    char *colorp;
    int cnt = 0;
    int color = curr_color;

    dprintf("display_move(%d, %d): curr_x %d, curr_y %d\n", x, y, curr_x, curr_y);

    /* are we in a position to do this without cursor addressing? */
    if (curr_y < y || (curr_y == y && curr_x <= x))
    {
	/* start buffering up what it would take to move there by rewriting
	   what's on the screen */
	cnt = CURSOR_COST;
	p = buff;

	/* one newline for every line */
	while (cnt > 0 && curr_y < y)
	{
	    if (color != 0)
	    {
		p = strecpy(p, color_set(0));
		color = 0;
		cnt -= 5;
	    }
	    *p++ = '\n';
	    curr_y++;
	    curr_x = 0;
	    cnt--;
	}

	/* write whats in the screenbuf */
	bufp = &screenbuf[lineindex(curr_y) + curr_x];
	colorp = &colorbuf[lineindex(curr_y) + curr_x];
	while (cnt > 0 && curr_x < x)
	{
	    if (color != *colorp)
	    {
		color = *colorp;
		p = strecpy(p, color_set(color));
		cnt -= 5;
	    }
	    if ((*p = *bufp) == '\0')
	    {
		/* somwhere on screen we haven't been before */
		*p = *bufp = ' ';
	    }
	    p++;
	    bufp++;
	    colorp++;
	    curr_x++;
	    cnt--;
	}
    }

    /* move the cursor */
    if (cnt > 0)
    {
	/* screen rewrite is cheaper */
	*p = '\0';
	fputs(buff, stdout);
	curr_color = color;
    }
    else
    {
	Move_to(x, y);
    }

    /* update our position */
    curr_x = x;
    curr_y = y;
}

/*
 * display_write(int x, int y, int newcolor, int eol, char *new)
 *
 * Optimized write to the display.  This writes characters to the
 * screen in a way that optimizes the number of characters actually
 * sent, by comparing what is being written to what is already on
 * the screen (according to screenbuf and colorbuf).  The string to
 * write is "new", the first character of "new" should appear at
 * screen position x, y.  If x is -1 then "new" begins wherever the
 * cursor is currently positioned.  The string is written with color
 * "newcolor".  If "eol" is true then the remainder of the line is
 * cleared.  It is expected that "new" will have no newlines and no
 * escape sequences.
 */

void
display_write(int x, int y, int newcolor, int eol, char *new)

{
    char *bufp;
    char *colorp;
    int ch;
    int diff;

    dprintf("display_write(%d, %d, %d, %d, \"%s\")\n",
	    x, y, newcolor, eol, new);

    /* dumb terminal handling here */
    if (!smart_terminal)
    {
	if (x != -1)
	{
	    /* make sure we are on the right line */
	    while (curr_y < y)
	    {
		putchar('\n');
		curr_y++;
		curr_x = 0;
	    }

	    /* make sure we are on the right column */
	    while (curr_x < x)
	    {
		putchar(' ');
		curr_x++;
	    }
	}

	/* write */
	fputs(new, stdout);
	curr_x += strlen(new);

	return;
    }

    /* adjust for "here" */
    if (x == -1)
    {
	x = virt_x;
	y = virt_y;
    }
    else
    {
	virt_x = x;
	virt_y = y;
    }

    /* a pointer to where we start */
    bufp = &screenbuf[lineindex(y) + x];
    colorp = &colorbuf[lineindex(y) + x];

    /* main loop */
    while ((ch = *new++) != '\0')
    {
	/* if either character or color are different, an update is needed */
	/* but only when the screen is wide enough */
	if (x < display_width && (ch != *bufp || newcolor != *colorp))
	{
	    /* check cursor */
	    if (y != curr_y || x != curr_x)
	    {
		/* have to move the cursor */
		display_move(x, y);
	    }

	    /* write character */
	    if (curr_color != newcolor)
	    {
		fputs(color_set(newcolor), stdout);
		curr_color = newcolor;
	    }
	    putchar(ch);
	    *bufp = ch;
	    *colorp = curr_color;
	    curr_x++;
	}

	/* move */
	x++;
	virt_x++;
	bufp++;
	colorp++;
    }

    /* eol handling */
    if (eol && *bufp != '\0')
    {
	dprintf("display_write: clear-eol (bufp = \"%s\")\n", bufp);
	/* make sure we are color 0 */
	if (curr_color != 0)
	{
	    fputs(color_set(0), stdout);
	    curr_color = 0;
	}

	/* make sure we are at the end */
	if (x != curr_x || y != curr_y)
	{
	    Move_to(x, y);
	    curr_x = x;
	    curr_y = y;
	}

	/* clear to end */
	clear_eol(strlen(bufp));

	/* clear out whats left of this line's buffer */
	diff = display_width - x;
	if (diff > 0)
	{
	    memzero(bufp, diff);
	    memzero(colorp, diff);
	}
    }
}

void
display_fmt(int x, int y, int newcolor, int eol, char *fmt, ...)

{
    va_list argp;

    va_start(argp, fmt);

    vsnprintf(scratchbuf, MAX_COLS, fmt, argp);
    display_write(x, y, newcolor, eol, scratchbuf);
}

void
display_cte()

{
    int len;
    int y;
    char *p;
    int need_clear = 0;

    /* is there anything out there that needs to be cleared? */
    p = &screenbuf[lineindex(virt_y) + virt_x];
    if (*p != '\0')
    {
	need_clear = 1;
    }
    else
    {
	/* this line is clear, what about the rest? */
	y = virt_y;
	while (++y < screen_length)
	{
	    if (screenbuf[lineindex(y)] != '\0')
	    {
		need_clear = 1;
		break;
	    }
	}
    }

    if (need_clear)
    {
	dprintf("display_cte: clearing\n");

	/* different method when there's no clear_to_end */
	if (clear_to_end)
	{
	    display_move(virt_x, virt_y);
	    putcap(clear_to_end);
	}
	else
	{
	    if (++virt_y < screen_length)
	    {
		display_move(0, virt_y);
		virt_x = 0;
		while (virt_y < screen_length)
		{
		    p = &screenbuf[lineindex(virt_y)];
		    len = strlen(p);
		    if (len > 0)
		    {
			clear_eol(len);
		    }
		    virt_y++;
		}
	    }
	}

	/* clear the screenbuf */
	len = lineindex(virt_y) + virt_x;
	memzero(&screenbuf[len], bufsize -len);
	memzero(&colorbuf[len], bufsize -len);
    }
}

static void
summary_format(int x, int y, int *numbers, char **names)

{
    register int num;
    register char *thisname;
    register char *lastname = NULL;

    /* format each number followed by its string */
    while ((thisname = *names++) != NULL)
    {
	/* get the number to format */
	num = *numbers++;

	/* display only non-zero numbers */
	if (num != 0)
	{
	    /* write the previous name */
	    if (lastname != NULL)
	    {
		display_write(-1, -1, 0, 0, lastname);
	    }

	    /* write this number if positive */
	    if (num > 0)
	    {
		display_write(x, y, 0, 0, itoa(num));
	    }

	    /* defer writing this name */
	    lastname = thisname;

	    /* next iteration will not start at x, y */
	    x = y = -1;
	}
    }

    /* if the last string has a separator on the end, it has to be
       written with care */
    if ((num = strlen(lastname)) > 1 &&
	lastname[num-2] == ',' && lastname[num-1] == ' ')
    {
	display_fmt(-1, -1, 0, 1, "%.*s", num-2, lastname);
    }
    else
    {
	display_write(-1, -1, 0, 1, lastname);
    }
}

static void
summary_format_memory(int x, int y, long *numbers, char **names, int *cidx)

{
    register long num;
    register int color;
    register char *thisname;
    register char *lastname = NULL;

    /* format each number followed by its string */
    while ((thisname = *names++) != NULL)
    {
	/* get the number to format */
	num = *numbers++;
	color = 0;

	/* display only non-zero numbers */
	if (num != 0)
	{
	    /* write the previous name */
	    if (lastname != NULL)
	    {
		display_write(-1, -1, 0, 0, lastname);
	    }

	    /* defer writing this name */
	    lastname = thisname;

#ifdef ENABLE_COLOR
	    /* choose a color */
	    color = color_test(*cidx++, num);
#endif

	    /* is this number in kilobytes? */
	    if (thisname[0] == 'K')
	    {
		display_write(x, y, color, 0, format_k(num));
		lastname++;
	    }
	    else
	    {
		display_write(x, y, color, 0, itoa((int)num));
	    }

	    /* next iteration will not start at x, y */
	    x = y = -1;
	}
    }

    /* if the last string has a separator on the end, it has to be
       written with care */
    if ((num = strlen(lastname)) > 1 &&
	lastname[num-2] == ',' && lastname[num-1] == ' ')
    {
	display_fmt(-1, -1, 0, 1, "%.*s", num-2, lastname);
    }
    else
    {
	display_write(-1, -1, 0, 1, lastname);
    }
}

/*
 * int display_resize()
 *
 * Reallocate buffer space needed by the display package to accomodate
 * a new screen size.  Must be called whenever the screen's size has
 * changed.  Returns the number of lines available for displaying 
 * processes or -1 if there was a problem allocating space.
 */

int
display_resize()

{
    register int lines;
    register int newsize;

    /* calculate the current dimensions */
    /* if operating in "dumb" mode, we only need one line */
    lines = smart_terminal ? screen_length : 1;

    /* we don't want more than MAX_COLS columns, since the machine-dependent
       modules make static allocations based on MAX_COLS and we don't want
       to run off the end of their buffers */
    display_width = screen_width;
    if (display_width >= MAX_COLS)
    {
	display_width = MAX_COLS - 1;
    }

    /* see how much space we need */
    newsize = lines * (MAX_COLS + 1);

    /* reallocate only if we need more than we already have */
    if (newsize > bufsize)
    {
	/* deallocate any previous buffer that may have been there */
	if (screenbuf != NULL)
	{
	    free(screenbuf);
	}
	if (colorbuf != NULL)
	{
	    free(colorbuf);
	}

	/* allocate space for the screen and color buffers */
	bufsize = newsize;
	screenbuf = (char *)calloc(bufsize, sizeof(char));
	colorbuf = (char *)calloc(bufsize, sizeof(char));
	if (screenbuf == NULL || colorbuf == NULL)
	{
	    /* oops! */
	    return(-1);
	}
    }
    else
    {
	/* just clear them out */
	memzero(screenbuf, bufsize);
	memzero(colorbuf, bufsize);
    }

    /* adjust total lines on screen to lines available for procs */
    lines -= y_procs;

    /* return number of lines available */
    /* for dumb terminals, pretend like we can show any amount */
    return(smart_terminal ? lines : Largest);
}

/*
 * int display_init(struct statics *statics)
 *
 * Initialize the display system based on information in the statics
 * structure.  Returns the number of lines available for displaying
 * processes or -1 if there was an error.
 */

int
display_init(struct statics *statics)

{
    register int lines;
    register char **pp;
    register char *p;
    register int *ip;
    register int i;

    /* certain things may influence the screen layout,
       so look at those first */
    /* a swap line shifts parts of the display down one */
    swap_names = statics->swap_names;
    if ((num_swap = string_count(swap_names)) > 0)
    {
	/* adjust screen placements */
	y_message++;
	y_header++;
	y_idlecursor++;
	y_procs++;
	x_swap = X_SWAP;
	y_swap = Y_SWAP;
    }
    
    /* call resize to do the dirty work */
    lines = display_resize();

    /* only do the rest if we need to */
    if (lines > -1)
    {
	/* save pointers and allocate space for names */
	procstate_names = statics->procstate_names;
	num_procstates = string_count(procstate_names);
	lprocstates = (int *)malloc(num_procstates * sizeof(int));

	cpustate_names = statics->cpustate_names;
	num_cpustates = string_count(cpustate_names);
	lcpustates = (int *)malloc(num_cpustates * sizeof(int));
	cpustate_columns = (int *)malloc(num_cpustates * sizeof(int));
	memory_names = statics->memory_names;
	num_memory = string_count(memory_names);

	/* calculate starting columns where needed */
	cpustate_total_length = 0;
	pp = cpustate_names;
	ip = cpustate_columns;
	while (*pp != NULL)
	{
	    *ip++ = cpustate_total_length;
	    if ((i = strlen(*pp++)) > 0)
	    {
		cpustate_total_length += i + 8;
	    }
	}
    }

#ifdef ENABLE_COLOR
    /* set up color tags for loadavg */
    load_cidx[0] = color_tag("1min");
    load_cidx[1] = color_tag("5min");
    load_cidx[2] = color_tag("15min");

    /* find header color */
    header_cidx = color_tag("header");
    header_color = color_test(header_cidx, 0);

    /* color tags for cpu states */
    cpustate_cidx = (int *)malloc(num_cpustates * sizeof(int));
    i = 0;
    p = strecpy(scratchbuf, "cpu.");
    while (i < num_cpustates)
    {
	strcpy(p, cpustate_names[i]);
	cpustate_cidx[i++] = color_tag(scratchbuf);
    }

    /* color tags for memory */
    memory_cidx = (int *)malloc(num_memory * sizeof(int));
    i = 0;
    p = strecpy(scratchbuf, "memory.");
    while (i < num_memory)
    {
	strcpy(p, homogenize(memory_names[i]+1));
	memory_cidx[i++] = color_tag(scratchbuf);
    }

    /* color tags for swap */
    swap_cidx = (int *)malloc(num_swap * sizeof(int));
    i = 0;
    p = strecpy(scratchbuf, "swap.");
    while (i < num_swap)
    {
	strcpy(p, homogenize(swap_names[i]+1));
	swap_cidx[i++] = color_tag(scratchbuf);
    }
#endif

    /* return number of lines available (or error) */
    return(lines);
}

static void
pr_loadavg(double avg, int i)

{
    int color = 0;

#ifdef ENABLE_COLOR
    color = color_test(load_cidx[i], (int)(avg * 100));
#endif
    display_fmt(x_loadave + X_LOADAVEWIDTH * i, y_loadave, color, 0,
		avg < 10.0 ? " %5.2f" : " %5.1f", avg);
    display_write(-1, -1, 0, 0, (i < 2 ? "," : ";"));
}

void
i_loadave(int mpid, double *avenrun)

{
    register int i;

    /* i_loadave also clears the screen, since it is first */
    display_clear();

    /* mpid == -1 implies this system doesn't have an _mpid */
    if (mpid != -1)
    {
	display_fmt(0, 0, 0, 0,
		    "last pid: %5d;  load avg:", mpid);
	x_loadave = X_LOADAVE;
    }
    else
    {
	display_write(0, 0, 0, 0, "load averages:");
	x_loadave = X_LOADAVE - X_LASTPIDWIDTH;
    }
    for (i = 0; i < 3; i++)
    {
	pr_loadavg(avenrun[i], i);
    }

    lmpid = mpid;
}

void
u_loadave(int mpid, double *avenrun)

{
    register int i;

    if (mpid != -1)
    {
	/* change screen only when value has really changed */
	if (mpid != lmpid)
	{
	    display_fmt(x_lastpid, y_lastpid, 0, 0,
			"%5d", mpid);
	    lmpid = mpid;
	}
    }

    /* display new load averages */
    for (i = 0; i < 3; i++)
    {
	pr_loadavg(avenrun[i], i);
    }
}

static char minibar_buffer[64];
#define MINIBAR_WIDTH 20

void
i_minibar(int (*formatter)(char *, int))
{
    (void)((*formatter)(minibar_buffer, MINIBAR_WIDTH));

    display_write(x_minibar, y_minibar, 0, 0, minibar_buffer);
}

void
u_minibar(int (*formatter)(char *, int))
{
    (void)((*formatter)(minibar_buffer, MINIBAR_WIDTH));

    display_write(x_minibar, y_minibar, 0, 0, minibar_buffer);
}

static int uptime_days;
static int uptime_hours;
static int uptime_mins;
static int uptime_secs;

void
i_uptime(time_t *bt, time_t *tod)

{
    time_t uptime;

    if (*bt != -1)
    {
	uptime = *tod - *bt;
	uptime += 30;
	uptime_days = uptime / 86400;
	uptime %= 86400;
	uptime_hours = uptime / 3600;
	uptime %= 3600;
	uptime_mins = uptime / 60;
	uptime_secs = uptime % 60;

	/*
	 *  Display the uptime.
	 */

	display_fmt(x_uptime, y_uptime, 0, 0,
		    "  up %d+%02d:%02d:%02d",
		    uptime_days, uptime_hours, uptime_mins, uptime_secs);
    }
}

void
u_uptime(time_t *bt, time_t *tod)

{
    i_uptime(bt, tod);
}


void
i_timeofday(time_t *tod)

{
    /*
     *  Display the current time.
     *  "ctime" always returns a string that looks like this:
     *  
     *	Sun Sep 16 01:03:52 1973
     *  012345678901234567890123
     *	          1         2
     *
     *  We want indices 11 thru 18 (length 8).
     */

    display_fmt((smart_terminal ? screen_width : 79) - 8, 0, 0, 1,
		"%-8.8s", &(ctime(tod)[11]));
}

static int ltotal = 0;

/*
 *  *_procstates(total, brkdn, names) - print the process summary line
 */


void
i_procstates(int total, int *brkdn)

{
    /* write current number of processes and remember the value */
    display_fmt(0, y_procstate, 0, 0,
		"%d processes: ", total);
    ltotal = total;

    /* remember where the summary starts */
    x_procstate = virt_x;

    /* format and print the process state summary */
    summary_format(-1, -1, brkdn, procstate_names);

    /* save the numbers for next time */
    memcpy(lprocstates, brkdn, num_procstates * sizeof(int));
}

void
u_procstates(int total, int *brkdn)

{
    /* update number of processes only if it has changed */
    if (ltotal != total)
    {
	display_fmt(0, y_procstate, 0, 0,
		    "%d", total);

	/* if number of digits differs, rewrite the label */
	if (digits(total) != digits(ltotal))
	{
	    display_write(-1, -1, 0, 0, " processes: ");
	    x_procstate = virt_x;
	}

	/* save new total */
	ltotal = total;
    }

    /* see if any of the state numbers has changed */
    if (memcmp(lprocstates, brkdn, num_procstates * sizeof(int)) != 0)
    {
	/* format and update the line */
	summary_format(x_procstate, y_procstate, brkdn, procstate_names);
	memcpy(lprocstates, brkdn, num_procstates * sizeof(int));
    }
}

/*
 *  *_cpustates(states, names) - print the cpu state percentages
 */

/* cpustates_tag() calculates the correct tag to use to label the line */

char *
cpustates_tag()

{
    register char *use;

    static char *short_tag = "CPU: ";
    static char *long_tag = "CPU states: ";

    /* if length + strlen(long_tag) >= screen_width, then we have to
       use the shorter tag (we subtract 2 to account for ": ") */
    if (cpustate_total_length + (int)strlen(long_tag) - 2 >= screen_width)
    {
	use = short_tag;
    }
    else
    {
	use = long_tag;
    }

    /* set x_cpustates accordingly then return result */
    x_cpustates = strlen(use);
    return(use);
}

void
i_cpustates(int *states)

{
    int value;
    char **names;
    char *thisname;
    int *colp;
    int color = 0;
#ifdef ENABLE_COLOR
    int *cidx = cpustate_cidx;
#endif

    /* initialize */
    names = cpustate_names;
    colp = cpustate_columns;

    /* print tag */
    display_write(0, y_cpustates, 0, 0, cpustates_tag());

    /* now walk thru the names and print the line */
    while ((thisname = *names++) != NULL)
    {
	if (*thisname != '\0')
	{
	    /* retrieve the value and remember it */
	    value = *states;

#ifdef ENABLE_COLOR
	    /* determine color number to use */
	    color = color_test(*cidx++, value/10);
#endif

	    /* if percentage is >= 1000, print it as 100% */
	    display_fmt(x_cpustates + *colp, y_cpustates,
			color, 0,
			(value >= 1000 ? "%4.0f%% %s%s" : "%4.1f%% %s%s"),
			((float)value)/10.,
			thisname,
			*names != NULL ? ", " : "");

	}
	/* increment */
	colp++;
	states++;
    }

    /* copy over values into "last" array */
    memcpy(lcpustates, states, num_cpustates * sizeof(int));
}

void
u_cpustates(int *states)

{
    int value;
    char **names = cpustate_names;
    char *thisname;
    int *lp;
    int *colp;
    int color = 0;
#ifdef ENABLE_COLOR
    int *cidx = cpustate_cidx;
#endif

    lp = lcpustates;
    colp = cpustate_columns;

    /* we could be much more optimal about this */
    while ((thisname = *names++) != NULL)
    {
	if (*thisname != '\0')
	{
	    /* did the value change since last time? */
	    if (*lp != *states)
	    {
		/* yes, change it */
		/* retrieve value and remember it */
		value = *states;

#ifdef ENABLE_COLOR
		/* determine color number to use */
		color = color_test(*cidx, value/10);
#endif

		/* if percentage is >= 1000, print it as 100% */
		display_fmt(x_cpustates + *colp, y_cpustates, color, 0,
			    (value >= 1000 ? "%4.0f" : "%4.1f"),
			    ((double)value)/10.);

		/* remember it for next time */
		*lp = value;
	    }
#ifdef ENABLE_COLOR
	    cidx++;
#endif
	}

	/* increment and move on */
	lp++;
	states++;
	colp++;
    }
}

void
z_cpustates()

{
    register int i = 0;
    register char **names = cpustate_names;
    register char *thisname;
    register int *lp;

    /* print tag */
    display_write(0, y_cpustates, 0, 0, cpustates_tag());

    while ((thisname = *names++) != NULL)
    {
	if (*thisname != '\0')
	{
	    display_fmt(-1, -1, 0, 0, "%s    %% %s", i++ == 0 ? "" : ", ",
			thisname);
	}
    }

    /* fill the "last" array with all -1s, to insure correct updating */
    lp = lcpustates;
    i = num_cpustates;
    while (--i >= 0)
    {
	*lp++ = -1;
    }
}

/*
 *  *_memory(stats) - print "Memory: " followed by the memory summary string
 *
 *  Assumptions:  cursor is on "lastline", the previous line
 */

void
i_memory(long *stats)

{
    display_write(0, y_mem, 0, 0, "Memory: ");

    /* format and print the memory summary */
    summary_format_memory(x_mem, y_mem, stats, memory_names, memory_cidx);
}

void
u_memory(long *stats)

{
    /* format the new line */
    summary_format_memory(x_mem, y_mem, stats, memory_names, memory_cidx);
}

/*
 *  *_swap(stats) - print "Swap: " followed by the swap summary string
 *
 *  Assumptions:  cursor is on "lastline", the previous line
 *
 *  These functions only print something when num_swap > 0
 */

void
i_swap(long *stats)

{
    if (num_swap > 0)
    {
	/* print the tag */
	display_write(0, y_swap, 0, 0, "Swap: ");

	/* format and print the swap summary */
	summary_format_memory(x_swap, y_swap, stats, swap_names, swap_cidx);
    }
}

void
u_swap(long *stats)

{
    if (num_swap > 0)
    {
	/* format the new line */
	summary_format_memory(x_swap, y_swap, stats, swap_names, swap_cidx);
    }
}

/*
 *  *_message() - print the next pending message line, or erase the one
 *                that is there.
 *
 *  Note that u_message is (currently) the same as i_message.
 *
 *  Assumptions:  lastline is consistent
 */

/*
 *  i_message is funny because it gets its message asynchronously (with
 *	respect to screen updates).
 */

static char next_msg[MAX_COLS + 8];
static int msglen = 0;
/* Invariant: msglen is always the length of the message currently displayed
   on the screen (even when next_msg doesn't contain that message). */

void
i_message()

{
    if (smart_terminal)
    {
	if (next_msg[0] != '\0')
	{
	    display_move(0, y_message);
	    standout(next_msg);
	    msglen = strlen(next_msg);
	    next_msg[0] = '\0';
	}
	else if (msglen > 0)
	{
	    display_move(0, y_message);
	    (void) clear_eol(msglen);
	    msglen = 0;
	}
    }
}

void
u_message()

{
    i_message();
}

static int header_length;

/*
 *  *_header(text) - print the header for the process area
 *
 *  Assumptions:  cursor is on the previous line and lastline is consistent
 */

void
i_header(char *text)

{
    header_length = strlen(text);
    if (header_status == ON)
    {
	display_write(x_header, y_header, header_color, 1, text);
    }
    else if (header_status == ERASE)
    {
	header_status = OFF;
    }
}

/*ARGSUSED*/
void
u_header(char *text)

{
    if (header_status == ERASE)
    {
	display_write(x_header, y_header, header_color, 1, "");
	header_status = OFF;
    }
}

/*
 *  *_process(line, thisline) - print one process line
 *
 *  Assumptions:  lastline is consistent
 */

void
i_process(int line, char *thisline)

{
    /* truncate the line to conform to our current screen width */
    thisline[display_width] = '\0';

    /* write the line out */
    display_write(0, y_procs + line, 0, 1, thisline);
}

void
u_process(int line, char *newline)

{
    i_process(line, newline);
}

void
u_endscreen(int hi)

{
    if (smart_terminal)
    {
	/* clear-to-end the display */
	display_cte();

	/* move the cursor to a pleasant place */
	/* does this need to be a display_move??? */
	Move_to(x_idlecursor, y_idlecursor);
    }
    else
    {
	/* separate this display from the next with some vertical room */
	fputs("\n\n", stdout);
    }
}

void
display_header(int t)

{
    if (t)
    {
	header_status = ON;
    }
    else if (header_status == ON)
    {
	header_status = ERASE;
    }
}

void
new_message_v(int type, char *msgfmt, va_list ap)

{
    register int i;

    /* first, format the message */
    (void) vsnprintf(next_msg, sizeof(next_msg), msgfmt, ap);

    if (msglen > 0)
    {
	/* message there already -- can we clear it? */
	if (!overstrike)
	{
	    /* yes -- write it and clear to end */
	    i = strlen(next_msg);
	    if ((type & MT_delayed) == 0)
	    {
		if ((type & MT_standout) != 0)
		    standout(next_msg);
		else
		    fputs(next_msg, stdout);
		(void) clear_eol(msglen - i);
		msglen = i;
		next_msg[0] = '\0';
	    }
	}
    }
    else
    {
	if ((type & MT_delayed) == 0)
	{
	    if ((type & MT_standout) != 0)
		standout(next_msg);
	    else
		fputs(next_msg, stdout);
	    msglen = strlen(next_msg);
	    next_msg[0] = '\0';
	}
    }
}

void
new_message(int type, char *msgfmt, ...)

{
    va_list ap;

    va_start(ap, msgfmt);
    new_message_v(type, msgfmt, ap);
    va_end(ap);
}

void
error_message(char *msgfmt, ...)

{
    va_list ap;

    va_start(ap, msgfmt);
    new_message_v(MT_standout | MT_delayed, msgfmt, ap);
    va_end(ap);
}

void
clear_message()

{
    dprintf("clear_message: msglen = %d, x = %d, y = %d\n", msglen, curr_x, curr_y);
    if (clear_eol(msglen) == 1)
    {
	putchar('\r');
    }
}

int
readline(char *buffer, int size, int numeric)

{
    register char *ptr = buffer;
    register char ch;
    register char cnt = 0;
    register char maxcnt = 0;

    /* allow room for null terminator */
    size -= 1;

    /* read loop */
    while ((fflush(stdout), read(0, ptr, 1) > 0))
    {
	/* newline or return means we are done */
	if ((ch = *ptr) == '\n' || ch == '\r')
	{
	    break;
	}

	/* handle special editing characters */
	if (ch == ch_kill)
	{
	    /* kill line -- account for overstriking */
	    if (overstrike)
	    {
		msglen += maxcnt;
	    }

	    /* return null string */
	    *buffer = '\0';
	    putchar('\r');
	    return(-1);
	}
	else if (ch == ch_erase)
	{
	    /* erase previous character */
	    if (cnt <= 0)
	    {
		/* none to erase! */
		putchar('\7');
	    }
	    else
	    {
		fputs("\b \b", stdout);
		ptr--;
		cnt--;
	    }
	}
	/* check for character validity and buffer overflow */
	else if (cnt == size || (numeric && !isdigit(ch)) ||
		!isprint(ch))
	{
	    /* not legal */
	    putchar('\7');
	}
	else
	{
	    /* echo it and store it in the buffer */
	    putchar(ch);
	    ptr++;
	    cnt++;
	    if (cnt > maxcnt)
	    {
		maxcnt = cnt;
	    }
	}
    }

    /* all done -- null terminate the string */
    *ptr = '\0';

    /* account for the extra characters in the message area */
    /* (if terminal overstrikes, remember the furthest they went) */
    msglen += overstrike ? maxcnt : cnt;

    /* return either inputted number or string length */
    putchar('\r');
    return(cnt == 0 ? -1 : numeric ? atoi(buffer) : cnt);
}

void
display_pagerstart()

{
    display_clear();
}

void
display_pagerend()

{
    char ch;

    standout("Hit any key to continue: ");
    fflush(stdout);
    (void) read(0, &ch, 1);
}

void
display_pager(char *data)

{
    int ch;
    char readch;

    while ((ch = *data++) != '\0')
    {
	putchar(ch);
	if (ch == '\n')
	{
	    if (++curr_y >= screen_length - 1)
	    {
		standout("...More...");
		fflush(stdout);
		(void) read(0, &readch, 1);
		putchar('\r');
		switch(readch)
		{
		case '\r':
		case '\n':
		    curr_y--;
		    break;

		case 'q':
		    return;

		default:
		    curr_y = 0;
		}
	    }
	}
    }
}
