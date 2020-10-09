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

typedef void (*uart_handler_rx_callback_t)(const uint8_t * str);

int uart_handler_init(uart_handler_rx_callback_t rx_cb);
void uart_handler_tx(const uint8_t * str);

#ifdef __cplusplus
}
#endif

#endif /* UART_HANDLER_H__ */
