DESTDIR :=
PREFIX  := /usr/local
BINDIR  := $(PREFIX)/bin
DATADIR := $(PREFIX)/share
MANDIR  := $(DATADIR)/man

CC ?= clang
CFLAGS = -std=c11 -D_BSD_SOURCE -Wall -Wextra -pedantic -O2
LDFLAGS =
PP_SOURCES = lambda-pp.c
PP_OBJECTS = lambda-pp.o
CC_SOURCES = lambda-cc.c
CC_OBJECTS = lambda-cc.o
LAMBDA_PP = lambda-pp
LAMBDA_CC = lambda-cc

all: $(LAMBDA_PP) $(LAMBDA_CC)

$(LAMBDA_PP): $(PP_OBJECTS)
	$(CC) $(PP_OBJECTS) -o $@ $(LDFLAGS)

$(LAMBDA_CC): $(CC_OBJECTS)
	$(CC) $(CC_OBJECTS) -o $@ $(LDFLAGS)

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

install:
	install -d -m755                 $(DESTDIR)$(BINDIR)
	install    -m755 $(LAMBDA_PP)    $(DESTDIR)$(BINDIR)/$(LAMBDA_PP)
	install	   -m755 $(LAMBDA_CC)	 $(DESTDIR)$(BINDIR)/$(LAMBDA_CC)
	install -d -m755                 $(DESTDIR)$(MANDIR)/man1
	install    -m644  doc/lambdapp.1 $(DESTDIR)$(MANDIR)/man1/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(LAMBDA_PP)
	rm -f $(DESTDIR)$(BINDIR)/$(LAMBDA_CC)
	rm -f $(DESTDIR)$(MANDIR)/man1/lambdapp.1

check: $(LAMBDA_PP)
	rm -f tests/test.log
	$(MAKE) -C tests

clean:
	rm -f $(PP_OBJECTS)
	rm -f $(CC_OBJECTS)
	rm -f $(LAMBDA_PP)
	rm -f $(LAMBDA_CC)
