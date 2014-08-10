OBJ = cv
override CFLAGS += -g -Wall -D_FILE_OFFSET_BITS=64
override LFLAGS += -lncurses -lm
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

$(OBJ) : cv.o sizes.o hlist.o
	$(CC) -Wall $^ -o $@ $(LFLAGS)
%.o : %.c
	$(CC) $(CFLAGS) -c $^
clean :
	rm -f *.o $(OBJ)
install : $(OBJ)
	@echo "Installing to $(DESTDIR)$(BINDIR) ..."
	@mkdir -p $(DESTDIR)$(BINDIR)
	@install -pm0755 $(OBJ) $(DESTDIR)$(BINDIR)/$(TARGET) || \
	echo "Failed. Try "make PREFIX=~ install" ?"
