# Makefile for xhisper

CC = gcc
CFLAGS = -O2 -Wall -Wextra
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

all: xhispertool xhisper-keyd test

xhispertool: xhispertool.c
	$(CC) $(CFLAGS) xhispertool.c -o xhispertool
	ln -sf xhispertool xhispertoold

xhisper-keyd: xhisper-keyd.c
	$(CC) $(CFLAGS) xhisper-keyd.c -o xhisper-keyd

test: test.c
	$(CC) $(CFLAGS) test.c -o test

install: xhispertool xhisper-keyd xhisper.sh
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 xhispertool $(DESTDIR)$(BINDIR)/xhispertool
	ln -sf xhispertool $(DESTDIR)$(BINDIR)/xhispertoold
	install -m 755 xhisper.sh $(DESTDIR)$(BINDIR)/xhisper
	install -m 755 xhisper-keyd $(DESTDIR)$(BINDIR)/xhisper-keyd

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/xhisper
	rm -f $(DESTDIR)$(BINDIR)/xhispertool
	rm -f $(DESTDIR)$(BINDIR)/xhispertoold
	rm -f $(DESTDIR)$(BINDIR)/xhisper-keyd

clean:
	rm -f xhispertool xhispertoold xhisper-keyd test

.PHONY: all install uninstall clean
