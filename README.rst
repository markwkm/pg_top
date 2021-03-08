pg_top
======

pg_top is 'top' for PostgreSQL. It is derived from Unix Top. Similar to top,
pg_top allows you to monitor PostgreSQL processes. It also allows you to:

* View currently running SQL statement of a process.
* View query plan of a currently running SELECT statement.
* View locks held by a process.
* View I/O statistics per process.
* View replication statistics for downstream nodes.

To compile and install "pg_top", read the file "INSTALL.rst" and follow the
directions and advice contained therein.

If you make any kind of change to "pg_top" that you feel would be
beneficial to others who use this program, or if you find and fix a bug,
please submit the change to the pg_top issue tracker:

  https://gitlab.com/pg_top/pg_top/issues

In order to monitor a remote database, the pg_proctab extension needs to be
created on the database to be monitored.  Any operating system that pg_proctab
supports can be monitored remotely on any operating system.  See details for
pg_proctab here:

  https://gitlab.com/pg_proctab/pg_proctab

Availability
------------

Project home page:

  https://pg_top.gitlab.io/


If you have git, you can download the source code::

  git clone git@gitlab.com:pg_top/pg_top.git

Logo
----

The logo is just a pipe (|) and a capital V using the free Ampad Brush true
type font.

Gratitude
---------

Selena Deckelmann & Gabrielle Roth, and the beer & free wi-fi at County Cork
pub in Portland, OR, USA.

License
-------

pg_top is distributed free of charge under the same terms as the BSD
license.  For an official statement, please refer to the file "LICENSE"
which should be included with the source distribution.
