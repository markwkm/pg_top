/*
 *	Top users/processes display for Unix
 *	Version 3
 *
 *	This program may be freely redistributed,
 *	but this entire comment MUST remain intact.
 *
 *	Copyright (c) 1984, 1989, William LeFebvre, Rice University
 *	Copyright (c) 1989, 1990, 1992, William LeFebvre, Northwestern University
 */

/*
 *	This file contains various handy utilities used by top.
 */

#include "os.h"
#include <ctype.h>
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#else
#undef DEBUG
#endif
#include "pg_top.h"
#include "utils.h"

static int
alldigits(char *s)
{
	int			ch;

	while ((ch = *s++) != '\0')
	{
		if (!isdigit(ch))
		{
			return 0;
		}
	}
	return 1;
}

int
atoiwi(char *str)
{
	register int len;

	len = strlen(str);
	if (len != 0)
	{
		if (strncmp(str, "infinity", len) == 0 ||
			strncmp(str, "all", len) == 0 ||
			strncmp(str, "maximum", len) == 0)
		{
			return (Infinity);
		}
		else if (alldigits(str))
		{
			return (atoi(str));
		}
		else
		{
			return (Invalid);
		}
	}
	return (0);
}

/*
 *	itoa - convert integer (decimal) to ascii string for positive numbers
 *		   only (we don't bother with negative numbers since we know we
 *	   don't use them).
 */

 /*
  * How do we know that 16 will suffice? Because the biggest number that we
  * will ever convert will be 2^32-1, which is 10 digits.
  */

char *
itoa(int val)
{
	register char *ptr;
	static char buffer[16];		/* result is built here */

	/*
	 * 16 is sufficient since the largest number we will ever convert will be
	 * 2^32-1, which is 10 digits.
	 */

	ptr = buffer + sizeof(buffer);
	*--ptr = '\0';
	if (val == 0)
	{
		*--ptr = '0';
	}
	else
		while (val != 0)
		{
			*--ptr = (val % 10) + '0';
			val /= 10;
		}
	return (ptr);
}

/*
 *	itoa7(val) - like itoa, except the number is right justified in a 7
 *	character field.  This code is a duplication of itoa instead of
 *	a front end to a more general routine for efficiency.
 */

char *
itoa7(uid_t val)
{
	register char *ptr;
	static char buffer[16];		/* result is built here */

	/*
	 * 16 is sufficient since the largest number we will ever convert will be
	 * 2^32-1, which is 10 digits.
	 */

	ptr = buffer + sizeof(buffer);
	*--ptr = '\0';
	if (val == 0)
	{
		*--ptr = '0';
	}
	else
		while (val != 0)
		{
			*--ptr = (val % 10) + '0';
			val /= 10;
		}
	while (ptr > buffer + sizeof(buffer) - 7)
	{
		*--ptr = ' ';
	}
	return (ptr);
}

/*
 *	digits(val) - return number of decimal digits in val.  Only works for
 *	positive numbers.  If val < 0 then digits(val) == 0, but
 *		digits(0) == 1.
 */

int
digits(int val)
{
	register int cnt = 0;

	if (val == 0)
	{
		return 1;
	}
	while (val > 0)
	{
		cnt++;
		val /= 10;
	}
	return (cnt);
}

/*
 *	printable(char *str) - make the string pointed to by "str" into one that is
 *	printable (i.e.: all ascii), by converting all non-printable
 *	characters into '?'.  Replacements are done in place and a pointer
 *	to the original buffer is returned.
 */

char *
printable(char *str)
{
	register char *ptr;
	register char ch;

	ptr = str;
	while ((ch = *ptr) != '\0')
	{
		if (!isprint(ch))
		{
			*ptr = '?';
		}
		ptr++;
	}
	return (str);
}

/*
 *	strecpy(to, from) - copy string "from" into "to" and return a pointer
 *	to the END of the string "to".
 */

char *
strecpy(char *to, char *from)
{
	while ((*to++ = *from++) != '\0');
	return (--to);
}

/*
 * char *
 * homogenize(char *str)
 *
 * Remove unwanted characters from "str" and make everything lower case.
 * Newly allocated string is returned: the original is not altered.
 */

char *
homogenize(char *str)
{
	char	   *ans;
	char	   *fr;
	char	   *to;
	int			ch;

	to = fr = ans = strdup(str);
	while ((ch = *fr++) != '\0')
	{
		if (isalnum(ch))
		{
			*to++ = tolower(ch);
		}
	}

	*to = '\0';
	return ans;
}

/*
 * string_index(string, array) - find string in array and return index
 */

int
string_index(char *string, char **array)
{
	register int i = 0;

	while (*array != NULL)
	{
		if (strcmp(string, *array) == 0)
		{
			return (i);
		}
		array++;
		i++;
	}
	return (-1);
}

/*
 * char *string_list(char **strings)
 *
 * Create a comma-separated list of the strings in the NULL-terminated
 * "strings".  Returned string is malloc-ed and should be freed when the
 * caller is done.	Note that this is not an efficient function.
 */

char *
string_list(char **strings)
{
	int			cnt = 0;
	char	  **pp;
	char	   *p;
	char	   *result = NULL;
	char	   *resp = NULL;

	pp = strings;
	while ((p = *pp++) != NULL)
	{
		cnt += strlen(p) + 2;
	}

	if (cnt > 0)
	{
		resp = result = (char *) malloc(cnt);
		pp = strings;
		while ((p = *pp++) != NULL)
		{
			resp = strecpy(resp, p);
			if (*pp != NULL)
			{
				resp = strecpy(resp, ", ");
			}
		}
	}

	return result;
}

/*
 * argparse(line, cntp) - parse arguments in string "line", separating them
 *	out into an argv-like array, and setting *cntp to the number of
 *	arguments encountered.	This is a simple parser that doesn't understand
 *	squat about quotes.
 */

char	  **
argparse(char *line, int *cntp)
{
	register char *from;
	register char *to;
	register int cnt;
	register int ch;
	int			length;
	int			lastch;
	register char **argv;
	char	  **argarray;
	char	   *args;

	/*
	 * unfortunately, the only real way to do this is to go thru the input
	 * string twice.
	 */

	/* step thru the string counting the white space sections */
	from = line;
	lastch = cnt = length = 0;
	while ((ch = *from++) != '\0')
	{
		length++;
		if (ch == ' ' && lastch != ' ')
		{
			cnt++;
		}
		lastch = ch;
	}

	/*
	 * add three to the count:	one for the initial "dummy" argument, one for
	 * the last argument and one for NULL
	 */
	cnt += 3;

	/* allocate a char * array to hold the pointers */
	argarray = (char **) malloc(cnt * sizeof(char *));

	/* allocate another array to hold the strings themselves */
	args = (char *) malloc(length + 2);

	/* initialization for main loop */
	from = line;
	to = args;
	argv = argarray;
	lastch = '\0';

	/* create a dummy argument to keep getopt happy */
	*argv++ = to;
	*to++ = '\0';
	cnt = 2;

	/* now build argv while copying characters */
	*argv++ = to;
	while ((ch = *from++) != '\0')
	{
		if (ch != ' ')
		{
			if (lastch == ' ')
			{
				*to++ = '\0';
				*argv++ = to;
				cnt++;
			}
			*to++ = ch;
		}
		lastch = ch;
	}
	*to++ = '\0';

	/* set cntp and return the allocated array */
	*cntp = cnt;
	return (argarray);
}

/*
 *	percentages(cnt, out, new, old, diffs) - calculate percentage change
 *	between array "old" and "new", putting the percentages i "out".
 *	"cnt" is size of each array and "diffs" is used for scratch space.
 *	The array "old" is updated on each call.
 *	The routine assumes modulo arithmetic.	This function is especially
 *	useful on BSD mchines for calculating cpu state percentages.
 */

long
percentages(int cnt, int64_t * out, int64_t * new, int64_t * old,
		int64_t * diffs)
{
	register int i;
	register int64_t change;
	register int64_t total_change;
	register int64_t * dp;
	int64_t		half_total;

	/* initialization */
	total_change = 0;
	dp = diffs;

	/* calculate changes for each state and the overall change */
	for (i = 0; i < cnt; i++)
	{
		if ((change = *new - *old) < 0)
		{
			/* this only happens when the counter wraps */
			change = (int64_t) ((int64_t) * new - (int64_t) * old);
		}
		total_change += (*dp++ = change);
		*old++ = *new++;
	}

	/* avoid divide by zero potential */
	if (total_change == 0)
	{
		total_change = 1;
	}

	/* calculate percentages based on overall change, rounding up */
	half_total = total_change / 2l;
	for (i = 0; i < cnt; i++)
	{
		*out++ = (int64_t) ((*diffs++ * 1000 + half_total) / total_change);
	}

	/* return the total in case the caller wants to use it */
	return (total_change);
}

/*
 * errmsg(errnum) - return an error message string appropriate to the
 *			 error number "errnum".  This is a substitute for the System V
 *			 function "strerror".  There appears to be no reliable way to
 *			 determine if "strerror" exists at compile time, so I make do
 *			 by providing something of similar functionality.  For those
 *			 systems that have strerror and NOT errlist, define
 *			 -DHAVE_STRERROR in the module file and this function will
 *			 use strerror.
 */

/* externs referenced by errmsg */

#ifndef HAVE_STRERROR
#if !HAVE_DECL_SYS_ERRLIST
extern char *sys_errlist[];
#endif

extern int	sys_nerr;
#endif

char *
errmsg(int errnum)
{
#ifdef HAVE_STRERROR
	char	   *msg = strerror(errnum);

	if (msg != NULL)
	{
		return msg;
	}
#else
	if (errnum > 0 && errnum < sys_nerr)
	{
		return ((char *) (sys_errlist[errnum]));
	}
#endif
	return ("No error");
}

/* format_percent(v) - format a double as a percentage in a manner that
 *		does not exceed 5 characters (excluding any trailing
 *		percent sign).	Since it is possible for the value
 *		to exceed 100%, we format such values with no fractional
 *		component to fit within the 5 characters.
 */

char *
format_percent(double v)
{
	static char result[10];

	/* enumerate the possibilities */
	if (v < 0 || v >= 100000.)
	{
		/* we dont want to try extreme values */
		strcpy(result, "  ???");
	}
	else if (v > 99.99)
	{
		sprintf(result, "%5.0f", v);
	}
	else
	{
		sprintf(result, "%5.2f", v);
	}

	return result;
}

/* format_time(seconds) - format number of seconds into a suitable
 *		display that will fit within 6 characters.	Note that this
 *		routine builds its string in a static area.  If it needs
 *		to be called more than once without overwriting previous data,
 *		then we will need to adopt a technique similar to the
 *		one used for format_k.
 */

/* Explanation:
   We want to keep the output within 6 characters.	For low values we use
   the format mm:ss.  For values that exceed 999:59, we switch to a format
   that displays hours and fractions:  hhh.tH.	For values that exceed
   999.9, we use hhhh.t and drop the "H" designator.  For values that
   exceed 9999.9, we use "???".
 */

char *
format_time(long seconds)
{
	static char result[10];

	/* sanity protection */
	if (seconds < 0 || seconds > (99999l * 360l))
	{
		strcpy(result, "   ???");
	}
	else if (seconds >= (1000l * 60l))
	{
		/* alternate (slow) method displaying hours and tenths */
		sprintf(result, "%5.1fH", (double) seconds / (double) (60l * 60l));

		/*
		 * It is possible that the sprintf took more than 6 characters. If so,
		 * then the "H" appears as result[6].  If not, then there is a \0 in
		 * result[6].  Either way, it is safe to step on.
		 */
		result[6] = '\0';
	}
	else
	{
		/* standard method produces MMM:SS */
		/* we avoid printf as must as possible to make this quick */
		sprintf(result, "%3ld:%02ld", seconds / 60l, seconds % 60l);
	}
	return (result);
}

#define NUM_STRINGS 8

/*
 * format_b(amt) - format a byte memory value, returning a string
 *		suitable for display.  Returns a pointer to a static
 *		area that changes each call.  "amt" is converted to a
 *		string with a trailing "B".  If "amt" is 10000 or greater,
 *		then it is formatted as megabytes (rounded) with a
 *		trailing "K".  And so on...
 */

char *
format_b(long long amt)
{
	static char retarray[NUM_STRINGS][32];
	static int	index = 0;
	register char *ret;
	register char tag = 'B';

	ret = retarray[index];
	index = (index + 1) % NUM_STRINGS;

	if (amt >= 10000)
	{
		amt = (amt + 512) / 1024;
		tag = 'K';
		if (amt >= 10000)
		{
			amt = (amt + 512) / 1024;
			tag = 'B';
			if (amt >= 10000)
			{
				amt = (amt + 512) / 1024;
				tag = 'G';
			}
		}
	}

	snprintf(ret, sizeof(retarray[index]) - 1, "%lld%c", amt, tag);

	return (ret);
}

/*
 * format_k(amt) - format a kilobyte memory value, returning a string
 *		suitable for display.  Returns a pointer to a static
 *		area that changes each call.  "amt" is converted to a
 *		string with a trailing "K".  If "amt" is 10000 or greater,
 *		then it is formatted as megabytes (rounded) with a
 *		trailing "M".
 */

/*
 * Compromise time.  We need to return a string, but we don't want the
 * caller to have to worry about freeing a dynamically allocated string.
 * Unfortunately, we can't just return a pointer to a static area as one
 * of the common uses of this function is in a large call to sprintf where
 * it might get invoked several times.	Our compromise is to maintain an
 * array of strings and cycle thru them with each invocation.  We make the
 * array large enough to handle the above mentioned case.  The constant
 * NUM_STRINGS defines the number of strings in this array:  we can tolerate
 * up to NUM_STRINGS calls before we start overwriting old information.
 * Keeping NUM_STRINGS a power of two will allow an intelligent optimizer
 * to convert the modulo operation into something quicker.	What a hack!
 */

char *
format_k(long amt)
{
	static char retarray[NUM_STRINGS][32];
	static int	index = 0;
	register char *ret;
	register char tag = 'K';

	ret = retarray[index];
	index = (index + 1) % NUM_STRINGS;

	if (amt >= 10000)
	{
		amt = (amt + 512) / 1024;
		tag = 'M';
		if (amt >= 10000)
		{
			amt = (amt + 512) / 1024;
			tag = 'G';
		}
	}

	snprintf(ret, sizeof(retarray[index]) - 1, "%ld%c", amt, tag);

	return (ret);
}

static int	debug_on = 0;

#ifdef DEBUG
FILE	   *debugfile;
#endif

void
debug_set(int i)
{
	debug_on = i;
#ifdef DEBUG
	debugfile = fopen("/tmp/top.debug", "w");
#endif
}

#ifdef DEBUG
void
xdprintf(char *fmt,...)
{
	va_list		argp;

	va_start(argp, fmt);

	if (debug_on)
	{
		vfprintf(debugfile, fmt, argp);
		fflush(stdout);
	}

	va_end(argp);
}

#endif
