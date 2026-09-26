#include "capture_tx.h"

#include <errno.h>
#include <zephyr/sys/util.h>

#include "ble.h"

#define QUEUE_LEN 8u
#define PAYLOAD_MAX 244u

static const cap_t *source;
static uint32_t queue[QUEUE_LEN];
static uint32_t queue_head;
static uint32_t queue_len;
static bool sending;
static bool header_sent;
static cap_packet_t pkt;
static uint8_t payload[PAYLOAD_MAX];

void cap_tx_init(const cap_t *cap)
{
	source = cap;
	queue_len = 0;
	sending = false;
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

static void start_next(void)
{
	uint32_t seq = queue[queue_head];

	queue_head = (queue_head + 1u) % QUEUE_LEN;
	queue_len--;
	pkt.seq = seq;
	pkt.slot = cap_find(source, seq);
	pkt.first = 0;
	pkt.buf = payload;
	header_sent = false;
	sending = true;
}

static void drop_all(void)
{
	sending = false;
	queue_len = 0;
}

void cap_tx_pump(void)
{
	for (;;) {
		if (!sending) {
			if (queue_len == 0) {
				return;
			}
			start_next();
		}
		uint16_t room = ble_capture_room();
		if (room == 0) {
			drop_all();
			return;
		}
		cap_packet_t trial = pkt;
		trial.capacity = MIN(room, PAYLOAD_MAX);
		size_t len = header_sent ? cap_pack_data(source, &trial) : cap_pack_header(source, &trial);
		if (len == 0) {
			sending = false;
			continue;
		}
		int err = ble_send_capture(payload, (uint16_t)len);
		if (err == -EAGAIN || err == -ENOMEM) {
			return;
		}
		if (err) {
			drop_all();
			return;
		}
		pkt = trial;
		header_sent = true;
	}
}
