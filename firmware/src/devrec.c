#include "devrec.h"

#define DEVCMD_BYTES 2u
#define DEVCMD_VALUE 1u

bool devcmd_valid(const devcmd_req_t *req)
{
	if (req->data[0] == SP_CTRL_REG0_MODE) {
		return req->len >= DEVCMD_BYTES && req->data[DEVCMD_VALUE] <= SP_REG0_LDO;
	}
	if (req->data[0] == SP_CTRL_STREAM) {
		return req->len >= DEVCMD_BYTES && req->data[DEVCMD_VALUE] <= SP_STREAM_MAX_DIVIDER;
	}
	if (req->data[0] == SP_CTRL_RECORD) {
		return req->len >= DEVCMD_BYTES && req->data[DEVCMD_VALUE] >= SP_RECORD_MIN_S &&
		       req->data[DEVCMD_VALUE] <= SP_RECORD_MAX_S;
	}
	if (req->data[0] == SP_CTRL_REJECT_CAPTURE) {
		return req->len >= DEVCMD_BYTES && req->data[DEVCMD_VALUE] <= SP_REJECT_CAPTURE_MAX_G;
	}
	return true;
}

void devrec_start(devrec_t *rec, uint8_t seconds)
{
	rec->samples = (uint32_t)seconds * CAP_ODR_HZ;
	rec->requested = false;
}

bool devrec_due(const devrec_t *rec, uint32_t sampled)
{
	return rec->samples > 0u && sampled > 0u;
}

capw_request_t devrec_take(devrec_t *rec, const devrec_at_t *at)
{
	capw_request_t req = {
		.seq = at->seq,
		.trigger = at->sampled - 1u,
		.epoch = at->epoch,
		.mode = SP_CAPTURE_STANDARD,
		.samples = rec->samples,
	};

	rec->seq = at->seq;
	rec->requested = true;
	rec->samples = 0u;
	return req;
}

bool devrec_running(const devrec_t *rec, const capw_t *writer)
{
	return rec->samples > 0u || (rec->requested && capw_pending(writer, rec->seq));
}
