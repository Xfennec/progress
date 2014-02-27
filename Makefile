OBJ=cv
CFLAGS=-g -Wall -D_FILE_OFFSET_BITS=64
$(OBJ) : cv.o sizes.o
	gcc -Wall $^ -o $@
%.o : %.c
	gcc $(CFLAGS) -c $^
clean :
	rm -f *.o $(OBJ)
install : $(OBJ)
	@echo "Trying /usr/local/bin/ and ~/bin/ ..."
	install -m 0755 $(OBJ) /usr/local/bin/ || \
	install -m 0755 $(OBJ) ~/bin
