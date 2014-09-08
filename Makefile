OBJ = cv
override CFLAGS += -Ofast -funroll-loops -flto -Wall -D_FILE_OFFSET_BITS=64
override DEVEL_FLAGS += -g -Wall -D_FILE_OFFSET_BITS=64
override LFLAGS += -lncurses -lm
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

$(OBJ) : cv.o sizes.o hlist.o
	$(CC) -Wall $^ -o $@ $(LFLAGS)

%.o : %.c
	$(CC) $(DEVEL_FLAGS) -c $^

clean :
	rm -f *.o $(OBJ)

ofast : 
	$(CC) $(CFLAGS) -c cv.c
	$(CC) $(CFLAGS) -c sizes.c
	$(CC) $(CFLAGS) -c hlist.c

install : $(OBJ)
	@echo "Installing program to $(DESTDIR)$(BINDIR) ..."
	@mkdir -p $(DESTDIR)$(BINDIR)
	@install -pm0755 $(OBJ) $(DESTDIR)$(BINDIR)/$(TARGET) || \
		echo "Failed. Try "make PREFIX=~ install" ?"
	@echo "Installing manpage to $(DESTDIR)$(MANDIR) ..."
	@mkdir -p $(DESTDIR)$(MANDIR)
	@install -pm0644 cv.1 $(DESTDIR)$(MANDIR)/ || \
		echo "Failed. Try "make PREFIX=~ install" ?"
