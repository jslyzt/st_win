##########################
# Supported OSes:
#
# AIX
# FREEBSD
# HPUX
# HPUX_64
# IRIX
# IRIX_64
# LINUX
# LINUX_IA64
# NETBSD
# OPENBSD
# OSF1
# SOLARIS
# SOLARIS_64

##########################

CC          = cc

SHELL       = /bin/sh
ECHO        = /bin/echo

DEPTH       = ..
BUILD       =
TARGETDIR   =

DEFINES     =
CFLAGS      =
OTHER_FLAGS =

OBJDIR      = $(DEPTH)/$(TARGETDIR)
INCDIR      = $(DEPTH)/$(TARGETDIR)
LIBST       = $(OBJDIR)/libst.a
HEADER      = $(INCDIR)/st.h

LIBRESOLV   =
EXTRALIBS   =

ifeq ($(OS),)
EXAMPLES    = unknown
else
EXAMPLES    = $(OBJDIR)/lookupdns $(OBJDIR)/proxy $(OBJDIR)/server
endif


##########################
# Platform section.
#

ifeq (DARWIN, $(findstring DARWIN, $(OS)))
LIBRESOLV   = -lresolv
endif

ifeq (LINUX, $(findstring LINUX, $(OS)))
LIBRESOLV   = -lresolv
endif

ifeq (SOLARIS, $(findstring SOLARIS, $(OS)))
LIBRESOLV   = -lresolv
EXTRALIBS   = -lsocket -lnsl
endif

#
# End of platform section.
##########################


all: $(EXAMPLES)

$(OBJDIR)/lookupdns: lookupdns.c $(OBJDIR)/res.o $(LIBST) $(HEADER)
	$(CC) $(CFLAGS) -I$(INCDIR) lookupdns.c $(OBJDIR)/res.o $(LIBST) $(LIBRESOLV) $(EXTRALIBS) -o $@

$(OBJDIR)/proxy: proxy.c $(LIBST) $(HEADER)
	$(CC) $(CFLAGS) -I$(INCDIR) proxy.c $(LIBST) $(EXTRALIBS) -o $@

$(OBJDIR)/server: server.c $(OBJDIR)/error.o $(LIBST) $(HEADER)
	$(CC) $(CFLAGS) -I$(INCDIR) server.c $(OBJDIR)/error.o $(LIBST) $(EXTRALIBS) -o $@

$(OBJDIR)/%.o: %.c
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

.DEFAULT:
	@cd $(DEPTH); $(MAKE) $@

