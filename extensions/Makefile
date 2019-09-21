CC          = cc

SHELL       = /bin/sh
ECHO        = /bin/echo

DEPTH       = ..
BUILD       =
TARGETDIR   = obj

DEFINES     =
OTHER_FLAGS =
CFLAGS      =

OBJDIR      = $(DEPTH)/$(TARGETDIR)
INCDIR      = $(DEPTH)/$(TARGETDIR)

LIBRESOLV   =
EXTRALIBS   =

SLIBRARY    = $(OBJDIR)/libstx.a
OBJS        = $(OBJDIR)/dnscache.o $(OBJDIR)/dnsres.o $(OBJDIR)/lrucache.o


CFLAGS      += -Wall -I$(INCDIR)
AR          = ar
ARFLAGS     = rv
RANLIB      = ranlib


##########################
# Platform section.
#

ifeq (LINUX, $(findstring LINUX, $(OS)))
LIBRESOLV   = -lresolv
endif

ifeq ($(OS), SOLARIS)
LIBRESOLV   = -lresolv
EXTRALIBS   = -lsocket -lnsl
endif

#
# End of platform section.
##########################


all: $(SLIBRARY)

$(SLIBRARY): $(OBJS)
	$(AR) $(ARFLAGS) $@ $(OBJS)
	$(RANLIB) $@

$(OBJDIR)/%.o: %.c stx.h common.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJS) $(SLIBRARY)

#.DEFAULT:
#	@cd $(DEPTH); $(MAKE) $@

