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

#include "rcksum.h"

#define VERSION "0.0.1"

size_t blocksize = 0;

SHA_CTX shactx;

int get_len(FILE * f) {
	struct stat s;

	if (fstat(fileno(f), &s) == -1) {
		return 0;
	}

	return s.st_size;
}

void write_block_sums(unsigned char *buf, size_t got, FILE * f) {
	struct rsum r;
	unsigned char checksum[MD4_DIGEST_LENGTH];

	if (got < blocksize) {
		memset(buf + got, 0, blocksize - got);
	}

	r = rcksum_calc_rsum_block(buf, blocksize);
	rcksum_calc_checksum(&checksum[0], buf, blocksize);
	r.a = htons(r.a);
	r.b = htons(r.b);

	fwrite(&r, sizeof(r), 1, f);
	fwrite(checksum, sizeof(checksum), 1, f);
}

size_t read_stream_write_blocksums(FILE *fin, FILE * fout) {
	unsigned char *buf = malloc(blocksize);

	size_t len = 0;

	while (!feof(fin)) {
		int got = fread(buf, 1, blocksize, fin);

		if (got > 0) {
			SHA1_Update(&shactx, buf, got);

			write_block_sums(buf, got, fout);
			len += got;
		}
	}
	free(buf);

	return len;
}

void fcopy_hashes(FILE * fin, FILE * fout, size_t rsum_bytes, size_t hash_bytes) {
	unsigned char buf[20];
	size_t len;

	while ((len = fread(buf, 1, sizeof(buf), fin)) > 0) {
        /* write trailing rsum_bytes of the rsum (trailing because the second part of the rsum is more useful in practice for hashing), and leading checksum_bytes of the checksum */
        if (fwrite(buf + 4 - rsum_bytes, 1, rsum_bytes, fout) < rsum_bytes)
            break;
        if (fwrite(buf + 4, 1, hash_bytes, fout) < hash_bytes)
            break;
    }           
}


int main(int argc, char **argv) {

	char *infname = malloc(sizeof(char) * strlen(argv[1]) + 1);
	strcpy(infname, argv[1]);
	FILE *instream = fopen(infname, "rb");
	free(infname);

	blocksize = (get_len(instream) < 100000000) ? 2048 : 4096;

	FILE *tf = tmpfile();

	SHA1_Init(&shactx);
	size_t len = read_stream_write_blocksums(instream, tf);

	int seq_matches = len > blocksize ? 2 : 1;
	int rsum_len = ceil(((log(len) + log(blocksize)) / log(2) - 8.6) / seq_matches / 8);

	if (rsum_len > 4) { rsum_len = 4; }
	if (rsum_len < 2) { rsum_len = 2; }

	int checksum_len = ceil((20 + (log(len) + log(1+len/blocksize))/log(2)) / seq_matches / 8);

	{
		int checksum_len2 =  (7.9 + (20 + log(1 + len / blocksize) / log(2))) / 8;
		if (checksum_len < checksum_len2) {
			checksum_len = checksum_len2;
		}
	}

	char *outfname = malloc(sizeof(char) * strlen(argv[2]) + 1);
	strcpy(outfname, argv[2]);
	FILE *fout = fopen(outfname, "wb");
	free(outfname);

	fprintf(fout, "oc-zsync: " VERSION "\n");
	fprintf(fout, "Blocksize: %zu\n", blocksize);
	fprintf(fout, "Length: %zu\n", len);
	fprintf(fout, "Hash-Lengths: %d,%d,%d\n", seq_matches, rsum_len, checksum_len);
	
	{
		unsigned char digest[SHA_DIGEST_LENGTH];

		fputs("SHA-1: ", fout);
		SHA1_Final(digest, &shactx);
		for (unsigned int i = 0; i < SHA_DIGEST_LENGTH; i++) {
			fprintf(fout, "%02x", digest[i]);
		}
		fputc('\n', fout);
	}

	fputc('\n', fout);

	rewind(tf);
	fcopy_hashes(tf, fout, rsum_len, checksum_len);

	return 0;
}
