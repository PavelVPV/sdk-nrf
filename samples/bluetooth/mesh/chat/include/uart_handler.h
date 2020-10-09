/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**
 * @file
 * @brief Model handler
 */

#ifndef UART_HANDLER_H__
#define UART_HANDLER_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*uart_handler_rx_callback_t)(const uint8_t * data, uint8_t len);

int uart_handler_init(uart_handler_rx_callback_t rx_cb);
void uart_handler_tx(const uint8_t *const data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* UART_HANDLER_H__ */
