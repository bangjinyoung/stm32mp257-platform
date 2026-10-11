/* SPDX-License-Identifier: MIT */
#ifndef LINK_SRV_H
#define LINK_SRV_H

#include <stdint.h>

struct link_srv_stats {
	uint32_t rx_ok;
	uint32_t rx_bad;
	uint8_t last_err;
};

/* Check one received frame and build the reply in tx; returns the reply size. */
uint16_t link_srv_handle(const uint8_t *rx, uint16_t rx_len, uint8_t *tx);
const struct link_srv_stats *link_srv_stats(void);

#endif /* LINK_SRV_H */
