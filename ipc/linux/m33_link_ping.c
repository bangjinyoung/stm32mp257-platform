// SPDX-License-Identifier: MIT
/*
 * m33_link_ping: send PING frames to the M33 over an RPMsg TTY, check
 * each PONG and measure the round trip with CLOCK_MONOTONIC.
 *
 * usage: m33_link_ping [-d tty] [-n count] [-s payload] [-t timeout_ms]
 *                      [-o csv] [-x short|magic|version|len|crc|type]
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "m33_link.h"

struct opts {
	const char *tty;
	const char *csv;
	const char *bad;
	unsigned int count;
	unsigned int size;
	int timeout_ms;
};

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int tty_open(const char *path)
{
	struct termios tio;
	int fd = open(path, O_RDWR | O_NOCTTY);

	if (fd < 0)
		return -1;
	if (tcgetattr(fd, &tio) == 0) {
		cfmakeraw(&tio);
		tcsetattr(fd, TCSANOW, &tio);
	}
	tcflush(fd, TCIOFLUSH);
	return fd;
}

/* read exactly len bytes or fail after timeout_ms in total */
static int read_full(int fd, uint8_t *buf, size_t len, int timeout_ms, uint64_t deadline)
{
	size_t got = 0;

	while (got < len) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		int64_t left = (int64_t)(deadline - now_ns()) / 1000000;
		ssize_t n;

		if (left <= 0)
			return -ETIMEDOUT;
		if (poll(&pfd, 1, (int)(left < timeout_ms ? left : timeout_ms)) <= 0)
			return -ETIMEDOUT;
		n = read(fd, buf + got, len - got);
		if (n < 0)
			return -errno;
		got += (size_t)n;
	}
	return 0;
}

static size_t build_ping(uint8_t *frame, uint32_t seq, unsigned int size)
{
	struct m33_link_hdr hdr = {
		.magic = M33_LINK_MAGIC,
		.ver = M33_LINK_VERSION,
		.type = M33_LINK_PING,
		.seq = seq,
		.len = (uint16_t)size,
	};
	uint8_t *payload = frame + sizeof(hdr);

	for (unsigned int i = 0; i < size; i++)
		payload[i] = (uint8_t)(seq + i);
	hdr.crc16 = m33_link_frame_crc(&hdr, payload);
	memcpy(frame, &hdr, sizeof(hdr));
	return sizeof(hdr) + size;
}

/* corrupt a valid frame the way -x asks; returns the size to send */
static size_t corrupt(uint8_t *frame, size_t len, const char *how)
{
	struct m33_link_hdr hdr;

	memcpy(&hdr, frame, sizeof(hdr));
	if (!strcmp(how, "short"))
		return 6;
	if (!strcmp(how, "magic"))
		hdr.magic ^= 0xFFFF;
	else if (!strcmp(how, "version"))
		hdr.ver = M33_LINK_VERSION + 1;
	else if (!strcmp(how, "len"))
		hdr.len += 4;
	else if (!strcmp(how, "type"))
		hdr.type = M33_LINK_PONG;
	if (!strcmp(how, "type"))
		hdr.crc16 = m33_link_frame_crc(&hdr, frame + sizeof(hdr));
	memcpy(frame, &hdr, sizeof(hdr));
	if (!strcmp(how, "crc"))
		frame[sizeof(hdr)] ^= 0x01;
	return len;
}

/* read one reply frame; returns its header and payload */
static int read_reply(int fd, struct m33_link_hdr *hdr, uint8_t *payload, int timeout_ms)
{
	uint64_t deadline = now_ns() + (uint64_t)timeout_ms * 1000000ULL;
	int ret = read_full(fd, (uint8_t *)hdr, sizeof(*hdr), timeout_ms, deadline);

	if (ret)
		return ret;
	if (hdr->magic != M33_LINK_MAGIC || hdr->len > M33_LINK_PAYLOAD_MAX) {
		tcflush(fd, TCIFLUSH);
		return -EPROTO;
	}
	ret = read_full(fd, payload, hdr->len, timeout_ms, deadline);
	if (ret)
		return ret;
	if (m33_link_frame_crc(hdr, payload) != hdr->crc16)
		return -EBADMSG;
	return 0;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return (x > y) - (x < y);
}

static int run_negative(int fd, const struct opts *o)
{
	uint8_t frame[M33_LINK_FRAME_MAX], payload[M33_LINK_FRAME_MAX];
	struct m33_link_hdr hdr;
	struct m33_link_nack nack;
	size_t len = corrupt(frame, build_ping(frame, 0xBAD0, o->size), o->bad);
	int ret;

	if (write(fd, frame, len) != (ssize_t)len) {
		printf("negative %s: write failed: %s\n", o->bad, strerror(errno));
		return 1;
	}
	ret = read_reply(fd, &hdr, payload, o->timeout_ms);
	if (ret) {
		printf("negative %s: no valid reply (%s)\n", o->bad, strerror(-ret));
		return 1;
	}
	if (hdr.type != M33_LINK_NACK || hdr.len != sizeof(nack)) {
		printf("negative %s: expected NACK, got type %u len %u\n", o->bad, hdr.type, hdr.len);
		return 1;
	}
	memcpy(&nack, payload, sizeof(nack));
	printf("negative %s: NACK err=%u seq=0x%x rx_ok=%u rx_bad=%u\n", o->bad, nack.err,
	       hdr.seq, nack.rx_ok, nack.rx_bad);
	return 0;
}

static int run_ping(int fd, const struct opts *o)
{
	uint8_t frame[M33_LINK_FRAME_MAX], payload[M33_LINK_FRAME_MAX];
	uint64_t *rtt = calloc(o->count, sizeof(*rtt));
	unsigned int ok = 0, bad = 0, lost = 0, nacked = 0;
	FILE *csv = o->csv ? fopen(o->csv, "w") : NULL;
	uint64_t sum = 0;

	if (!rtt)
		return 1;
	if (csv)
		fprintf(csv, "seq,rtt_ns,result\n");
	for (uint32_t seq = 1; seq <= o->count; seq++) {
		struct m33_link_hdr hdr;
		size_t len = build_ping(frame, seq, o->size);
		const char *result = "ok";
		uint64_t t0, t1;
		int ret;

		t0 = now_ns();
		if (write(fd, frame, len) != (ssize_t)len) {
			result = "write";
			lost++;
		} else if ((ret = read_reply(fd, &hdr, payload, o->timeout_ms)) != 0) {
			result = ret == -ETIMEDOUT ? "timeout" : "frame";
			lost += ret == -ETIMEDOUT;
			bad += ret != -ETIMEDOUT;
		} else if (hdr.type == M33_LINK_NACK) {
			result = "nack";
			nacked++;
		} else if (hdr.type != M33_LINK_PONG || hdr.seq != seq || hdr.len != o->size ||
			   memcmp(payload, frame + sizeof(hdr), o->size)) {
			result = "mismatch";
			bad++;
		}
		t1 = now_ns();
		if (!strcmp(result, "ok")) {
			rtt[ok++] = t1 - t0;
			sum += t1 - t0;
		}
		if (csv)
			fprintf(csv, "%u,%llu,%s\n", seq, (unsigned long long)(t1 - t0), result);
	}
	if (csv)
		fclose(csv);

	printf("m33_link_ping tty=%s n=%u payload=%u ok=%u bad=%u nack=%u lost=%u\n",
	       o->tty, o->count, o->size, ok, bad, nacked, lost);
	if (ok) {
		qsort(rtt, ok, sizeof(*rtt), cmp_u64);
		printf("rtt_us min=%.1f avg=%.1f p50=%.1f p99=%.1f max=%.1f\n",
		       rtt[0] / 1e3, (double)sum / ok / 1e3, rtt[ok / 2] / 1e3,
		       rtt[(ok * 99) / 100 < ok ? (ok * 99) / 100 : ok - 1] / 1e3, rtt[ok - 1] / 1e3);
	}
	free(rtt);
	return ok == o->count ? 0 : 1;
}

int main(int argc, char **argv)
{
	struct opts o = {
		.tty = "/dev/ttyRPMSG0",
		.count = 1000,
		.size = 32,
		.timeout_ms = 1000,
	};
	int opt, fd, ret;

	while ((opt = getopt(argc, argv, "d:n:s:t:o:x:")) != -1) {
		switch (opt) {
		case 'd': o.tty = optarg; break;
		case 'n': o.count = (unsigned int)strtoul(optarg, NULL, 0); break;
		case 's': o.size = (unsigned int)strtoul(optarg, NULL, 0); break;
		case 't': o.timeout_ms = atoi(optarg); break;
		case 'o': o.csv = optarg; break;
		case 'x': o.bad = optarg; break;
		default:
			fprintf(stderr, "usage: %s [-d tty] [-n count] [-s payload] [-t ms] [-o csv] "
				"[-x short|magic|version|len|crc|type]\n", argv[0]);
			return 2;
		}
	}
	if (o.size > M33_LINK_PAYLOAD_MAX || o.count == 0) {
		fprintf(stderr, "payload max %u, count > 0\n", M33_LINK_PAYLOAD_MAX);
		return 2;
	}

	fd = tty_open(o.tty);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", o.tty, strerror(errno));
		return 1;
	}
	ret = o.bad ? run_negative(fd, &o) : run_ping(fd, &o);
	close(fd);
	return ret;
}
