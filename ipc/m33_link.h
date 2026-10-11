/* SPDX-License-Identifier: MIT */
/*
 * m33_link: framed request/response over an RPMsg TTY between the
 * Cortex-A35 (Linux) and the Cortex-M33 (bare-metal OpenAMP).
 *
 * One frame per RPMsg message, little endian:
 *
 *   0       2     3      4        8        10       12
 *   +-------+-----+------+--------+--------+--------+---------------+
 *   | magic | ver | type | seq    | len    | crc16  | payload (len) |
 *   +-------+-----+------+--------+--------+--------+---------------+
 *
 * crc16 is CRC-16/CCITT-FALSE over the header with crc16 = 0, then the
 * payload. The M33 answers PING with PONG (same seq, same payload),
 * GET_STATS (no payload) with STATS, and any frame it rejects with NACK
 * (seq of the request when it could be read, else 0).
 */
#ifndef M33_LINK_H
#define M33_LINK_H

#include <stddef.h>
#include <stdint.h>

#define M33_LINK_MAGIC        0x4C4DU /* "ML" */
#define M33_LINK_VERSION      1U
#define M33_LINK_HDR_SIZE     12U
/* RPMsg buffer 512 B minus the 16 B RPMsg header */
#define M33_LINK_FRAME_MAX    496U
#define M33_LINK_PAYLOAD_MAX  (M33_LINK_FRAME_MAX - M33_LINK_HDR_SIZE)

enum m33_link_type {
	M33_LINK_PING = 1,
	M33_LINK_PONG = 2,
	M33_LINK_NACK = 3,
	M33_LINK_GET_STATS = 4,
	M33_LINK_STATS = 5,
};

enum m33_link_err {
	M33_LINK_OK = 0,
	M33_LINK_E_SHORT = 1,   /* fewer bytes than a header */
	M33_LINK_E_MAGIC = 2,
	M33_LINK_E_VERSION = 3,
	M33_LINK_E_LEN = 4,     /* len does not match the received size */
	M33_LINK_E_CRC = 5,
	M33_LINK_E_TYPE = 6,
};

struct m33_link_hdr {
	uint16_t magic;
	uint8_t ver;
	uint8_t type;
	uint32_t seq;
	uint16_t len;
	uint16_t crc16;
};

/* NACK payload */
struct m33_link_nack {
	uint8_t err;
	uint8_t reserved[3];
	uint32_t rx_ok;
	uint32_t rx_bad;
};

/* STATS payload */
struct m33_link_stats {
	uint32_t rx_ok;
	uint32_t rx_bad;
	uint8_t last_err;
	uint8_t reserved[3];
	uint32_t tick_ms;	/* M33 HAL tick */
};

_Static_assert(sizeof(struct m33_link_hdr) == M33_LINK_HDR_SIZE, "header layout");
_Static_assert(sizeof(struct m33_link_nack) == 12, "nack layout");
_Static_assert(sizeof(struct m33_link_stats) == 16, "stats layout");

static inline uint16_t m33_link_crc16(uint16_t crc, const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)buf[i] << 8;
		for (int b = 0; b < 8; b++)
			crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
	}
	return crc;
}

/* crc over a header copy with crc16 = 0, then the payload */
static inline uint16_t m33_link_frame_crc(const struct m33_link_hdr *hdr, const uint8_t *payload)
{
	struct m33_link_hdr h = *hdr;

	h.crc16 = 0;
	return m33_link_crc16(m33_link_crc16(0xFFFFU, (const uint8_t *)&h, sizeof(h)),
			      payload, hdr->len);
}

#endif /* M33_LINK_H */
