pg_top
======

Installation
------------

Configuring
~~~~~~~~~~~

::

  cmake [options] CMakeLists.txt

options:

-DCMAKE_INSTALL_PREFIX=PREFIX   Install files in PREFIX.  Default is
                                '/usr/local'.
-DENABLE_COLOR=0                Default on.  Include code that allows for the
                                use of color in the output display.  Use
                                -DENABLE_COLOR=0 if you do not want this
                                feature compiled in to the code.  The configure
                                script also recognizes the spelling "colour".

Installing
~~~~~~~~~~

::

  make install

Uninstalling
~~~~~~~~~~~~

::

  xargs rm < install_manifest.txt
