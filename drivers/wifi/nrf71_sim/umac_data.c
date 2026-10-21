/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "nsim_wifi_if.h"
#include "umac_sim.h"

LOG_MODULE_DECLARE(nrf71_sim, CONFIG_WIFI_NRF71_SIM_LOG_LEVEL);

#define RX_BUF_MAX 64
#define ETH_HLEN   14
#define SNAP_LEN   8

/* Host RX buffers given by CONFIG_RX_BUF, used as a FIFO. */
int16_t umac_sim_rx_signal_mbm = -3000;

static struct nrf_wifi_rx_buf rx_bufs[RX_BUF_MAX];
static unsigned int rx_head;
static unsigned int rx_cnt;

void umac_data_rx_buf_cfg(const struct nrf_wifi_umac_hdr *hdr)
{
	const struct nrf_wifi_cmd_rx_buf_info *cfg = (const void *)hdr;

	for (unsigned int i = 0; i < cfg->rx_buf_num; i++) {
		if (rx_cnt == RX_BUF_MAX) {
			LOG_ERR("RX buffer pool full");
			break;
		}
		rx_bufs[(rx_head + rx_cnt) % RX_BUF_MAX] = cfg->info[i];
		rx_cnt++;
	}

	LOG_DBG("CONFIG_RX_BUF +%u, free %u", cfg->rx_buf_num, rx_cnt);
	cmd_status_send(hdr, 0);
}

void umac_data_tx(const struct nrf_wifi_tx_buff *tx)
{
	uint8_t buf[sizeof(struct host_rpu_msg) + sizeof(struct nrf_wifi_tx_buff_done) +
		    tx->num_tx_pkts];
	struct host_rpu_msg *msg = (void *)buf;
	struct nrf_wifi_tx_buff_done *done = (void *)msg->msg;

	memset(buf, 0, sizeof(buf));

	for (unsigned int i = 0; i < tx->num_tx_pkts; i++) {
		const uint8_t *frame = (const uint8_t *)tx->tx_buff_info[i].ddr_ptr;
		int len = tx->tx_buff_info[i].pkt_length;
		int ret = umac_sim_host_ready ? nsim_wifi_pkt_send(frame, len) : -ENODEV;

		LOG_DBG("TX %d B type 0x%02x%02x: %d", len, frame[12], frame[13], ret);
		done->tx_status_code[i] = ret ? NRF_WIFI_TX_STATUS_FAILED :
						NRF_WIFI_TX_STATUS_SUCCESS;
	}

	msg->hdr.len = sizeof(buf);
	msg->type = NRF_WIFI_HOST_RPU_MSG_TYPE_DATA;
	done->umac_head.cmd = NRF_WIFI_CMD_TX_BUFF_DONE;
	done->umac_head.len = sizeof(buf) - sizeof(struct host_rpu_msg);
	done->tx_desc_num = tx->tx_desc_num;
	done->num_tx_status_code = tx->num_tx_pkts;
	umac_sim_evt_send(buf, sizeof(buf));
}

static void rx_send(const uint8_t *eth, int len)
{
	struct {
		struct host_rpu_msg msg;
		struct nrf_wifi_rx_buff rx;
		struct nrf_wifi_rx_buff_info info;
	} __packed evt = { 0 };
	struct nrf_wifi_rx_buf *buf = &rx_bufs[rx_head];
	uint8_t *dst = (uint8_t *)buf->skb_pointer;
	int payload = len - ETH_HLEN;
	static const uint8_t snap[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

	/* MSDU layout: dst, src, length, LLC/SNAP, ethertype, payload. */
	memcpy(dst, eth, 12);
	sys_put_be16(SNAP_LEN + payload, &dst[12]);
	memcpy(&dst[ETH_HLEN], snap, sizeof(snap));
	memcpy(&dst[ETH_HLEN + 6], &eth[12], 2);
	memcpy(&dst[ETH_HLEN + SNAP_LEN], &eth[ETH_HLEN], payload);

	rx_head = (rx_head + 1) % RX_BUF_MAX;
	rx_cnt--;

	evt.msg.hdr.len = sizeof(evt);
	evt.msg.type = NRF_WIFI_HOST_RPU_MSG_TYPE_DATA;
	evt.rx.umac_head.cmd = NRF_WIFI_CMD_RX_BUFF;
	evt.rx.umac_head.len = sizeof(evt) - sizeof(evt.msg);
	evt.rx.rx_pkt_type = NRF_WIFI_RX_PKT_DATA;
	evt.rx.rx_pkt_cnt = 1;
	evt.rx.signal = umac_sim_rx_signal_mbm;
	evt.info.descriptor_id = buf->skb_desc_no;
	evt.info.rx_pkt_len = ETH_HLEN + SNAP_LEN + payload;
	evt.info.pkt_type = PKT_TYPE_MSDU;

	LOG_DBG("RX %d B type 0x%02x%02x desc %u", len, eth[12], eth[13], buf->skb_desc_no);
	umac_sim_evt_send(&evt, sizeof(evt));
}

void umac_data_rx_poll(void)
{
	static uint8_t frame[1600];
	int len;

	/* Leave frames in the socket while the host has no RX buffer. */
	while (rx_cnt > 0 && (len = nsim_wifi_pkt_recv(frame, sizeof(frame))) > 0) {
		if (len > ETH_HLEN && len <= CONFIG_NRF71_RX_MAX_DATA_SIZE - SNAP_LEN) {
			rx_send(frame, len);
		}
	}
}
