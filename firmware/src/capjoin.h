#ifndef CAPJOIN_H
#define CAPJOIN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint32_t main_size;
	uint32_t offset;
	uint32_t len;
} capjoin_req_t;

typedef struct {
	bool spare;
	uint32_t local;
	uint32_t len;
} capjoin_piece_t;

capjoin_piece_t capjoin_piece(const capjoin_req_t *req);

#endif
