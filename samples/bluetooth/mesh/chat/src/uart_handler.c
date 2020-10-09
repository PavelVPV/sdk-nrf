#include <zephyr/types.h>
#include <zephyr.h>
#include <drivers/uart.h>

#include <device.h>
#include <soc.h>

#include "uart_handler.h"

#include <logging/log.h>
LOG_MODULE_DECLARE(chat, 4);

#define STACKSIZE 1024
#define PRIORITY 7

#define UART_BUF_SIZE CONFIG_BT_MESH_CHAT_MESSAGE_BUFFER_SIZE + 2 /* + \n\r */
#define UART_WAIT_FOR_BUF_DELAY K_MSEC(50)
#define UART_WAIT_FOR_RX 50

static struct device *uart;
static struct k_delayed_work uart_work;

struct uart_data_t {
	void *fifo_reserved;
	uint8_t data[UART_BUF_SIZE];
	uint16_t len;
};

static K_SEM_DEFINE(uart_init_ok, 0, 1);

static K_FIFO_DEFINE(fifo_uart_tx_data);
static K_FIFO_DEFINE(fifo_uart_rx_data);

static uart_handler_rx_callback_t callback;

static void uart_cb(struct device *dev, struct uart_event *evt, void *user_data)
{
	ARG_UNUSED(dev);

	static uint8_t *current_buf;
	static size_t aborted_len;
	static bool buf_release;
	struct uart_data_t *buf;
	static uint8_t *aborted_buf;

	LOG_WRN("uart_cb: %d", evt->type);

	switch (evt->type) {
	case UART_TX_DONE:
		if ((evt->data.tx.len == 0) ||
		    (!evt->data.tx.buf)) {
			LOG_WRN("No buf");
			return;
		}

		if (aborted_buf) {
			buf = CONTAINER_OF(aborted_buf, struct uart_data_t,
					   data);
			aborted_buf = NULL;
			aborted_len = 0;
		} else {
			buf = CONTAINER_OF(evt->data.tx.buf, struct uart_data_t,
					   data);
		}

		k_free(buf);

		buf = k_fifo_get(&fifo_uart_tx_data, K_NO_WAIT);
		if (!buf) {
			LOG_WRN("Fifo empty");
			return;
		}

		LOG_WRN("Sending len: %d", buf->len);

		if (uart_tx(uart, buf->data, buf->len, SYS_FOREVER_MS)) {
			LOG_WRN("Failed to send data over UART");
		}

		break;

	case UART_RX_RDY:
		buf = CONTAINER_OF(evt->data.rx.buf, struct uart_data_t, data);
		buf->len += evt->data.rx.len;
		buf_release = false;

		if (buf->len == UART_BUF_SIZE) {
			k_fifo_put(&fifo_uart_rx_data, buf);
		} else if ((evt->data.rx.buf[buf->len - 1] == '\n') ||
			  (evt->data.rx.buf[buf->len - 1] == '\r')) {
			buf->data[buf->len - 1] = '\0';

			k_fifo_put(&fifo_uart_rx_data, buf);
			current_buf = evt->data.rx.buf;
			buf_release = true;

			uart_rx_disable(uart);
		}

		break;

	case UART_RX_DISABLED:
		buf = k_malloc(sizeof(*buf));
		if (buf) {
			buf->len = 0;
		} else {
			LOG_WRN("Not able to allocate UART receive buffer");
			k_delayed_work_submit(&uart_work,
					      UART_WAIT_FOR_BUF_DELAY);
			return;
		}

		uart_rx_enable(uart, buf->data, sizeof(buf->data),
			       UART_WAIT_FOR_RX);

		break;

	case UART_RX_BUF_REQUEST:
		buf = k_malloc(sizeof(*buf));
		if (buf) {
			buf->len = 0;
			uart_rx_buf_rsp(uart, buf->data, sizeof(buf->data));
		} else {
			LOG_WRN("Not able to allocate UART receive buffer");
		}

		break;

	case UART_RX_BUF_RELEASED:
		buf = CONTAINER_OF(evt->data.rx_buf.buf, struct uart_data_t,
				   data);
		if (buf_release && (current_buf != evt->data.rx_buf.buf)) {
			k_free(buf);
			buf_release = false;
			current_buf = NULL;
		}

		break;

	case UART_TX_ABORTED:
		if (!aborted_buf) {
			aborted_buf = (uint8_t *)evt->data.tx.buf;
		}

		aborted_len += evt->data.tx.len;
		buf = CONTAINER_OF(aborted_buf, struct uart_data_t,
				   data);

		uart_tx(uart, &buf->data[aborted_len],
			buf->len - aborted_len, SYS_FOREVER_MS);
		break;

	default:
		break;
	}
}

static void uart_read_thread(void)
{
	/* Don't go any further until BLE is initialized */
	k_sem_take(&uart_init_ok, K_FOREVER);

	for (;;) {
		/* Wait indefinitely for data to be sent over bluetooth */
		struct uart_data_t *buf = k_fifo_get(&fifo_uart_rx_data,
						     K_FOREVER);

		callback(buf->data);

		k_free(buf);
	}
}

K_THREAD_DEFINE(uart_read_thread_id, STACKSIZE, uart_read_thread, NULL, NULL,
		NULL, PRIORITY, 0, 0);

static void uart_work_handler(struct k_work *item)
{
	struct uart_data_t *buf;

	buf = k_malloc(sizeof(*buf));
	if (buf) {
		buf->len = 0;
	} else {
		LOG_WRN("Not able to allocate UART receive buffer");
		k_delayed_work_submit(&uart_work, UART_WAIT_FOR_BUF_DELAY);
		return;
	}

	uart_rx_enable(uart, buf->data, sizeof(buf->data), UART_WAIT_FOR_RX);
}

int uart_handler_init(uart_handler_rx_callback_t rx_cb)
{
	int err;
	struct uart_data_t *rx;

	uart = device_get_binding(DT_LABEL(DT_NODELABEL(uart0)));
	if (!uart) {
		return -ENXIO;
	}

	rx = k_malloc(sizeof(*rx));
	if (rx) {
		rx->len = 0;
	} else {
		return -ENOMEM;
	}

	k_delayed_work_init(&uart_work, uart_work_handler);

	err = uart_callback_set(uart, uart_cb, NULL);
	if (err) {
		return err;
	}

	err = uart_rx_enable(uart, rx->data, sizeof(rx->data), UART_WAIT_FOR_RX);
	if (err) {
		return err;
	}

	callback = rx_cb;
	k_sem_give(&uart_init_ok);

	return 0;
}

void uart_handler_tx(const uint8_t * str)
{
	struct uart_data_t *tx = k_malloc(sizeof(*tx));
	int err;

	if (!tx) {
		LOG_WRN("Not able to allocate UART send data buffer");
		return;
	}

	tx->len = strnlen(str, sizeof(tx->data) - 2);
	memcpy(tx->data, str, tx->len);

	tx->data[tx->len++] = '\n';

	err = uart_tx(uart, tx->data, tx->len, SYS_FOREVER_MS);
	if (err) {
		LOG_WRN("Failed to send data immediately: %d", err);
		k_fifo_put(&fifo_uart_tx_data, tx);
	}
}
