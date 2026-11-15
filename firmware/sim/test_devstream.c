#include <stdio.h>
#include <string.h>

#include "../src/devstream.h"

#define FULL_ROOM 244u
#define SMALL_ROOM 182u
#define TINY_ROOM 16u
#define NO_ROOM 3u
#define LARGE_ROOM 300u
#define START_MS 1000u
#define PACKETS 6u
#define AXES 3u
#define GYRO_OFFSET 7

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static sp_cap_sample_t sample_at(uint32_t index)
{
	sp_cap_sample_t s;

	for (uint32_t a = 0; a < AXES; a++) {
		s.acc[a] = (int16_t)(index * AXES + a);
		s.gyro[a] = (int16_t)(index * AXES + a + GYRO_OFFSET);
	}
	return s;
}

static int packet_holds(devstream_packet_t p, const devstream_t *s, uint32_t first_raw)
{
	sp_stream_head_t head;
	uint32_t count = (p.len - (uint32_t)sizeof(head)) / (uint32_t)sizeof(sp_cap_sample_t);

	memcpy(&head, p.data, sizeof(head));
	if (head.divider != s->divider || count != s->fit) {
		return 0;
	}
	for (uint32_t i = 0; i < count; i++) {
		sp_cap_sample_t want = sample_at(first_raw + i * s->divider);
		if (memcmp(&p.data[sizeof(head) + i * sizeof(want)], &want, sizeof(want)) != 0) {
			return 0;
		}
	}
	return 1;
}

static void run(devstream_t *s, uint32_t packets)
{
	uint32_t made = 0;

	for (uint32_t raw = 0; made < packets; raw++) {
		sp_cap_sample_t sample = sample_at(raw);

		if (devstream_push(s, &sample)) {
			uint32_t expected_first = s->first;
			devstream_packet_t p = devstream_take(s);
			sp_stream_head_t head;

			memcpy(&head, p.data, sizeof(head));
			check(head.first == expected_first, "each packet says which stream sample it starts with");
			check(packet_holds(p, s, expected_first * s->divider), "a packet holds every divider-th sample in order");
			made++;
		}
	}
}

static void test_rates(void)
{
	for (uint8_t divider = 1; divider <= SP_STREAM_MAX_DIVIDER; divider++) {
		devstream_t s;
		devstream_req_t req = {divider, devstream_fit(FULL_ROOM), START_MS};

		devstream_start(&s, &req);
		check(devstream_on(&s) && s.fit == DEVSTREAM_MAX_SAMPLES, "a full-size link carries 19 samples a packet");
		run(&s, PACKETS);
		check(s.first == PACKETS * DEVSTREAM_MAX_SAMPLES, "the stream index counts streamed samples");
	}
}

static void test_small_link(void)
{
	devstream_t s;
	devstream_req_t req = {2u, devstream_fit(SMALL_ROOM), START_MS};

	devstream_start(&s, &req);
	check(s.fit == (SMALL_ROOM - sizeof(sp_stream_head_t)) / sizeof(sp_cap_sample_t), "a smaller MTU gets fewer samples");
	run(&s, PACKETS);
	check(devstream_fit(TINY_ROOM) == 0u && devstream_fit(NO_ROOM) == 0u, "a link too small for one sample gives no room");
	check(devstream_fit(LARGE_ROOM) == DEVSTREAM_MAX_SAMPLES, "a larger link never overfills the packet buffer");
	req.fit = devstream_fit(TINY_ROOM);
	devstream_start(&s, &req);
	check(!devstream_on(&s), "a stream does not start without room for a sample");
}

static void test_stop_and_expiry(void)
{
	devstream_t s;
	devstream_req_t req = {1u, devstream_fit(FULL_ROOM), START_MS};
	sp_cap_sample_t sample = sample_at(0u);

	devstream_start(&s, &req);
	check(!devstream_expired(&s, START_MS + DEVSTREAM_MAX_MS - 1u), "a stream runs for 10 minutes");
	check(devstream_expired(&s, START_MS + DEVSTREAM_MAX_MS), "and then expires");
	devstream_stop(&s);
	check(!devstream_on(&s) && !devstream_push(&s, &sample), "a stopped stream takes no samples");
	check(!devstream_expired(&s, START_MS + DEVSTREAM_MAX_MS), "a stopped stream does not expire");
	req.divider = 0u;
	devstream_start(&s, &req);
	check(!devstream_on(&s), "divider 0 stops the stream");
}

int main(void)
{
	test_rates();
	test_small_link();
	test_stop_and_expiry();
	printf("DEVSTREAM TESTS: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
