/*
 *  Top users/processes display for Unix
 *  Version 3
 *
 *  This program may be freely redistributed,
 *  but this entire comment MUST remain intact.
 */

/*
 * This file handles color definitions and access for augmenting
 * the output with ansi color sequences.
 *
 * The definition of a color setting is as follows, separated by
 * colons:
 *
 * tag=minimum,maximum#code
 *
 * "tag" is the name of the value to display with color.
 *
 * "minimum" and "maximum" are positive integer values defining a range:
 * when the value is within this range it will be shown with the
 * specified color.  A missing value indicates that no check should be
 * made (i.e.: ",25" is n <= 25; "25,50" is 25 <= n <= 50; and "50,"
 * is 50 <= n).
 * 
 * "code" is the ansi sequence that defines the color to use with the
 * escape sequence "[m".  Semi-colons are allowed in this string to
 * combine attributes.
 */

#include "os.h"
#include "message.h"

typedef struct color_entry {
    char *tag;
    int min;
    int max;
    char color;
    struct color_entry *next;
    struct color_entry *tagnext;
} color_entry;

static color_entry *entries = NULL;

static color_entry **bytag = NULL;
static char **bytag_names = NULL;
static int totaltags = 0;
static int tagcnt = 0;

static char **color_ansi = NULL;
static int num_color_ansi = 0;
static int max_color_ansi = 0;

#define COLOR_ANSI_SLOTS 20

static int
color_slot(char *str)

{
    int i;

    for (i = 0; i < num_color_ansi; i++)
    {
	if (strcmp(color_ansi[i], str) == 0)
	{
	    return i;
	}
    }

    /* need a new slot */
    if (num_color_ansi >= max_color_ansi)
    {
	max_color_ansi += COLOR_ANSI_SLOTS;
	color_ansi = (char **)realloc(color_ansi, max_color_ansi * sizeof(char *));
    }
    color_ansi[num_color_ansi] = strdup(str);
    return num_color_ansi++;
}

/*
 * int color_env_parse(char *env)
 *
 * Parse a color specification "env" (such as one found in the environment) and
 * add them to the list of entries.  Always returns 0.  Should only be called
 * once.
 */

int
color_env_parse(char *env)

{
    char *p;
    char *min;
    char *max;
    char *str;
    int len;
    color_entry *ce;

    /* initialization */
    color_ansi = (char **)malloc(COLOR_ANSI_SLOTS * sizeof(char *));
    max_color_ansi = COLOR_ANSI_SLOTS;

    /* color slot 0 is always "0" */
    color_slot("0");

    if (env != NULL)
    {
	p = strtok(env, ":");
	while (p != NULL)
	{
	    if ((min = strchr(p, '=')) != NULL &&
		(max = strchr(min, ',')) != NULL &&
		(str = strchr(max, '#')) != NULL)
	    {
		ce = (color_entry *)malloc(sizeof(color_entry));
		len = min - p;
		ce->tag = (char *)malloc(len + 1);
		strncpy(ce->tag, p, len);
		ce->tag[len] = '\0';
		ce->min = atoi(++min);
		ce->max = atoi(++max);
		ce->color = color_slot(++str);
		ce->next = entries;
		entries = ce;
	    }
	    else
	    {
		if (min != NULL)
		{
		    len = min - p;
		}
		else
		{
		    len = strlen(p);
		}
		error_message(" %.*s: bad color entry", len, p);
	    }
	    p = strtok(NULL, ":");
	}
    }
    return 0;
}

/*
 * int color_tag(char *tag)
 *
 * Declare "tag" as a color tag.  Return a tag index to use when testing
 * a valuse against the tests for this tag.  Should not be called before
 * color_env_parse.
 */

int
color_tag(char *tag)

{
    color_entry *entryp;
    color_entry *tp;

    if (tag == NULL || *tag == '\0')
    {
	return -1;
    }

    if (bytag == NULL)
    {
	totaltags = 10;
	bytag = (color_entry **)malloc(totaltags * sizeof(color_entry *));
	bytag_names = (char **)malloc(totaltags * sizeof(char *));
    }

    if (tagcnt >= totaltags)
    {
	totaltags *= 2;
	bytag = (color_entry **)realloc(bytag, totaltags * sizeof(color_entry *));
	bytag_names = (char **)realloc(bytag_names, totaltags * sizeof(char *));
    }

    entryp = entries;
    tp = NULL;

    while (entryp != NULL)
    {
	if (strcmp(entryp->tag, tag) == 0)
	{
	    entryp->tagnext = tp;
	    tp = entryp;
	}
	entryp = entryp->next;
    }

    bytag[tagcnt] = tp;
    bytag_names[tagcnt] = strdup(tag);
    return (tagcnt++);
}

/*
 * int color_test(int tagidx, int value)
 *
 * Test "value" against tests for tag "tagidx", a number previously returned
 * by color_tag.  Return the correct color number to use when highlighting.
 * If there is no match, return 0 (color 0).
 */

int
color_test(int tagidx, int value)

{
    color_entry *ce;

    /* sanity check */
    if (tagidx < 0 || tagidx >= tagcnt)
    {
	return 0;
    }

    ce = bytag[tagidx];

    while (ce != NULL)
    {
	if ((!ce->min || ce->min <= value) &&
	    (!ce->max || ce->max >= value))
	{
	    return ce->color;
	}
	ce = ce->tagnext;
    }

    return 0;
}

/*
 * char *color_set(int color)
 *
 * Return ANSI string to set the terminal for color number "color".
 */

char *
color_set(int color)

{
    static char v[32];

    v[0] = '\0';
    if (color >= 0 && color < num_color_ansi)
    {
	snprintf(v, sizeof(v), "\033[%sm", color_ansi[color]);
    }
    return v;
}

void
color_dump(FILE *f)

{
    color_entry *ep;
    int i;
    int col;
    int len;

    fputs("These color tags are available:", f);
    col = 81;
    for (i = 0; i < tagcnt; i++)
    {
	len = strlen(bytag_names[i]) + 1;
	if (len + col > 79)
	{
	    fputs("\n  ", f);
	    col = 2;
	}
	fprintf(f, " %s", bytag_names[i]);
	col += len;
    }

    fputs("\n\nTop color settings:\n", f);

    for (i = 0; i < tagcnt; i++)
    {
	ep = bytag[i];
	while (ep != NULL)
	{
	    fprintf(f, "   %s (%d-", ep->tag, ep->min);
	    if (ep->max != 0)
	    {
		fprintf(f, "%d", ep->max);
	    }
	    fprintf(f, "): ansi color %s, %sSample Text",
		    color_ansi[(int)ep->color],
		    color_set(ep->color));
	    fprintf(f, "%s\n", color_set(0));
	    ep = ep -> tagnext;
	}
    }
}

void
color_debug(FILE *f)

{
    color_entry *ep;
    int i;

    printf("color debug dump\n");
    ep = entries;
    while (ep != NULL)
    {
	printf("%s(%d,%d): slot %d, ansi %s, %sSample Text",
	       ep->tag, ep->min, ep->max, ep->color, color_ansi[(int)ep->color],
	       color_set(ep->color));
	printf("%s\n", color_set(0));
	ep = ep -> next;
    }

    printf("\ntags:");
    for (i = 0; i < tagcnt; i++)
    {
	printf(" %s", bytag_names[i]);
    }
    printf("\n");
}

