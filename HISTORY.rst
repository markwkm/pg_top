Release Notes
=============

YYYY-MM-DD v4.0.1
-----------------

* Set CMake minimum version to 2.8.12
* Add container files for creating an AppImage using AppImageKit v13
* Update AppImage notes
* Fix string truncation warnings in byte and kilobyte pretty functions
* Replace reallocarray() with POSIX realloc()
* Fix escaping in CMakeLists.txt AppImage target
* Removed building of sigdesc.h, since kill commands was removed in v4.0.0
* Fix CMake handling of building getopt.c when not found
* Bump CMake minimum to 3.1.0

2020-08-05 v4.0.0
-----------------

* Replace autoconf with cmake
* Remove table stats monitoring, use pg_systat instead
  https://pg_systat.gitlab.io/
* Remove index stats monitoring, use pg_systat instead
  https://pg_systat.gitlab.io/
* Remove kill and renice command, and stop displaying nice priority
* Show backend state instead of operating system state
* Show database username instead of operating system username
* Make connections with password persistent and clear connection memory in
  these scenarios
* Fix sorting when specified from the command line and in interactive mode
* Add display for top replication processes using 'R' command line argument or
  'R' command
* Add 'a' command to switch to top 'activity' processes, the default, while
  removing the 'I' for the top I/O display as a toggle
* Show swapping activity
* Simplify top I/O display
* Remove toggle for display raw I/O statistics
* Fix bug for specifing number of displays to show on the command line without
  -x flag
* Fix view of locks held by process.  Separate table and index locks.  Also
  showing schema.
* Handle longer pids on Linux
* Updated for FreeBSD 12.
* Updated for OpenBSD 6.7.

2013-07-31 v3.7.0
-----------------

* Added support for monitoring databases on remote systems.
* Added support for monitoring i/o statistics on Linux.
* Updated for changed introduced in PostgreSQL 9.2.
* Updated for OpenBSD 5.2.
* Updated for FreeBSD 9.1.
* Updated for OS X Mountain Lion (10.8).
* Add monitoring for database activity
* Add monitoring for disk activity
* Add monitoring for disk space
* Add long options

2008-05-03 v3.6.2
-----------------

* Add 'A' command to re-run SQL statement and show actual execution plan
  (EXPLAIN ANALYZE) of a running query.
* Fixed 'E' command (EXPLAIN) to be UPDATE and INSERT safe.
* Updated the automake file so other targets like 'make dist' and 'make
  distdir' work.
* Fixed a bug so user table statistics can be sorted.
* Added a 't' command so that user table and index statistics can display
  either cumulative or differential statistics.
* Fixed support for OS X, tested on v10.4.x, v10.5.x.
* Added support for OpenBSD, tested on v4.2.
* Rename 'ptop' to 'pg_top' to fit PostgreSQL naming conventions and avoid
  naming conflict with free pascal's source formatter 'ptop'.
* Recognize PGDATABASE, PGHOST, PGUSER, and PGPORT environment variables.

2008-03-05 v3.6.1
-----------------

* Add -h command line option to specify a socket file when connected to the
  database..
* Use the same -p PORT, -U USER, and -d DBNAME options as other PostgreSQL
  programs.
* Change unixtop's original -d to -x, and -U to -z.
* Add 'X' command to view user index statistics.
* Add 'R' command to view user table statistics.
* Add support for Solaris 10.
* Add support for FreeBSD.
* Add 'E' command to re-determine and show execution plan of a running SQL
  statement.
* Add parameters to specify database connection information.
* Add 'L' command to show locks held by a process.
* Add 'Q' command to show current query of a process.
* Rename 'top' to 'ptop'.
* Add support for Linux.
* Configure support for PostgreSQL libpq client libraries.
* Remove old_modules directory.
* Update RES calculation for Linux 2.6.x.
