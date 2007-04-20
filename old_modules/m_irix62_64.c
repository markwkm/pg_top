/*
 * top - a top users display for Unix
 *
 * SYNOPSIS:  any SGI machine running IRIX 6.2 with a 64-bit kernel
 *
 * DESCRIPTION:
 * This is the machine-dependent module for IRIX 6.2.
 * It has been tested on a Power Challenge/L running IRIX 6.2.
 *
 * LIBS: -lelf
 *
 * Need -I. for all the top include files which are searched for in machine/,
 * because of the way include "file" finds files.
 *
 * CFLAGS: -I. -DHAVE_GETOPT -DIRIX64
 *
 * AUTHOR: Rainer Orth <ro@TechFak.Uni-Bielefeld.DE>
 *
 */
#include "machine/m_irix62.c"
