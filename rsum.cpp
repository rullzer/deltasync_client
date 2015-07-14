
/*
 *   rcksum/lib - library for using the rsync algorithm to determine
 *			   which parts of a file you have and which you need.
 *   Copyright (C) 2004,2005,2007,2009 Colin Phipps <cph@moria.org.uk>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the Artistic License v2 (see the accompanying 
 *   file COPYING for the full license terms), or, at your option, any later 
 *   version of the same license.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   COPYING file for details.
 */

/* This is the core of the rsync rolling checksum algorithm - this is what it's
 * all about. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "rcksum.h"
#include "internal.h"

#include <openssl/md4.h>

#include <map>

using namespace std;

#define UPDATE_RSUM(a, b, oldc, newc, bshift) do { (a) += ((unsigned char)(newc)) - ((unsigned char)(oldc)); (b) += (a) - ((oldc) << (bshift)); } while (0)

/* rcksum_calc_rsum_block(data, data_len)
 * Calculate the rsum for a single block of data. */
struct rsum __attribute__ ((pure)) rcksum_calc_rsum_block(const unsigned char *data, size_t len) {
	register unsigned short a = 0;
	register unsigned short b = 0;

	while (len) {
		unsigned char c = *data++;
		a += c;
		b += len * c;
		len--;
	}
	{
		struct rsum r = { a, b };
		return r;
	}
}

/* rcksum_calc_checksum(checksum_buf, data, data_len)
 * Returns the MD4 checksum (in checksum_buf) of the given data block */
void rcksum_calc_checksum(unsigned char *c, const unsigned char *data,
						  size_t len) {
	MD4_CTX ctx;
	MD4_Init(&ctx);
	MD4_Update(&ctx, data, len);
	MD4_Final(c, &ctx);
}

int check_checksum(struct rcksum_state *const z, const struct hash_entry *e, const unsigned char *data, struct rsum *r, int prev_valid, zs_blockid *id) {

	const struct hash_entry *e_next = e;
	while(e_next) {

		e = e_next;
		e_next = e->next;
		
		//Check weak checksum
		if (e->r.a != (r[0].a & z->rsum_a_mask) || e->r.b != r[0].b) {
			continue;
		}

		//Get the current block id
		zs_blockid _id = get_HE_blockid(z, e);

		// If the previous block is not valid.. check the next block to verify this one..
		//Check weak checksum of next block
		if (!prev_valid) {
			if (z->blockhashes[_id+1].r.a != (r[1].a & z->rsum_a_mask) || z->blockhashes[_id+1].r.b != r[1].b) {
				continue;
			}
		}

		//Check long checksum
		unsigned char md4sum[MD4_DIGEST_LENGTH];
		rcksum_calc_checksum(&md4sum[0], data, z->blocksize);
		if (memcmp(&md4sum[0], z->blockhashes[_id].checksum, z->checksum_bytes)) {
			continue;
		}

		// If the previous block is not valid.. check the next block to verify this one..
		if (!prev_valid) {
			//Check long checksum of next block
			rcksum_calc_checksum(&md4sum[0], data + z->blocksize, z->blocksize);
			if (memcmp(&md4sum[0], z->blockhashes[_id+1].checksum, z->checksum_bytes)) {
				continue;
			}
		}

		*id = _id;

		return 1;
	}
	return 0;
}

int check_data(struct rcksum_state *z, unsigned char *data, size_t len, size_t offset) {
	int x = 0;
	register int bs = z->blocksize;
	int got_blocks = 0;

	int prev_valid = 0;

	for (;;) {
		if (x + z->context >= len) {
			return got_blocks;
		}

		struct rsum r[2];
		r[0] = rcksum_calc_rsum_block(data + x, bs);
		r[1] = rcksum_calc_rsum_block(data + x + bs, bs);

		{
			const struct hash_entry *e;
			unsigned int hash = calc_rhash2(z, r[0], r[1]);
			if ((z->bithash[(hash & z->bithashmask) >> 3] & (1 << (hash & 7))) != 0
				&& (e = z->rsum_hash[hash & z->hashmask]) != NULL) {

				//Get block id
				zs_blockid id = -1;

				int matched = check_checksum(z, e, data+x, r, prev_valid, &id);

				if (matched) {
					z->offsets->push_back(offset+x);

					if (id * z->blocksize != offset+x) {
						long long diff = (offset+x) - id*z->blocksize;
						(*(z->moves))[diff].push_back(id*z->blocksize);
						//printf("%d: Add to %lld\n", id, diff);
					}

					got_blocks++;
					remove_block_from_hash(z, id);
					prev_valid = 1;

					x += bs;

					if (x+z->context > len) {
						return got_blocks;
					}
					continue;
				}
			}
		}

		x++;
		prev_valid = 0;
	}
	return 0;
}

/* rcksum_submit_source_file(self, stream, progress)
 * Read the given stream, applying the rsync rolling checksum algorithm to
 * identify any blocks of data in common with the target file. Blocks found are
 * written to our working target output. Progress reports if progress != 0
 */
int rcksum_submit_source_file(struct rcksum_state *z, FILE * f) {
	/* Track progress */
	int got_blocks = 0;
	off_t in = 0;

	/* Allocate buffer of 16 blocks */
	register int bufsize = z->blocksize * 16;
	unsigned char *buf = (unsigned char *)malloc(bufsize + z->context);

	build_hash(z);

	while (!feof(f)) {
		size_t len;
		off_t start_in = in;

		/* If this is the start, fill the buffer for the first time */
		if (!in) {
			len = fread(buf, 1, bufsize, f);
			in += len - z->context;
		}
		/* Else, move the last context bytes from the end of the buffer to the
		 * start, and refill the rest of the buffer from the stream. */
		else {
			memcpy(buf, buf + (bufsize - z->context), z->context);
			in += bufsize - z->context;
			len = z->context + fread(buf + z->context, 1, bufsize - z->context, f);
		}

		if (ferror(f)) {
			perror("fread");
			free(buf);
			return got_blocks;
		}
		if (feof(f)) {		  /* 0 pad to complete a block */
			memset(buf + len, 0, z->context);
			len += z->context;
		}

		/* Process the data in the buffer, and report progress */
		got_blocks += check_data(z, buf, len, start_in);
	}
	printf("%d\n", got_blocks);
	free(buf);
	return got_blocks;
}

void parseAdd(struct rcksum_state *z, FILE *fnew, size_t new_len, upload *u) {
	z->offsets->sort();

	size_t i = 0;
	do {
		size_t offset = z->offsets->front();
		z->offsets->pop_front();

		if (offset - i) {
			//Set read/write pointer at right location
			fseek(fnew, i, SEEK_SET);

			//Now copy all required bytes
			size_t left = offset-i;

			while(left) {
				char *x = (char *)malloc(sizeof(char) * 102400);
				size_t s = left < 102400 ? left : 102400;
				fread(x, 1, s, fnew);
				u->add(i, s, x);
				free(x);

				left = left - s;
				i = i+s;

			}
			i = offset;
		} 

		i += z->blocksize;
	} while(!z->offsets->empty());

	//If we just appended the file... fix it here
	if (i < new_len) {
		fseek(fnew, i, SEEK_SET);

		while(i < new_len) {
			char *x = (char *)malloc(sizeof(char) * 102400);
			size_t s = (new_len - i) < 102400 ? (new_len - i) : 102400;
			fread(x, 1, s, fnew);
			u->add(i, s, x);
			i = i+s;
		}
	}
}

void parseMove(struct rcksum_state *z, upload *u) {
	for (auto it = z->moves->begin(); it != z->moves->end(); it++) {
		long long move = it->first;
		list<size_t> offsets = it->second;

		printf("%lu\n", offsets.size());
		for(auto it2 = offsets.begin(); it2 != offsets.end(); it2++) {
			size_t offset = *it2;

			size_t num = 1;
			size_t prev = offset;

			auto it3 = it2;
			it3++;

			while ( it3 != offsets.end() && (*it3) == prev + 2048 ) {
				prev = (*it3);
				it3++;
				num++;
			}

			it2 = it3;

			u->move(offset, offset+move, 2048 * num);

			if (it2 == offsets.end()) {
				break;
			}
		}
	}
}
