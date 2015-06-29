CC=gcc
CFLAGS=-std=c11 -D _POSIX_C_SOURCE=1 -Wall -pedantic
LDFLAGS=-lssl -lcrypto -lm

zsyncmake: mksync.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)
	

mksync.o: mksync.c
	$(CC) -c -o $@ $< $(CFLAGS)


