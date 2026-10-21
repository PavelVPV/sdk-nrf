/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef NRF71_UMAC_SIM_H__
#define NRF71_UMAC_SIM_H__

#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include <common/fw_if/nrf71_wifi_ctrl.h>

/* Work queue that runs all UMAC sim logic. Keeps command and event order. */
extern struct k_work_q umac_sim_wq;

/* Deliver one event message (host_rpu_msg) to the host, now or after delay_ms. */
void umac_sim_evt_send(const void *msg, size_t len);
void umac_sim_evt_send_delayed(const void *msg, size_t len, uint32_t delay_ms);

/* Handle one host message (host_rpu_msg). Runs on umac_sim_wq. */
void umac_sim_handle_msg(const uint8_t *msg, size_t len);

/* Send a UMAC event. evt starts with umac_hdr; seq and ids are copied from cmd_hdr. */
void umac_evt_send(const struct nrf_wifi_umac_hdr *cmd_hdr, unsigned int evt_id,
		   void *evt, size_t evt_len, uint32_t delay_ms);
void cmd_status_send(const struct nrf_wifi_umac_hdr *cmd_hdr, unsigned int status);

/* True when the hwsim STA is open (nl80211 + packet socket). */
extern bool umac_sim_host_ready;

void umac_scan_trigger(const struct nrf_wifi_umac_hdr *hdr);
void umac_scan_host_evt(int evt);

void umac_conn_cmd(const struct nrf_wifi_umac_hdr *hdr, size_t len);
void umac_conn_host_evt(int evt, const uint8_t *frame, int frame_len);

/* Signal in mBm put in RX_BUFF events. Updated from the kernel station info. */
extern int16_t umac_sim_rx_signal_mbm;

void umac_data_rx_buf_cfg(const struct nrf_wifi_umac_hdr *hdr);
void umac_data_tx(const struct nrf_wifi_tx_buff *tx);
void umac_data_rx_poll(void);
void umac_data_carrier_send(unsigned int wdev_id, bool on);

#endif /* NRF71_UMAC_SIM_H__ */
