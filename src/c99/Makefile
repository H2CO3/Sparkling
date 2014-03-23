CFLAGS += -std=c99

SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SOURCES))

all: $(SOURCES) $(OBJECTS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	$(RM) $(OBJECTS) .DS_Store $(SRCDIR)/.DS_Store $(OBJDIR)/.DS_Store

.PHONY: all install clean

