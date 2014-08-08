OBJ=cv
CFLAGS+=-g -Wall -D_FILE_OFFSET_BITS=64
LFLAGS=-lncurses -lm
PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin

$(OBJ) : cv.o sizes.o hlist.o
	$(CC) -Wall $^ -o $@ $(LFLAGS)
%.o : %.c
	$(CC) $(CFLAGS) -c $^
clean :
	rm -f *.o $(OBJ)
install : $(OBJ)
	@echo "Installing to $(BINDIR) ..."
	@mkdir -p $(BINDIR)
	@install -m 0755 $(OBJ) $(BINDIR)/$(TARGET) || \
	echo "Failed. Try "make PREFIX=~ install" ?"
