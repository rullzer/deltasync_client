/*
 *   zsync - client side rsync over http
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

#include "upload.h"

struct zsync_state;

/* zsync_begin - load a zsync file and return data structure to use for the rest of the process.
 */
struct zsync_state* zsync_begin(FILE* cf);

/* zsync_submit_source_file - submit local file data to zsync
 */
int zsync_submit_source_file(struct zsync_state* zs, FILE* f);

/* zsync_complete - set file length and verify checksum if available
 * Returns -1 for failure, 1 for success, 0 for unable to verify (e.g. no checksum in the .zsync) */
int zsync_complete(struct zsync_state* zs);

/* Clean up and free all resources. The pointer is freed by this call.
 * Returns a strdup()d pointer to the name of the file resulting from the process. */
char* zsync_end(struct zsync_state* zs);

void zsync_parseAdd(struct zsync_state *zs, FILE *fnew, size_t new_len, upload *u);
void zsync_parseMove(struct zsync_state *zs, upload *u);
