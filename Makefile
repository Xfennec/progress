OBJ=cv
CFLAGS=-g -Wall -D_FILE_OFFSET_BITS=64

PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin

$(OBJ) : cv.o sizes.o
	gcc -Wall $^ -o $@
%.o : %.c
	gcc $(CFLAGS) -c $^
clean :
	rm -f *.o $(OBJ)
install : $(OBJ)
	@echo "Installing to $(BINDIR) ..."
	@mkdir -p $(BINDIR)
	@install -m 0755 $(OBJ) $(BINDIR)/$(TARGET) || \
	echo "Failed. Try "make PREFIX=~ install" ?"
