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

struct zsync_state {
	struct rcksum_state *rs;

	int filelen;
	int blocks;
	size_t blocksize;

	char *checksum;
	const char *checksum_method;
};

struct zsync_state *read_zsync_control_file(const char *fn) {
	struct zsync_state *zs = NULL;

	FILE *f = fopen(fn, "r");

	zs = zsync_begin(f);

	fclose(f);

	return zs;
}

void read_seed_file(struct zsync_state *z, const char *fname) {
	FILE *f = fopen(fname, "r");
	zsync_submit_source_file(z, f, 1);
	fclose(f);

	{   /* And print how far we've progressed towards the target file */
		long long done, total;

		zsync_progress(z, &done, &total);
		printf("\n%lld - %lld = %lld\n", total, done, total - done);
	}
}

int main(int argc, char **argv) {

	struct zsync_state *zs = read_zsync_control_file(argv[1]);
	
	char *fin = malloc(sizeof(char) * strlen(argv[2]) + 1);
	char *fout = malloc(sizeof(char) * strlen(argv[3]) + 1);

	strcpy(fin, argv[2]);
	strcpy(fout, argv[3]);

	//Get tmp file
	char *tmpfile = malloc(sizeof(char) * strlen(fout) + 6);
	strcpy(tmpfile, fout);
	strcat(tmpfile, ".part");

	//Step 2 fill availble local data
	read_seed_file(zs, fin);

	zsync_rename_file(zs, tmpfile);

	//Step 3 Get required byte ranges
	int nrange;
	off_t *zbyterange = zsync_needed_byte_ranges(zs, &nrange);

	printf("------\n");
	printf("%d ranges needed\n", nrange);
	for (unsigned int i = 0; i < nrange; i = i+2) {
		printf("%d -- %d\n", zbyterange[i], zbyterange[i+1]);
	}

}
