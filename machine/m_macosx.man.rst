MacOS X NOTES
=============

The display is pretty close to the recommended display and also that
of a normal 4.4 BSD system.  The NICE column has been changed to be
the number of threads for each process.  The SIZE column reflects the
total size of the process (resident + non-resident) while the RES
column shows only the resident size.  The STATE column uses
information taken from the kinfo_proc structure p_pstat member.  It
will accurately display the state of stopped and zombie processes, but
I am not really sure about the other states.  Finally, the MEM column
is included which displays the percent of total memory per the ps
command.

The MacOS X module was written by Andrew S. Townley <atownley@primenet.com>.
Many thanks to William LeFebvre who is the original author
of the top utility and to Mike Rhee who showed the utility
to me in the first place.  Thanks also to Christos Zoulas
who wrote the 4.4 BSD implementation of the machine module.
I also got some pointers from the NEXTSTEP 3.2 and OSF/1
versions by Tim Pugh and Anthony Baxter, respectively.

