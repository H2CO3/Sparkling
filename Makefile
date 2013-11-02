# change this variable only! should be 'debug' or 'release' (without quotes)
BUILD ?= release

OPSYS = $(shell uname | tr '[[:upper:]]' '[[:lower:]]')

ifeq ($(OPSYS), darwin)
CC = clang -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.7.sdk
EXTRA_WARNINGS = -Wno-error=unused-function -Wno-error=sign-compare -Wno-error=logical-op-parentheses -Wimplicit-fallthrough -Wno-unused-parameter
LDFLAGS = -w
LTO_FLAG = -flto
else
CC = gcc
EXTRA_WARNINGS = -Wno-error=unused-function -Wno-error=sign-compare -Wno-error=parentheses -Wno-error=pointer-to-int-cast -Wno-unused-parameter
LDFLAGS = -lm
endif

LD = $(CC)

SRCDIR = src
OBJDIR = bld
DSTDIR ?= /usr/local

WARNINGS = -Wall -Wextra -Werror $(EXTRA_WARNINGS)
CFLAGS = -c -std=c89 -pedantic -pedantic-errors -fstrict-aliasing $(WARNINGS)

ifeq ($(BUILD), debug)
	CFLAGS += -O0 -g -pg -DDEBUG
	LDFLAGS += -O0 -g -pg
else
	CFLAGS += -O2 -DNDEBUG $(LTO_FLAG)
	LDFLAGS += -O2 $(LTO_FLAG)
endif

OBJECTS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(wildcard $(SRCDIR)/*.c))

LIB = $(OBJDIR)/libspn.a
REPL = $(OBJDIR)/spn

all: $(LIB) $(REPL)

$(LIB): $(OBJECTS)
	ar -cvr $@ $^

$(REPL): repl.o $(LIB)
	$(LD) -o $@ $^ $(LDFLAGS)

install: $(LIB) $(REPL)
	mkdir -p $(DSTDIR)/lib/
	cp $(LIB) $(DSTDIR)/lib/
	mkdir -p $(DSTDIR)/include/spn/
	cp $(SRCDIR)/*.h $(DSTDIR)/include/spn/
	mkdir -p $(DSTDIR)/bin/
	cp $(REPL) $(DSTDIR)/bin/

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<

repl.o: repl.c
	printf "#define REPL_VERSION \"%s\"\n" $(shell git rev-parse --short HEAD) > repl.h
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $<

clean:
	rm -f $(OBJECTS) $(LIB) $(REPL) repl.o repl.h gmon.out .DS_Store $(SRCDIR)/.DS_Store $(OBJDIR)/.DS_Store doc/.DS_Store examples/.DS_Store

.PHONY: all install clean

