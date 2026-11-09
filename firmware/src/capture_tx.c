#include "capture_tx.h"

#include <errno.h>
#include <zephyr/sys/util.h>

#include "ble.h"
#include "capcodec.h"
#include "capture.h"

#define QUEUE_LEN 8u
#define PAYLOAD_MAX 244u
#define FIT_MAX ((PAYLOAD_MAX - sizeof(sp_cap_data_t)) / sizeof(sp_cap_sample_t))

typedef struct {
	uint32_t seq;
	uint16_t n_samples;
	uint16_t trigger_index;
	uint16_t next;
	uint16_t packet_samples;
	size_t packet_len;
	bool header_sent;
	bool packet_ready;
} tx_job_t;

static const capstore_t *store;
static const capw_t *writer;
static uint32_t queue[QUEUE_LEN];
static uint32_t queue_head;
static uint32_t queue_len;
static bool sending;
static tx_job_t job;
static capdec_t dec;
static uint8_t payload[PAYLOAD_MAX];

static int refill(capdec_t *d)
{
	const capstore_entry_t *e = capstore_find(store, job.seq);

	if (!e) {
		return -ENOENT;
	}
	capstore_read_t req = {job.seq, d->source_offset, d->in,
			       MIN(e->payload_len - d->source_offset, CODEC_IN_BYTES)};
	d->in_len = req.len;
	d->source_offset += req.len;
	return req.len ? capstore_read(store, &req) : 0;
}

void cap_tx_init(const capstore_t *capture_store, const capw_t *capture_writer)
{
	store = capture_store;
	writer = capture_writer;
	queue_len = 0;
	sending = false;
	dec.refill = refill;
}

void cap_tx_queue(uint32_t seq)
{
	if (queue_len == QUEUE_LEN) {
		queue_head = (queue_head + 1u) % QUEUE_LEN;
		queue_len--;
	}
	queue[(queue_head + queue_len) % QUEUE_LEN] = seq;
	queue_len++;
}

bool cap_tx_busy(void)
{
	return sending || queue_len > 0;
}

static bool start_next(void)
{
	uint32_t seq = queue[queue_head];

	if (capw_pending(writer, seq)) {
		return false;
	}
	queue_head = (queue_head + 1u) % QUEUE_LEN;
	queue_len--;
	const capstore_entry_t *e = capstore_find(store, seq);
	job = (tx_job_t){.seq = seq, .n_samples = e ? e->n_samples : 0u, .trigger_index = e ? e->trigger_index : 0u};
	capdec_init(&dec, job.n_samples);
	sending = true;
	return true;
}

static size_t build_packet(size_t capacity)
{
	sp_cap_sample_t samples[FIT_MAX];
	cap_buf_t out = {payload, capacity};

	if (!job.header_sent) {
		cap_header_info_t info = {job.seq, job.n_samples, job.trigger_index};
		return cap_pack_header(&info, &out);
	}
	uint32_t fit = MIN(cap_data_fit(capacity), FIT_MAX);
	uint32_t n = MIN(fit, (uint32_t)(job.n_samples - job.next));

	for (uint32_t i = 0; i < n; i++) {
		if (capdec_next(&dec, &samples[i]) != 1) {
			return 0;
		}
	}
	cap_data_info_t data = {job.seq, job.next, samples, n};
	job.packet_samples = (uint16_t)n;
	return cap_pack_data(&data, &out);
}

static void drop_all(void)
{
	sending = false;
	queue_len = 0;
}

void cap_tx_pump(void)
{
	for (;;) {
		if (!sending && (queue_len == 0 || !start_next())) {
			return;
		}
		uint16_t room = ble_capture_room();
		if (room == 0) {
			drop_all();
			return;
		}
		if (!job.packet_ready) {
			job.packet_len = build_packet(MIN(room, PAYLOAD_MAX));
			if (job.packet_len == 0) {
				sending = false;
				continue;
			}
			job.packet_ready = true;
		}
		int err = ble_send_capture(payload, (uint16_t)job.packet_len);
		if (err == -EAGAIN || err == -ENOMEM) {
			return;
		}
		if (err) {
			drop_all();
			return;
		}
		job.packet_ready = false;
		if (job.header_sent) {
			job.next = (uint16_t)(job.next + job.packet_samples);
		} else {
			job.header_sent = true;
		}
	}
}
