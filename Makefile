CC=gcc
RM=rm -f
CFLAGS=-W -Wall -Wextra -pedantic -O2

.PHONY: all clean

all: http-redirect

# Links the final binary
http-redirect: http-redirect.o
	$(CC) -o $@ $(CFLAGS) http-redirect.o

# Compile a .c into a .o
%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<

# Clean up object files
clean:
	$(RM) *.o
