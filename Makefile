CC=g++
CFLAGS=-std=c++11 -D _POSIX_C_SOURCE=1 -Wall -pedantic -D _XOPEN_SOURCE=500 -Werror -g
LDFLAGS=-lssl -lcrypto -lm

all: uploadclient zsyncmake

uploadclient: uploadclient.o range.o hash.o rsum.o state.o zsync.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

zsyncmake: mksync.o rsum.o rcksum.h hash.o range.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)
	
%.o: %.cpp
	$(CC) -c -o $@ $< $(CFLAGS)

%.o: %.c 
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -rf uploadclient zsyncmake *.o
