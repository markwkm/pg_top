/*
 *	Top - a top users display for Unix
 *
 *	This file defines the default locations on the screen for various parts
 *	of the display.  These definitions are used by the routines in "display.c"
 *	for cursor addressing.
 */

#ifndef _LAYOUT_H_
#define _LAYOUT_H_

#define  X_LASTPID	10
#define  Y_LASTPID	0
#define  X_LASTPIDWIDTH 13
#define  X_LOADAVE	27
#define  Y_LOADAVE	0
#define  X_LOADAVEWIDTH 7
#define  X_MINIBAR		50
#define  Y_MINIBAR		0
#define  X_UPTIME		53
#define  Y_UPTIME		0
#define  X_PROCSTATE	15
#define  Y_PROCSTATE	1
#define  X_BRKDN	15
#define  Y_BRKDN	1
#define  X_CPUSTATES	0
#define  Y_CPUSTATES	2
#define  X_MEM		8
#define  Y_MEM		3
#define  X_DB		0
#define  Y_DB		4
#define  X_IO		0
#define  Y_IO		5
#define  X_SWAP		6
#define  Y_SWAP		6
#define  Y_MESSAGE	6
#define  X_HEADER	0
#define  Y_HEADER	7
#define  X_IDLECURSOR	0
#define  Y_IDLECURSOR	6
#define  Y_PROCS	8

#endif							/* _LAYOUT_H_ */
