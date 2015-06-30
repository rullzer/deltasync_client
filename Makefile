CC=gcc
CFLAGS=-std=c11 -D _POSIX_C_SOURCE=1 -Wall -pedantic -D _XOPEN_SOURCE=500
LDFLAGS=-lssl -lcrypto -lm

all: zsyncclient zsyncmake

uploadclient: uploadclient.o range.o hash.o rsum.o state.o zsync.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

zsyncmake: mksync.o rsum.o rcksum.h hash.o range.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)
	
%.o: %.c 
	$(CC) -c -o $@ $< $(CFLAGS)


