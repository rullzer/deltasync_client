/*
 *   rcksum/lib - library for using the rsync algorithm to determine
 *               which parts of a file you have and which you need.
 *   Copyright (C) 2004,2005,2009 Colin Phipps <cph@moria.org.uk>
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

/* This is the library interface. Very changeable at this stage. */

#include <stdio.h>
#include <sys/types.h>

struct rcksum_state;

typedef int zs_blockid;

struct rsum {
	unsigned short	a;
	unsigned short	b;
} __attribute__((packed));

#define CHECKSUM_SIZE 16

struct rcksum_state* rcksum_init(zs_blockid nblocks, size_t blocksize, int rsum_butes, int checksum_bytes, int require_consecutive_matches);
void rcksum_end(struct rcksum_state* z);

void rcksum_add_target_block(struct rcksum_state* z, zs_blockid b, struct rsum r, void* checksum);

int rcksum_submit_source_file(struct rcksum_state* z, FILE* f);

/* For preparing rcksum control files - in both cases len is the block size. */
struct rsum __attribute__((pure)) rcksum_calc_rsum_block(const unsigned char* data, size_t len);
void rcksum_calc_checksum(unsigned char *c, const unsigned char* data, size_t len);
void parseAdd(struct rcksum_state *z, FILE *fnew, FILE *fout, size_t ne_len);
void parseMove(struct rcksum_state *z, FILE *fout, FILE *forig);
