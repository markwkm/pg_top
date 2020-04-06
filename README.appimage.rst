AppImages are only for Linux based systems:

  https://appimage.org/

The AppImageKit AppImage can be downloaded from:

  https://github.com/AppImage/AppImageKit/releases

It is recommended to build AppImages on older distributions:

  https://docs.appimage.org/introduction/concepts.html#build-on-old-systems-run-on-newer-systems

At the time of this document, CentOS 6 is the one of the oldest supported Linux
distributions with the oldest libc version.

Use a custom configured PostgreSQL build with minimal options enabled to reduce
library dependency support.  Part of this reason is to make it easier to
include libraries with compatible licences.  Part of the reason is to be able
to build libpq with minimal development dependencies for PostgreSQL, since the
purpose is just to build a pg_top AppImage.  At least version PostgreSQL 10
should be used so that PGHOST can be defined in the AppRun script to search
multiple potential unixsock directories if no PGHOST is defined in the user's
environment.

At the time of this document, PostgreSQL 10 was configured with the following
options::

  ./configure --without-ldap --without-readline --without-zlib --without-gssapi

Don't forget you may have to set both PATH and LD_LIBRARY_PATH appropriately
depending on where the custom build of PostgreSQL is installed.
