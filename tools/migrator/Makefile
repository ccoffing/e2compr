MAKEDEPEND=makedepend
CFLAGS=-O2 -Wall -g3
CC=gcc
STRIP=strip
INSTALL=/usr/bin/install -c
UPROGS=migrator cattr
UETC=migrator.conf
USR_PREFIX = /usr/local
USR_BINDIR = $(USR_PREFIX)/bin
USR_ETCDIR = $(USR_PREFIX)/etc

all:depend migrator cattr

migrator: migrator.c list.c Makefile
	$(CC) $(CFLAGS) $(INCS) -o $@ migrator.c list.c

cattr: cattr.c Makefile
	$(CC) $(CFLAGS) $(INCS) -o $@ cattr.c

clean:
	rm -f core *.o *.a .depend migrator cattr

install: $(UPROGS)
	for i in $(UPROGS); do \
		$(INSTALL) $$i $(USR_BINDIR)/$$i; \
		$(STRIP) $(USR_BINDIR)/$$i; \
	done
	for i in $(UETC); do \
		$(INSTALL) -m 0644 $$i $(USR_ETCDIR)/$$i; \
	done

depend:
	rm -f .depend
	touch .depend
	$(MAKEDEPEND) $(INCS) -f .depend *.cc *.hh *.c *.h	

include .depend
