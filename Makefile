OBJ = progress
override CFLAGS += -g -Wall -D_FILE_OFFSET_BITS=64
override LDFLAGS += -lm
UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
    ifeq (, $(shell which pkg-config 2> /dev/null))
    $(error "pkg-config command not found")
    endif
    ifeq (, $(shell pkg-config ncurses --libs 2> /dev/null))
    $(error "ncurses package not found")
    endif
    override LDFLAGS += $(shell pkg-config ncurses --libs)
endif
ifeq ($(UNAME), Darwin)
    override LDFLAGS += -lncurses
endif
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

$(OBJ) : progress.o sizes.o hlist.o
	$(CC) -Wall $^ -o $@ $(LDFLAGS)
%.o : %.c
	$(CC) $(CFLAGS) -c $^
clean :
	rm -f *.o $(OBJ)
install : $(OBJ)
	@echo "Installing program to $(DESTDIR)$(BINDIR) ..."
	@mkdir -p $(DESTDIR)$(BINDIR)
	@install -pm0755 $(OBJ) $(DESTDIR)$(BINDIR)/$(TARGET) || \
	echo "Failed. Try "make PREFIX=~ install" ?"
	@echo "Installing manpage to $(DESTDIR)$(MANDIR) ..."
	@mkdir -p $(DESTDIR)$(MANDIR)
	@install -pm0644 $(OBJ).1 $(DESTDIR)$(MANDIR)/ || \
	echo "Failed. Try "make PREFIX=~ install" ?"
uninstall :
	@rm -f $(DESTDIR)$(BINDIR)/$(OBJ)
	@rm -f $(DESTDIR)$(MANDIR)/$(OBJ).1
