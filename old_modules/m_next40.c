/*
 * top - a top users display for Unix
 * NEXTSTEP v.0.3  2/14/1996 tpugh
 *
 * SYNOPSIS:  any hppa or sparc NEXTSTEP v3.3 system
 *
 * DESCRIPTION:
 *	This is the machine-dependent module for NEXTSTEP v3.x/4.x
 *	Reported to work for:
 *		NEXTSTEP v3.2 on HP machines.
 *		NEXTSTEP v3.3 on HP and Sparc machines.
 * 	Has not been tested for NEXTSTEP v4.0 machines, although it should work.
 * 	Install "top" with the permissions 4755.
 *		tsunami# chmod 4755 top
 *		tsunami# ls -lg top
 *		-rwsr-xr-x  1 root     kmem      121408 Sep  1 10:14 top*
 *	With the kmem group sticky bit set, we can read kernal memory without problems,
 *	but to communicate with the Mach kernal for task and thread info, it requires
 *	root privileges.
 *
 * LIBS: 
 *
 * Need the compiler flag, "-DSHOW_UTT", to see the user task and thread task
 * data structures to report process info.
 * Need the compiler flag, "-DNEXTSTEP40", to use the proper task structure.
 * Need -I. for all the top include files which are searched for in machine/,
 * because of the way include "file" finds files.
 *
 * CFLAGS: -I. -DSHOW_UTT -DNEXTSTEP40
 *
 *
 * AUTHORS:		Tim Pugh <tpugh@oce.orst.edu>
 */

#include "machine/m_next32.c"
