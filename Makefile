DESTDIR :=
PREFIX  := /usr/local
BINDIR  := $(PREFIX)/bin
DATADIR := $(PREFIX)/share
MANDIR  := $(DATADIR)/man

CC ?= clang
CFLAGS = -std=c11 -D_XOPEN_SOURCE=700 -Wall -Wextra -pedantic -O2
LDFLAGS =
SOURCES = lambda.c
OBJECTS = lambda.o
LAMBDAPP = lambdapp

all: $(LAMBDAPP)

$(LAMBDAPP): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

install:
	install -d -m755                 $(DESTDIR)$(BINDIR)
	install    -m755 $(LAMBDAPP)     $(DESTDIR)$(BINDIR)/$(LAMBDAPP)
	install -d -m755                 $(DESTDIR)$(MANDIR)/man1
	install    -m644  doc/lambdapp.1 $(DESTDIR)$(MANDIR)/man1/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(LAMBDAPP)
	rm -f $(DESTDIR)$(MANDIR)/man1/lambdapp.1

check: $(LAMBDAPP)
	rm -f tests/test.log
	$(MAKE) -C tests

clean:
	rm -f $(OBJECTS)
	rm -f $(LAMBDAPP)
