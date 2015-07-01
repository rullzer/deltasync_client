#include <stdio.h>
#include <sys/types.h> 
#include <sys/stat.h> 
#include <unistd.h>
#include <string.h>
#include <stdlib.h> 
#include <math.h> 

#include <openssl/md4.h>
#include <openssl/sha.h> 
#include <arpa/inet.h> 

#include "zsync.h"

int get_len(FILE * f) {
	struct stat s;

	if (fstat(fileno(f), &s) == -1) {
		return 0;
	}

	return s.st_size;
}

struct zsync_state *read_zsync_control_file(const char *fn) {
	struct zsync_state *zs = NULL;

	FILE *f = fopen(fn, "r");

	zs = zsync_begin(f);

	fclose(f);

	return zs;
}

void read_seed_file(struct zsync_state *z, const char *fname) {
	FILE *f = fopen(fname, "r");
	zsync_submit_source_file(z, f);
	fclose(f);
}

void fix_input(struct zsync_state *z, const char *nameFnew, const char *nameFout, const char *nameForig) {
	FILE *fnew = fopen(nameFnew, "r");
	FILE *fout = fopen(nameFout, "r+");
	FILE *forig = fopen(nameForig, "r");

	int len = get_len(fnew);
	ftruncate(fileno(fout), len);

	zsync_parseMove(z, fout, forig);
	zsync_parseAdd(z, fnew, fout, len);

	fclose(fnew);
	fclose(fout);
	fclose(forig);
}

int main(int argc, char **argv) {

	if (argc < 5) {
		printf("Usage: %s <file.zsync> <file.new> <file.out> <file>\n", argv[0]);
		return 0;
	}

	struct zsync_state *zs = read_zsync_control_file(argv[1]);
	
	char *fin = (char *)malloc(sizeof(char) * strlen(argv[2]) + 1);

	strcpy(fin, argv[2]);

	//Step 2 fill availble local data
	printf("READING %s\n", fin);
	read_seed_file(zs, fin);
	printf("DONE READING\n");

	//Step 3 fix input file
	fix_input(zs, argv[2], argv[3], argv[4]);


	return 1;
	//Step 3 Get required byte ranges
	/*
	int nrange;
	off_t *zbyterange = zsync_needed_byte_ranges(zs, &nrange);

	printf("------\n");
	printf("%d Ranges need to be removed\n", nrange);
	for (unsigned int i = 0; i < nrange; i++) {
		printf("%d -- %d\n", zbyterange[i*2], zbyterange[i*2+1]);
	}*/

}
