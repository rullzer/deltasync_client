
/*
 *   zsync - client side rsync over http
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

/* This is the heart of zsync.
 *
 * .zsync file parsing and glue between all the main components of zsync.
 *
 * This file is where the .zsync metadata format is understood and read; it
 * extracts it and creates the corresponding rcksum object to apply the rsync
 * algorithm in constructing the target. It applies the zmap to convert byte
 * ranges between compressed and uncompressed versions of the data as needed,
 * and does decompression on compressed data received. It joins the HTTP code
 * to the rsync algorithm by converting lists of blocks from rcksum into lists
 * of byte ranges at particular URLs to be retrieved by the HTTP code.
 *
 * It also handles:
 * - blocking edge cases (decompressed data not lining up with blocks for rcksum; 
 *   last block of the file only containing partial data)
 * - recompression of the compressed data at the end of the transfer;
 * - checksum verification of the entire output.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <arpa/inet.h>

#include "rcksum.h"
#include "zsync.h"

#include <openssl/sha.h>

/* Probably we really want a table of compression methods here. But I've only
 * implemented SHA1 so this is it for now. */
const char ckmeth_sha1[] = { "SHA-1" };

/****************************************************************************
 *
 * zsync_state object and methods
 * This holds a single target file's details, and holds the state of the
 * in-progress local copy of that target that we are constructing (via a
 * contained rcksum_state object)
 *
 * Also holds all the other misc data from the .zsync file.
 */
struct zsync_state {
    struct rcksum_state *rs;    /* rsync algorithm state, with block checksums and
                                 * holding the in-progress local version of the target */
    off_t filelen;              /* Length of the target file */
    int blocks;                 /* Number of blocks in the target */
    size_t blocksize;           /* Blocksize */

    /* Checksum of the entire file, and checksum alg */
    char *checksum;
    const char *checksum_method;
};

static int zsync_read_blocksums(struct zsync_state *zs, FILE * f,
                                int rsum_bytes, int checksum_bytes,
                                int seq_matches);
static int zsync_sha1(struct zsync_state *zs, int fh);
static time_t parse_822(const char* ts);

/* char*[] = append_ptrlist(&num, &char[], "to add")
 * Crude data structure to store an ordered list of strings. This appends one
 * entry to the list. */
static char **append_ptrlist(int *n, char **p, char *a) {
    if (!a)
        return p;
    p = realloc(p, (*n + 1) * sizeof *p);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    p[*n] = a;
    (*n)++;
    return p;
}

/* Constructor */
struct zsync_state *zsync_begin(FILE * f) {
    /* Defaults for the checksum bytes and sequential matches properties of the
     * rcksum_state. These are the defaults from versions of zsync before these
     * were variable. */
    int checksum_bytes = 16, rsum_bytes = 4, seq_matches = 1;

    /* Field names that we can ignore if present and not
     * understood. This allows new headers to be added without breaking
     * backwards compatibility, and conversely to add headers that do break
     * backwards compat and have old clients give meaningful errors. */
    char *safelines = NULL;

    /* Allocate memory for the object */
    struct zsync_state *zs = calloc(sizeof *zs, 1);

    if (!zs)
        return NULL;

    for (;;) {
        char buf[1024];
        char *p = NULL;
        int l;

        if (fgets(buf, sizeof(buf), f) != NULL) {
            if (buf[0] == '\n')
                break;
            l = strlen(buf) - 1;
            while (l >= 0
                   && (buf[l] == '\n' || buf[l] == '\r' || buf[l] == ' '))
                buf[l--] = 0;

            p = strchr(buf, ':');
        }
        if (p && *(p + 1) == ' ') {
            *p++ = 0;
            p++;
            if (!strcmp(buf, "oc-zsync")) {
            }
            else if (!strcmp(buf, "Length")) {
                zs->filelen = atoll(p);
            }
            else if (!strcmp(buf, "Blocksize")) {
                zs->blocksize = atol(p);
                if (zs->blocksize < 0 || (zs->blocksize & (zs->blocksize - 1))) {
                    fprintf(stderr, "nonsensical blocksize %ld\n", zs->blocksize);
                    free(zs);
                    return NULL;
                }
            }
            else if (!strcmp(buf, "Hash-Lengths")) {
                if (sscanf
                    (p, "%d,%d,%d", &seq_matches, &rsum_bytes,
                     &checksum_bytes) != 3 || rsum_bytes < 1 || rsum_bytes > 4
                    || checksum_bytes < 3 || checksum_bytes > 16
                    || seq_matches > 2 || seq_matches < 1) {
                    fprintf(stderr, "nonsensical hash lengths line %s\n", p);
                    free(zs);
                    return NULL;
                }
            }
            else if (!strcmp(buf, ckmeth_sha1)) {
                if (strlen(p) != SHA_DIGEST_LENGTH * 2) {
                    fprintf(stderr, "SHA-1 digest from control file is wrong length.\n");
                }
                else {
					zs->checksum = malloc(sizeof(char) * SHA_DIGEST_LENGTH * 2 + 1);
                    strcpy(zs->checksum, p);
                    zs->checksum_method = ckmeth_sha1;
                }
            }
            else if (!safelines || !strstr(safelines, buf)) {
                fprintf(stderr,
                        "unrecognised tag %s - you need a newer version of zsync.\n",
                        buf);
                free(zs);
                return NULL;
            }
            if (zs->filelen && zs->blocksize)
                zs->blocks = (zs->filelen + zs->blocksize - 1) / zs->blocksize;
        }
        else {
            fprintf(stderr, "Bad line - not a zsync file? \"%s\"\n", buf);
            free(zs);
            return NULL;
        }
    }
    if (!zs->filelen || !zs->blocksize) {
        fprintf(stderr, "Not a zsync file (looked for Blocksize and Length lines)\n");
        free(zs);
        return NULL;
    }
    if (zsync_read_blocksums(zs, f, rsum_bytes, checksum_bytes, seq_matches) != 0) {
        free(zs);
        return NULL;
    }
    return zs;
}

/* zsync_read_blocksums(self, FILE*, rsum_bytes, checksum_bytes, seq_matches)
 * Called during construction only, this creates the rcksum_state that stores
 * the per-block checksums of the target file and holds the local working copy
 * of the in-progress target. And it populates the per-block checksums from the
 * given file handle, which must be reading from the .zsync at the start of the
 * checksums. 
 * rsum_bytes, checksum_bytes, seq_matches are settings for the checksums,
 * passed through to the rcksum_state. */
static int zsync_read_blocksums(struct zsync_state *zs, FILE * f,
                                int rsum_bytes, int checksum_bytes,
                                int seq_matches) {
    /* Make the rcksum_state first */
    if (!(zs->rs = rcksum_init(zs->blocks, zs->blocksize, rsum_bytes,
                               checksum_bytes, seq_matches))) {
        return -1;
    }

    /* Now read in and store the checksums */
    zs_blockid id = 0;
    for (; id < zs->blocks; id++) {
        struct rsum r = { 0, 0 };
        unsigned char checksum[CHECKSUM_SIZE];

        /* Read in */
        if (fread(((char *)&r) + 4 - rsum_bytes, rsum_bytes, 1, f) < 1
            || fread((void *)&checksum, checksum_bytes, 1, f) < 1) {

            /* Error - free the rcksum_state and tell the caller to bail */
            fprintf(stderr, "short read on control file; %s\n",
                    strerror(ferror(f)));
            rcksum_end(zs->rs);
            return -1;
        }

        /* Convert to host endian and store */
        r.a = ntohs(r.a);
        r.b = ntohs(r.b);
        rcksum_add_target_block(zs->rs, id, r, checksum);
    }
    return 0;
}

/* parse_822(buf[])
 * Parse an RFC822 date string. Returns a time_t, or -1 on failure. 
 * E.g. Tue, 25 Jul 2006 20:02:17 +0000
 */
static time_t parse_822(const char* ts) {
    struct tm t;

    if (strptime(ts, "%a, %d %b %Y %H:%M:%S %z", &t) == NULL
        && strptime(ts, "%d %b %Y %H:%M:%S %z", &t) == NULL) {
        return -1;
    }
    return mktime(&t);
}

/* zsync_blocksize(self)
 * Returns the blocksize used by zsync on this target. */
int zsync_blocksize(const struct zsync_state *zs) {
    return zs->blocksize;
}

/* zsync_status(self)
 * Returns  0 if we have no data in the target file yet.
 *          1 if we have some but not all
 *          2 or more if we have all.
 * The caller should not rely on exact values 2+; just test >= 2. Values >2 may
 * be used in later versions of libzsync. */
int zsync_status(const struct zsync_state *zs) {
    int todo = rcksum_blocks_todo(zs->rs);

    if (todo == zs->blocks)
        return 0;
    if (todo > 0)
        return 1;
    return 2;                   /* TODO: more? */
}

/* zsync_progress(self, &got, &total)
 * Writes the number of bytes got, and the total to get, into the long longs.
 */
void zsync_progress(const struct zsync_state *zs, long long *got,
                    long long *total) {

    if (got) {
        int todo = zs->blocks - rcksum_blocks_todo(zs->rs);
        *got = todo * zs->blocksize;
    }
    if (total)
        *total = zs->blocks * zs->blocksize;
}

/* zsync_needed_byte_ranges(self, &num, type)
 * Returns an array of offsets (2*num of them) for the start and end of num
 * byte ranges in the given type of version of the target (type as returned by
 * a zsync_get_urls call), such that retrieving all these byte ranges would be
 * sufficient to obtain a complete copy of the target file.
 */
off_t *zsync_needed_byte_ranges(struct zsync_state * zs, int *num) {
    int nrange;
    off_t *byterange;
    int i;

    /* Request all needed block ranges */
    zs_blockid *blrange = rcksum_needed_block_ranges(zs->rs, &nrange, 0, 0x7fffffff);
    if (!blrange)
        return NULL;

    /* Allocate space for byte ranges */
    byterange = malloc(2 * nrange * sizeof *byterange);
    if (!byterange) {
        free(blrange);
        return NULL;
    }

    /* Now convert blocks to bytes.
     * Note: Must cast one operand to off_t as both blocksize and blrange[x]
     * are int's whereas the product must be a file offfset. Needed so we don't
     * truncate file offsets to 32bits on 32bit platforms. */
    for (i = 0; i < nrange; i++) {
        byterange[2 * i] = blrange[2 * i] * (off_t)zs->blocksize;
        byterange[2 * i + 1] = blrange[2 * i + 1] * (off_t)zs->blocksize - 1;
    }
    free(blrange);      /* And release the blocks, we're done with them */

    *num = nrange;
    return byterange;
}

/* zsync_submit_source_file(self, FILE*, progress)
 * Read the given stream, applying the rsync rolling checksum algorithm to
 * identify any blocks of data in common with the target file. Blocks found are
 * written to our local copy of the target in progress. Progress reports if
 * progress != 0  */
int zsync_submit_source_file(struct zsync_state *zs, FILE * f, int progress) {
    return rcksum_submit_source_file(zs->rs, f, progress);
}

/* int hexdigit(char)
 * Maps a character to 0..15 as a hex digit (or 0 if not valid hex digit)
 */
static int hexdigit(char c) {
    return (isdigit(c) ? (c - '0') : isupper(c) ? (0xa + (c - 'A')) : islower(c)
            ? (0xa + (c - 'a')) : 0);
}

/* zsync_complete(self)
 * Finish a zsync download. Should be called once all blocks have been
 * retrieved successfully. This returns 0 if the file passes the final
 * whole-file checksum and if any recompression requested by the .zsync file is
 * done.
 * Returns -1 on error (and prints the error to stderr)
 *          0 if successful but no checksum verified
 *          1 if successful including checksum verified
 */
int zsync_complete(struct zsync_state *zs) {
    int rc = 0;

    /* We've finished with the rsync algorithm. Take over the local copy from
     * librcksum and free our rcksum state. */
    int fh = rcksum_filehandle(zs->rs);
    rcksum_end(zs->rs);
    zs->rs = NULL;

    /* Truncate the file to the exact length (to remove any trailing NULs from
     * the last block); return to the start of the file ready to verify. */
    if (ftruncate(fh, zs->filelen) != 0) {
        perror("ftruncate");
        rc = -1;
    }
    if (lseek(fh, 0, SEEK_SET) != 0) {
        perror("lseek");
        rc = -1;
    }

    /* Do checksum check */
    if (rc == 0 && zs->checksum && !strcmp(zs->checksum_method, ckmeth_sha1)) {
        rc = zsync_sha1(zs, fh);
    }
    close(fh);

    return rc;
}

/* zsync_sha1(self, filedesc)
 * Given the currently-open-and-at-start-of-file complete local copy of the
 * target, read it and compare the SHA1 checksum with the one from the .zsync.
 * Returns -1 or 1 as per zsync_complete.
 */
static int zsync_sha1(struct zsync_state *zs, int fh) {
    SHA_CTX shactx;

    {                           /* Do SHA1 of file contents */
        unsigned char buf[4096];
        int rc;

        SHA1_Init(&shactx);
        while (0 < (rc = read(fh, buf, sizeof buf))) {
            SHA1_Update(&shactx, buf, rc);
        }
        if (rc < 0) {
            perror("read");
            return -1;
        }
    }

    {                           /* And compare result of the SHA1 with the one from the .zsync */
        unsigned char digest[SHA_DIGEST_LENGTH];
        int i;

        SHA1_Final(digest, &shactx);

        for (i = 0; i < SHA_DIGEST_LENGTH; i++) {
            unsigned int j;
            sscanf(&(zs->checksum[2 * i]), "%2x", &j);
            if (j != digest[i]) {
                return -1;
            }
        }
        return 1; /* Checksum verified okay */
    }
}

/* Destructor */
char *zsync_end(struct zsync_state *zs) {
    /* Free rcksum object and zmap */
    if (zs->rs)
        rcksum_end(zs->rs);

    free(zs->checksum);
    free(zs);
    return NULL;
}

/* zsync_submit_data(self, buf[], offset, blocks)
 * Passes data retrieved from the remote copy of
 * the target file to libzsync, to be written into our local copy. The data is
 * the given number of blocks at the given offset (must be block-aligned), data
 * in buf[].  */
static int zsync_submit_data(struct zsync_state *zs,
                             const unsigned char *buf, off_t offset,
                             int blocks) {
    zs_blockid blstart = offset / zs->blocksize;
    zs_blockid blend = blstart + blocks - 1;

    return rcksum_submit_blocks(zs->rs, buf, blstart, blend);
}

char *zsync_cur_filename(struct zsync_state *zs) {
    return rcksum_filename(zs->rs);
}


/* zsync_rename_file(self, filename)
 * Tell libzsync to move the local copy of the target (or under construction
 * target) to the given filename. */
int zsync_rename_file(struct zsync_state *zs, const char *f) {
	char *rf = zsync_cur_filename(zs);
	int x = rename(rf, f);

	return x;
}


