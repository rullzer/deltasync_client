/* Effectively the constructor and destructor for the rcksum object.
 * Also handles the file handles on the temporary store.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rcksum.h"
#include "internal.h"

/* rcksum_init(num_blocks, block_size, rsum_bytes, checksum_bytes, require_consecutive_matches)
 * Creates and returns an rcksum_state with the given properties
 */
struct rcksum_state *rcksum_init(zs_blockid nblocks, size_t blocksize,
								 int rsum_bytes, int checksum_bytes,
								 int require_consecutive_matches) {
	/* Allocate memory for the object */
	struct rcksum_state *z = (rcksum_state *)malloc(sizeof(struct rcksum_state));
	if (z == NULL) return NULL;

	/* Enter supplied properties. */
	z->blocksize = blocksize;
	z->blocks = nblocks;
	z->rsum_a_mask = rsum_bytes < 3 ? 0 : rsum_bytes == 3 ? 0xff : 0xffff;
	z->checksum_bytes = checksum_bytes;
	z->seq_matches = require_consecutive_matches;

	/* require_consecutive_matches is 1 if true; and if true we need 1 block of
	 * context to do block matching */
	z->context = blocksize * require_consecutive_matches;

	/* Initialise to 0 various state & stats */
	z->gotblocks = 0;
	memset(&(z->stats), 0, sizeof(z->stats));
	z->ranges = NULL;
	z->numranges = 0;

	//offsets
	z->offsets = new list<size_t>;
	z->moves = new map<size_t, list<size_t>>;
	z->add = new map<size_t, size_t>;

	/* Hashes for looking up checksums are generated when needed.
	 * So initially store NULL so we know there's nothing there yet.
	 */
	z->rsum_hash = NULL;
	z->bithash = NULL;

	if (!(z->blocksize & (z->blocksize - 1)) && z->blocks) {
			{   /* Calculate bit-shift for blocksize */
				int i;
				for (i = 0; i < 32; i++)
					if (z->blocksize == (1u << i)) {
						z->blockshift = i;
						break;
					}
			}

			z->blockhashes =
				(hash_entry *)malloc(sizeof(z->blockhashes[0]) *
						(z->blocks + z->seq_matches));
			if (z->blockhashes != NULL)
				return z;

			/* All below is error handling */
	}
	free(z);
	return NULL;
}

/* rcksum_end - destructor */
void rcksum_end(struct rcksum_state *z) {
	/* Free other allocated memory */
	free(z->rsum_hash);
	free(z->blockhashes);
	free(z->bithash);
	free(z->ranges);			// Should be NULL already
	free(z);
}
