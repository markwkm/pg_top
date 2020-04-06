SUNOS 5 NOTES
=============

CPU percentage is calculated as a fraction of total available computing
resources.  Hence on a multiprocessor machine a single threaded process 
can never consume cpu time in excess of 1 divided by the number of processors.
For example, on a 4 processor machine, a single threaded process will 
never show a cpu percentage higher than 25%.  The CPU percentage column
will always total approximately 100, regardless of the number of processors.

The memory summary line displays the following: "phys mem" is the total
amount of physical memory that can be allocated for use by processes
(it does not include memory reserved for the kernel's use), "free mem" is
the amount of unallocated physical memory, "total swap" is the amount
of swap area on disk that is being used, "free swap" is the amount of
swap area on disk that is still available.  Unlike previous versions of
*pg_top*, The swap figures will differ from the summary output of *swap (1M)*
since the latter includes physical memory as well.

The column "THR" indicates the number of execution threads in the process.

In BSD Unix, process priority was represented internally as a signed
offset from a zero value with an unsigned value.  The "zero" value
was usually something like 20, allowing for a range of priorities
from -20 to 20.  As implemented on SunOS 5, older versions of top
continued to interpret process priority in this manner, even though
it was no longer correct.  Starting with top version 3.5, this was
changed to agree with the rest of the system.

The SunOS 5 (Solaris 2) port was originally written by Torsten Kasch,
<torsten@techfak.uni-bielefeld.de>.  Many contributions have been
provided by Casper Dik <Casper.Dik@sun.com>.
Support for multi-cpu, calculation of CPU% and memory stats provided by
Robert Boucher <boucher@sofkin.ca>, Marc Cohen <marc@aai.com>, 
Charles Hedrick <hedrick@geneva.rutgers.edu>, and
William L. Jones <jones@chpc>.
