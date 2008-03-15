AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = $(DBCFLAGS)
AM_LDFLAGS = $(DBLDFLAGS)

sigdesc.h: sigconv.awk $(SIGNAL_H)
	$(AWK) -f $(srcdir)/sigconv.awk $(SIGNAL_H) > sigdesc.h 

bin_PROGRAMS = ptop

ptop_SOURCES = color.c commands.c display.c getopt.c screen.c sprompt.c pg.c \
		ptop.c username.c utils.c version.c
EXTRA_ptop_SOURCES = \
		machine/m_aix43.c \
		machine/m_aix5.c \
		machine/m_decosf1.c \
		machine/m_freebsd.c \
		machine/m_hpux10.c \
		machine/m_hpux7.c \
		machine/m_hpux8.c \
		machine/m_hpux9.c \
		machine/m_irix5.c \
		machine/m_irixsgi.c \
		machine/m_linux.c \
		machine/m_linuxthr.c \
		machine/m_macosx.c \
		machine/m_netbsd.c \
		machine/m_openbsd.c \
		machine/m_sco5.c \
		machine/m_sunos4.c \
		machine/m_sunos5.c \
		machine/m_svr4.c \
		machine/m_svr5.c
ptop_LDADD = @OBJ@
ptop_DEPENDENCIES = @OBJ@

man_MANS = ptop.1

EXTRA_DIST = \
		FAQ \
		HISTORY \
		INSTALL \
		LICENSE \
		Porting \
		README \
		TODO \
		Y2K \
		boolean.h \
		c.h \
		color.h \
		commands.h \
		configure \
		display.h \
		help.h \
		layout.h \
		loadavg.h \
		machine.h \
		message.h \
		os.h \
		pg.h \
		pg_trace.h \
		port.h \
		ptop.h \
		screen.h \
		sigconv.awk \
		sigdesc.h \
		username.h \
		utils.h \
		version.h \
		machine/m_decosf1.man \
		machine/m_freebsd.man \
		machine/m_linux.man \
		machine/m_netbsd.man \
		machine/m_sco5.man \
		machine/m_svr4.man \
		machine/m_svr5.man \
		machine/m_hpux9.man \
		machine/m_hpux10.man \
		machine/m_linuxthr.man \
		machine/m_sunos5.man \
		machine/m_macosx.man \
		machine/m_sunos4.man
