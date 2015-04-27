# change this variable only! should be 'debug' or 'release' (without quotes)
BUILD ?= debug

# this should reflect if libreadline is to be used. Defaulting to yes;
# if it doesn't compile with readline enabled, then try turning it off.
READLINE ?= 1

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

OPSYS = $(shell uname | tr '[[:upper:]]' '[[:lower:]]')
ARCH = $(shell uname -p | tr '[[:upper:]]' '[[:lower:]]')

ifeq ($(OPSYS), darwin)
	ifeq ($(ARCH), arm)
		SYSROOT = /var/mobile/iPhoneOS6.1.sdk
	else
		SYSROOT = /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.9.sdk
	endif

	CC = clang
	CFLAGS = -isysroot $(SYSROOT)
	EXTRA_WARNINGS = -Wno-error=unused-function -Wno-error=sign-compare -Wno-error=logical-op-parentheses -Wimplicit-fallthrough -Wno-unused-parameter -Wno-error-deprecated-declarations -Wno-error=missing-field-initializers
	LDFLAGS = -isysroot $(SYSROOT) -w
	DYNLDFLAGS = -isysroot $(SYSROOT) -w -dynamiclib
	LTO_FLAG = -flto
	DYNEXT = dylib
else
	CC = gcc
	EXTRA_WARNINGS = -Wno-error=unused-function -Wno-error=sign-compare -Wno-error=parentheses -Wno-error=pointer-to-int-cast -Wno-error=uninitialized -Wno-unused-parameter -Wno-error=missing-field-initializers
	LIBS = -lm
	LDFLAGS = -lrt -ldl
	DYNLDFLAGS = -lm -lrt -ldl -shared
	DYNEXT = so
	DEFINES += -D_XOPEN_SOURCE=700
endif

LD = $(CC)

SRCDIR = src
OBJDIR = bld
DSTDIR ?= /usr/local

WARNINGS = -Wall -Wextra -Werror $(EXTRA_WARNINGS)
CFLAGS += -c -std=c89 -pedantic -fpic -fstrict-aliasing $(WARNINGS) $(DEFINES)

# Enable/disable user-defined features
ifneq ($(READLINE), 0)
	DEFINES += -DUSE_READLINE=1
	LIBS += -lreadline
else
	DEFINES += -DUSE_READLINE=0
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

$(REPL): spn.o dump.o $(OBJECTS)
	$(LD) -o $@ $^ $(LDFLAGS) $(LIBS)

install: all
	mkdir -p $(DSTDIR)/lib/
	cp $(LIB) $(DSTDIR)/lib/
	cp $(DYNLIB) $(DSTDIR)/lib/
	mkdir -p $(DSTDIR)/include/spn/
	cp $(SRCDIR)/*.h $(DSTDIR)/include/spn/
	mkdir -p $(DSTDIR)/bin/
	cp $(REPL) $(DSTDIR)/bin/

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<

spn.o: spn.c
	printf "#define REPL_VERSION \"%s\"\n" $(shell git rev-parse --short HEAD) > spn.h
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $<

dump.o: dump.c
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $<

# AST validator
src/rtlb.c: src/verifyast.inc

src/verifyast.inc: tools/verifyAST.spn
	hexdump -v -e '1/1 "0x%.2x, "' $< > $@
	echo "0x00" >> $@

clean:
	rm -f $(OBJECTS) $(LIB) $(DYNLIB) $(REPL) \
		spn.o spn.h dump.o gmon.out \
		src/verifyast.inc \
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
