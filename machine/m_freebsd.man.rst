FreeBSD 4.0 NOTES
=================

Last pid is compiler depended. 

$ strings /kernel | grep _nextpid

DESCRIPTION OF MEMORY
=====================

Memory: 10M Act 1208K Inact 3220K Wired 132K Free 25% Swap, 2924Kin 2604Kout

:K:: Kilobyte
:M:: Megabyte
:%:: 1/100
:Act:: number of pages active
:Incat:: number of pages inactive
:Wired:: number of pages wired down
:Free:: number of pages free
:Swap:: swap usage
:Kin:: kilobytes swap pager pages paged in (last interval)
:Kout:: kilobytes swap pager pages paged out  (last interval)

See /usr/include/sys/vmmeter.h and  /sys/vm/vm_meter.c.

Christos Zoulas, Steven Wallace, Wolfram Schneider, Monte Mitzelfelt.

This module was retrofitted from FreeBSD 9.1 sources.
