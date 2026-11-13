#include "capjoin.h"

capjoin_piece_t capjoin_piece(const capjoin_req_t *req)
{
	if (req->offset >= req->main_size) {
		return (capjoin_piece_t){true, req->offset - req->main_size, req->len};
	}
	uint32_t room = req->main_size - req->offset;

	return (capjoin_piece_t){false, req->offset, req->len < room ? req->len : room};
}
