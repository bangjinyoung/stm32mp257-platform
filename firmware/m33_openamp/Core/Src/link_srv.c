// SPDX-License-Identifier: MIT
/*
 * m33_link server: answers PING with PONG and rejects malformed frames
 * with NACK. Runs from the main loop, never from the RPMsg callback.
 */
#include <string.h>
#include "m33_link.h"
#include "link_srv.h"

static struct link_srv_stats stats;

static uint16_t build_frame(uint8_t *tx, uint8_t type, uint32_t seq,
			    const uint8_t *payload, uint16_t len)
{
	struct m33_link_hdr hdr = {
		.magic = M33_LINK_MAGIC,
		.ver = M33_LINK_VERSION,
		.type = type,
		.seq = seq,
		.len = len,
		.crc16 = 0,
	};

	hdr.crc16 = m33_link_frame_crc(&hdr, payload);
	memcpy(tx, &hdr, sizeof(hdr));
	memmove(tx + sizeof(hdr), payload, len);
	return (uint16_t)(sizeof(hdr) + len);
}

static uint8_t check_frame(const uint8_t *rx, uint16_t rx_len, struct m33_link_hdr *hdr)
{
	if (rx_len < M33_LINK_HDR_SIZE)
		return M33_LINK_E_SHORT;
	memcpy(hdr, rx, sizeof(*hdr));
	if (hdr->magic != M33_LINK_MAGIC)
		return M33_LINK_E_MAGIC;
	if (hdr->ver != M33_LINK_VERSION)
		return M33_LINK_E_VERSION;
	if (hdr->len > M33_LINK_PAYLOAD_MAX || M33_LINK_HDR_SIZE + hdr->len != rx_len)
		return M33_LINK_E_LEN;
	if (m33_link_frame_crc(hdr, rx + M33_LINK_HDR_SIZE) != hdr->crc16)
		return M33_LINK_E_CRC;
	if (hdr->type != M33_LINK_PING)
		return M33_LINK_E_TYPE;
	return M33_LINK_OK;
}

uint16_t link_srv_handle(const uint8_t *rx, uint16_t rx_len, uint8_t *tx)
{
	struct m33_link_hdr hdr = { 0 };
	struct m33_link_nack nack = { 0 };
	uint8_t err = check_frame(rx, rx_len, &hdr);

	if (err == M33_LINK_OK) {
		stats.rx_ok++;
		return build_frame(tx, M33_LINK_PONG, hdr.seq, rx + M33_LINK_HDR_SIZE, hdr.len);
	}

	stats.rx_bad++;
	stats.last_err = err;
	nack.err = err;
	nack.rx_ok = stats.rx_ok;
	nack.rx_bad = stats.rx_bad;
	/* seq is only trusted once magic and version matched */
	return build_frame(tx, M33_LINK_NACK, err > M33_LINK_E_VERSION ? hdr.seq : 0,
			   (const uint8_t *)&nack, sizeof(nack));
}

const struct link_srv_stats *link_srv_stats(void)
{
	return &stats;
}
