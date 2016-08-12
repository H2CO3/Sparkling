# change this variable only! should be 'debug' or 'release' (without quotes)
BUILD ?= debug

# this should reflect if libedit is to be used. Defaulting to yes;
# if it doesn't compile with libedit enabled, then try turning it off.
LIBEDIT ?= 1

# if the terminal supports ANSI color codes, then the Sparkling REPL
# will attempt to print error messages and return values in color.
# Turn this off if your terminal doesn't understand ANSI color codes
# (khm, Windows, khm...)
ANSI_COLORS ?= 1

# if your operating system supports dynamic loading
# (most modern operating systems and Windows), then
# this flag enables the Sparkling engine to load
# modules defined as a dynamic library.
DYNAMIC_LOADING ?= 1

OPSYS = $(shell uname | tr '[[:upper:]]' '[[:lower:]]' | sed 's/.*\(mingw\).*/\1/g')
ARCH = $(shell uname -p | tr '[[:upper:]]' '[[:lower:]]')

ifeq ($(OPSYS), darwin)
	ifeq ($(ARCH), arm)
		SYSROOT = /var/mobile/iPhoneOS.sdk
	else
		SYSROOT = $(shell xcrun --show-sdk-path)
	endif

	CC = clang
	CFLAGS = -isysroot $(SYSROOT)
	EXTRA_WARNINGS = -Wno-error=unused-function -Wno-error=sign-compare -Wno-logical-op-parentheses -Wimplicit-fallthrough -Wno-unused-parameter -Wno-error=deprecated-declarations -Wno-missing-field-initializers
	LDFLAGS = -isysroot $(SYSROOT) -w
	DYNLDFLAGS = -isysroot $(SYSROOT) -w -dynamiclib
	LTO_FLAG = -flto
	DYNEXT = dylib
else
	CC = gcc
	EXTRA_WARNINGS = -Wno-error=unused-function -Wno-error=sign-compare -Wno-parentheses -Wno-error=pointer-to-int-cast -Wno-error=uninitialized -Wno-unused-parameter -Wno-missing-field-initializers -Wno-error=pedantic
	LIBS = -lm
	LDFLAGS = -lrt -ldl -rdynamic
	DYNLDFLAGS = -lm -lrt -ldl -shared
	DYNEXT = so
	DEFINES += -D_XOPEN_SOURCE=700
endif

ifeq ($(OPSYS), mingw)
	SPARKLING_LIBDIR = $(shell echo "%SystemDrive%/Sparkling/")
else
	SPARKLING_LIBDIR = "$(DSTDIR)/lib/sparkling/"
endif

DEFINES += -DSPARKLING_LIBDIR_RAW=$(SPARKLING_LIBDIR)

LD = $(CC)

SRCDIR = src
OBJDIR = bld
LIBDIR = lib
DSTDIR ?= /usr/local

WARNINGS = -Wall -Wextra -Werror $(EXTRA_WARNINGS)
CFLAGS += -c -std=c89 -pedantic -fpic -fstrict-aliasing $(WARNINGS) $(DEFINES)

# Enable/disable user-defined features
ifneq ($(LIBEDIT), 0)
	DEFINES += -DUSE_LIBEDIT=1
	LIBS += -ledit
else
	DEFINES += -DUSE_LIBEDIT=0
endif

ifneq ($(ANSI_COLORS), 0)
	DEFINES += -DUSE_ANSI_COLORS=1
else
	DEFINES += -DUSE_ANSI_COLORS=0
endif

ifneq ($(DYNAMIC_LOADING), 0)
	DEFINES += -DUSE_DYNAMIC_LOADING=1
else
	DEFINES += -DUSE_DYNAMIC_LOADING=0
endif

ifeq ($(BUILD), debug)
	CFLAGS += -O0 -g -pg -DDEBUG
	LDFLAGS += -O0 -g -pg
	DYNLDFLAGS += -O0 -g -pg
else
	CFLAGS += -O3 -DNDEBUG $(LTO_FLAG)
	LDFLAGS += -O3 $(LTO_FLAG)
	DYNLDFLAGS += -O3 $(LTO_FLAG)
endif

OBJECTS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(wildcard $(SRCDIR)/*.c))

LIB = $(OBJDIR)/libspn.a
DYNLIB = $(OBJDIR)/libspn.$(DYNEXT)
REPL = $(OBJDIR)/spn

all: $(LIB) $(DYNLIB) $(REPL)

$(LIB): $(OBJECTS)
	ar -cvr $@ $^

$(DYNLIB): $(OBJECTS)
	$(LD) -o $@ $^ $(DYNLDFLAGS)

$(REPL): repl.o dump.o $(OBJECTS)
	$(LD) -o $@ $^ $(LDFLAGS) $(LIBS)

install: all
	mkdir -p $(DSTDIR)/lib/
	mkdir -p $(SPARKLING_LIBDIR)
	cp $(LIB) $(DSTDIR)/lib/
	cp $(DYNLIB) $(DSTDIR)/lib/
	cp $(LIBDIR)/*.spn $(SPARKLING_LIBDIR)
	mkdir -p $(DSTDIR)/include/spn/
	cp $(SRCDIR)/*.h $(DSTDIR)/include/spn/
	mkdir -p $(DSTDIR)/bin/
	cp $(REPL) $(DSTDIR)/bin/

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<

repl.o: repl.c
	printf "#define REPL_VERSION \"%s\"\n" $(shell git rev-parse --short HEAD) > repl.h
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $<

dump.o: dump.c
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $<

# Script standard library loader
src/ctx.c: src/stdmodules.inc

src/stdmodules.inc:
	find $(LIBDIR) -name "*.spn" -exec basename {} \; \
	| awk '{ print "\"" $(SPARKLING_LIBDIR) "/" $$0 "\"," }' > $@

clean:
	rm -f $(OBJECTS) $(LIB) $(DYNLIB) $(REPL) \
		repl.o repl.h dump.o gmon.out \
		src/stdmodules.inc \
		.DS_Store \
		$(SRCDIR)/.DS_Store \
		$(OBJDIR)/.DS_Store \
		doc/.DS_Store \
		examples/.DS_Store \
		*~ src/*~

test:
	VALGRIND="" ./runtests.sh

test-valgrind:
	VALGRIND="valgrind --quiet --leak-check=full --show-leak-kinds=definite,possible,indirect --leak-check-heuristics=all --dsymutil=yes" ./runtests.sh


.PHONY: all install clean test test-valgrind
# DO NOT DELETE
