/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/__assert.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/console/console.h>
#include <zephyr/drivers/gpio.h>

#include <hal/nrf_radio.h>
#include <hal/nrf_timer.h>
#include <helpers/nrfx_gppi.h>
#include <nrfx_gpiote.h>
#include <nrfx_timer.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_egu.h>
#include <nrfx_dppi.h>
#include <hal/nrf_ppib.h>

/* Predefined channels for radio events. */
#include <protocol/mpsl_dppi_protocol_api.h>

#include <zephyr/drivers/clock_control/nrf_clock_control.h>

#define APP_COUNTER ((NRF_TIMER_Type *) DT_REG_ADDR(DT_ALIAS(timer)))
#define APP_COUNTER_RADIO_ACTIVITY_CC 0

#define APP_GRANT_ACTIVE_LOW                                                                       \
	(GPIO_ACTIVE_LOW & DT_GPIO_FLAGS(COEX_NODE, grant_gpios) ? true : false)

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct device *sample_clock_dev = DEVICE_DT_GET(DT_CLOCKS_CTLR(DT_NODELABEL(timer022)));
static struct onoff_client cli;

#define CONFIG_SAMPLE_CLOCK_FREQUENCY_HZ 16000000
#define CONFIG_SAMPLE_CLOCK_ACCURACY_PPM NRF_CLOCK_CONTROL_ACCURACY_MAX
#define CONFIG_SAMPLE_CLOCK_PRECISION 0

#define SAMPLE_NOTIFY_TIMEOUT       K_SECONDS(2)

static const struct nrf_clock_spec spec = {
	.frequency = CONFIG_SAMPLE_CLOCK_FREQUENCY_HZ,
	.accuracy = CONFIG_SAMPLE_CLOCK_ACCURACY_PPM,
	.precision = CONFIG_SAMPLE_CLOCK_PRECISION,
};

static K_SEM_DEFINE(sample_sem, 0, 1);

static void sample_notify_cb(void)
{
	k_sem_give(&sample_sem);
}

static void console_print_thread(void)
{
	while (1) {
		nrf_timer_task_trigger(APP_COUNTER,
				       nrf_timer_capture_task_get(APP_COUNTER_RADIO_ACTIVITY_CC));

		printk("Number of radio events in the last second: %d\n",
		       nrf_timer_cc_get(APP_COUNTER, APP_COUNTER_RADIO_ACTIVITY_CC));

		nrf_timer_task_trigger(APP_COUNTER, NRF_TIMER_TASK_CLEAR);

		k_sleep(K_MSEC(1000));
	}
}

static int clock_setup(void)
{
	int ret;
	int res;
	int64_t req_start_uptime;
	int64_t req_stop_uptime;

	printk("\n");
	printk("minimum frequency request: %uHz\n", CONFIG_SAMPLE_CLOCK_FREQUENCY_HZ);
	printk("minimum accuracy request: %uPPM\n", CONFIG_SAMPLE_CLOCK_ACCURACY_PPM);
	printk("minimum precision request: %u\n", CONFIG_SAMPLE_CLOCK_PRECISION);

	sys_notify_init_callback(&cli.notify, sample_notify_cb);

	printk("\n");
	printk("requesting minimum clock specs\n");
	req_start_uptime = k_uptime_get();
	ret = nrf_clock_control_request(sample_clock_dev, &spec, &cli);
	if (ret < 0) {
		printk("minimum clock specs could not be met\n");
		return 0;
	}

	ret = k_sem_take(&sample_sem, SAMPLE_NOTIFY_TIMEOUT);
	if (ret < 0) {
		printk("timed out waiting for clock to meet request\n");
		return 0;
	}

	req_stop_uptime = k_uptime_get();

	ret = sys_notify_fetch_result(&cli.notify, &res);
	if (ret < 0) {
		printk("sys notify fetch failed\n");
		return 0;
	}

	if (res < 0) {
		printk("failed to apply request to clock\n");
		return 0;
	}

	printk("request applied to clock in %llims\n", req_stop_uptime - req_start_uptime);

	return 0;
}

static void setup_radio_event_counter(void)
{
	/* This function sets up a timer as a counter to count radio events. */
	nrf_timer_mode_set(APP_COUNTER, NRF_TIMER_MODE_LOW_POWER_COUNTER);

	/* Radio events are published on predefined channels.
	 */
	uint8_t ready_channel = MPSL_DPPI_RADIO_PUBLISH_READY_CHANNEL_IDX;

	NRF_DPPI_ENDPOINT_SETUP(nrf_timer_task_address_get(APP_COUNTER, NRF_TIMER_TASK_COUNT),
				ready_channel);
}

int main(void)
{
	printk("Starting Radio Coex Demo 1wire on board %s\n", CONFIG_BOARD);

	clock_setup();

	if (bt_enable(NULL)) {
		printk("Bluetooth init failed");
		return 0;
	}

	printk("Bluetooth initialized\n");

	setup_radio_event_counter();

	if (bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0)) {
		printk("Advertising failed to start");
		return 0;
	}

	printk("Advertising started\n");

	while (1) {
		k_sleep(K_FOREVER);
	}
}

K_THREAD_DEFINE(console_print_thread_id, CONFIG_MAIN_STACK_SIZE, console_print_thread, NULL, NULL,
		NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
