
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
						printf("%d: Add to %lld\n", id, diff);
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

		
		/* Else - advance the window by 1 byte - update the rolling checksum
		 * and our offset in the buffer */
		{
			unsigned char Nc = data[x + bs * 2];
			unsigned char nc = data[x + bs];
			unsigned char oc = data[x];
			UPDATE_RSUM(z->r[0].a, z->r[0].b, oc, nc, z->blockshift);
			if (z->seq_matches > 1)
				UPDATE_RSUM(z->r[1].a, z->r[1].b, nc, Nc, z->blockshift);
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

void parseAdd(struct rcksum_state *z, int len) {

	z->offsets->sort();

	size_t i = 0;
	do {
		size_t offset = z->offsets->front();
		z->offsets->pop_front();

		if (offset - i) {
			printf("add %lu bytes at %lu\n", offset-i, i);
			i = offset;
		} else {
			//printf("no move\n");
		}

		i += z->blocksize;
	} while(!z->offsets->empty());
}

void parseMove(struct rcksum_state *z, int len) {
	//printf("move <start> <num> <to>\n");
	for (map<long long, list<size_t> >::iterator it = z->moves->begin(); it != z->moves->end(); it++) {
		long long move = it->first;
		list<size_t> offsets = it->second;

		for(list<size_t>::iterator it2 = offsets.begin(); it2 != offsets.end(); it2++) {
			printf("move %lu %d %llu\n", *it2, 2048, (*it2) + move);
		}
	}
}
