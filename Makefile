OBJ=cv
CFLAGS = -g -Wall -D_FILE_OFFSET_BITS=64 -std=gnu99
CFLAGS += -DBUILD_DAEMON=1
CFLAGS += $(shell pkg-config --cflags --libs libnotify glib)
PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin

$(OBJ) : cv.c sizes.c
	gcc $(CFLAGS) $^ -o $@
clean :
	rm -f *.o $(OBJ)
install : $(OBJ)
	@echo "Installing to $(BINDIR) ..."
	@install -m 0755 $(OBJ) $(BINDIR)/$(TARGET) || \
	echo "Failed. Try "make PREFIX=~ install" ?"
