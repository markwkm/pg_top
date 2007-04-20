/*
 * Top - a top users display for Unix
 *
 * Definition of the color interface.
 */

int color_env_parse(char *env);
int color_tag(char *tag);
int color_test(int tagidx, int value);
char *color_set(int color);
void color_dump(FILE *f);


/*
 * These color tag names are currently in use
 * (or reserved for future use):
 *
 * cpu, size, res, time, 1min, 5min, 15min, host
 */

/*
 * Valid ANSI values for colors are:
 *
 * 0	Reset all attributes
 * 1	Bright
 * 2	Dim
 * 4	Underscore	
 * 5	Blink
 * 7	Reverse
 * 8	Hidden
 * 
 * 	Foreground Colours
 * 30	Black
 * 31	Red
 * 32	Green
 * 33	Yellow
 * 34	Blue
 * 35	Magenta
 * 36	Cyan
 * 37	White
 * 
 * 	Background Colours
 * 40	Black
 * 41	Red
 * 42	Green
 * 43	Yellow
 * 44	Blue
 * 45	Magenta
 * 46	Cyan
 * 47	White
 */
