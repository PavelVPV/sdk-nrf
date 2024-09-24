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
//#include <nrfx_ppi.h>
//#include <nrfx_dppi.h>
#include <hal/nrf_gpio.h>

/* Generalize PPI or DPPI channel management */
#if defined(PPI_PRESENT)
#include <nrfx_ppi.h>
#define gppi_channel_t nrf_ppi_channel_t
#define gppi_channel_alloc nrfx_ppi_channel_alloc
#define gppi_channel_enable nrfx_ppi_channel_enable
#elif defined(DPPI_PRESENT)
#include <nrfx_dppi.h>
#define gppi_channel_t uint8_t
#define gppi_channel_alloc nrfx_dppi_channel_alloc
#define gppi_channel_enable nrfx_dppi_channel_enable
#else
#error "No PPI or DPPI"
#endif

//#define APP_COUNTER NRF_TIMER20
#define APP_COUNTER NRF_TIMER1
#define APP_COUNTER_RADIO_ACTIVITY_CC 0

#if DT_NODE_HAS_STATUS(DT_PHANDLE(DT_NODELABEL(radio), coex), okay)
#define COEX_NODE DT_PHANDLE(DT_NODELABEL(radio), coex)
#else
#define COEX_NODE DT_INVALID_NODE
#error No enabled coex nodes registered in DTS.
#endif

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, coex_pta_grant_gpios)
#define APP_GRANT_GPIO_PIN NRF_DT_GPIOS_TO_PSEL(ZEPHYR_USER_NODE, coex_pta_grant_gpios)
#else
#error "Unsupported board: see README and check /zephyr,user node"
#define APP_GRANT_GPIO_PIN 0
#endif

static struct gpio_dt_spec app_grant_gpio = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, coex_pta_grant_gpios);

#define APP_GRANT_ACTIVE_LOW                                                                       \
	(GPIO_ACTIVE_LOW & DT_GPIO_FLAGS(COEX_NODE, grant_gpios) ? true : false)

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

//static const nrfx_timer_t app_timer_instance = NRFX_TIMER_INSTANCE(20);
static const nrfx_timer_t app_timer_instance = NRFX_TIMER_INSTANCE(1);

static void print_welcome_message(void)
{
	if (console_init()) {
		__ASSERT(false, "Failed to initialise console");
	}

	printk("-----------------------------------------------------\n");
	printk("This sample illustrates the 1Wire coex interface\n");
	printk("The number of radio events is printed every second.\n");
	printk("\n");
	printk("Press a key to change the state of the grant line:\n");
	if (APP_GRANT_ACTIVE_LOW) {
		printk("'g' to set the grant line to low and allow radio activity\n");
		printk("'d' to set the grant line to high and deny radio activity\n");
	} else {
		printk("'g' to set the grant line to high and allow radio activity\n");
		printk("'d' to set the grant line to low and deny radio activity\n");
	}
	printk("-----------------------------------------------------\n");
}

static void check_input(void)
{
	char input_char;
	int err;

	input_char = console_getchar();

	if ((input_char == 'g') ^ APP_GRANT_ACTIVE_LOW) {
#if 1
		nrf_gpio_pin_set(APP_GRANT_GPIO_PIN);
#else
		err = gpio_pin_set_dt(&app_grant_gpio, 1);
		if (err) {
			printk("Cannot set LED gpio");
		}
#endif
		printk("Current state is: Grant every request\n");
	} else if ((input_char == 'd') ^ APP_GRANT_ACTIVE_LOW) {
#if 1
		nrf_gpio_pin_clear(APP_GRANT_GPIO_PIN);
#else
		err = gpio_pin_set_dt(&app_grant_gpio, 0);
		if (err) {
			printk("Cannot set LED gpio");
		}
#endif
		printk("Current state is: Deny every request\n");
	} else {
		return;
	}
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

#if 0
static nrf_ppi_channel_t allocate_gppi_channel(void)
{
	nrf_ppi_channel_t channel;

	if (nrfx_ppi_channel_alloc(&channel) != NRFX_SUCCESS) {
		__ASSERT(false, "(D)PPI channel allocation error");
	}
	return channel;
}
#endif

#if 1
static void unused_timer_isr_handler(nrf_timer_event_t event_type, void *ctx)
{
	ARG_UNUSED(event_type);
	ARG_UNUSED(ctx);
}
#endif

static void setup_radio_event_counter(void)
{
	/* This function sets up a timer as a counter to count radio events. */
#if 0
	nrf_timer_mode_set(APP_COUNTER, NRF_TIMER_MODE_LOW_POWER_COUNTER);

	nrf_ppi_channel_t channel = allocate_gppi_channel();

	nrfx_gppi_channel_endpoints_setup(
		channel, nrf_radio_event_address_get(NRF_RADIO, NRF_RADIO_EVENT_READY),

		nrf_timer_task_address_get(APP_COUNTER, NRF_TIMER_TASK_COUNT));
	nrfx_ppi_channel_enable(channel);
#elif 1
	gppi_channel_t dppi_channel;
	int ret;

	const nrfx_timer_config_t timer_cfg = {
		.frequency = NRFX_MHZ_TO_HZ(16UL),
		.mode = NRF_TIMER_MODE_TIMER,
		.bit_width = NRF_TIMER_BIT_WIDTH_8,
		.interrupt_priority = NRFX_TIMER_DEFAULT_CONFIG_IRQ_PRIORITY,
		.p_context = NULL};

	ret = nrfx_timer_init(&app_timer_instance, &timer_cfg, unused_timer_isr_handler);
	if (ret != NRFX_SUCCESS) {
		printk("Failed initializing timer (ret: %d)\n", ret - NRFX_ERROR_BASE_NUM);
	}


	ret = gppi_channel_alloc(&dppi_channel);
	if (ret != NRFX_SUCCESS) {
		printk("nrfx DPPI channel alloc error for starting RTC: %d", ret);
	}

	nrfx_gppi_channel_endpoints_setup(dppi_channel,
					  nrfx_timer_task_address_get(&app_timer_instance, NRF_TIMER_TASK_COUNT),
					  nrf_radio_event_address_get(NRF_RADIO, NRF_RADIO_EVENT_READY));

	nrfx_timer_enable(&app_timer_instance);

	nrfx_gppi_channels_enable(BIT(dppi_channel));
#else
	uint8_t dppi_channel;
	int ret;

	nrf_timer_mode_set(APP_COUNTER, NRF_TIMER_MODE_LOW_POWER_COUNTER);

	ret = nrfx_gppi_channel_alloc(&dppi_channel);
	if (ret != NRFX_SUCCESS) {
		printk("nrfx DPPI channel alloc error for starting RTC: %d", ret);
		while(1);
	}

	printk("ready: %p, count: %p\n",
		nrf_radio_event_address_get(NRF_RADIO, NRF_RADIO_EVENT_READY),
		nrf_timer_task_address_get(APP_COUNTER, NRF_TIMER_TASK_COUNT));
	nrfx_gppi_channel_endpoints_setup(
		dppi_channel, nrf_radio_event_address_get(NRF_RADIO, NRF_RADIO_EVENT_READY),

		nrf_timer_task_address_get(APP_COUNTER, NRF_TIMER_TASK_COUNT));
	nrfx_gppi_channels_enable(BIT(dppi_channel));
#endif
}

static void setup_grant_pin(void)
{
#if 1
	nrf_gpio_cfg_output(APP_GRANT_GPIO_PIN);
	if (APP_GRANT_ACTIVE_LOW) {
		nrf_gpio_pin_clear(APP_GRANT_GPIO_PIN);
		printk("Grant line is set to: %s\n", APP_GRANT_ACTIVE_LOW ? "low" : "high");
	} else {
		nrf_gpio_pin_set(APP_GRANT_GPIO_PIN);
		printk("Grant line is set to: %s\n", APP_GRANT_ACTIVE_LOW ? "low" : "high");
	}
#else
	int err;

	err = gpio_pin_configure_dt(&app_grant_gpio, GPIO_OUTPUT);
	if (err) {
		printk("Cannot configure LED gpio");
		return;
	}

	printk("Port: %d, Pin: %d, flags: %d\n", app_grant_gpio.port, app_grant_gpio.pin, app_grant_gpio.dt_flags);

	err = gpio_pin_set_dt(&app_grant_gpio, 1);
	if (err) {
		printk("Cannot set LED gpio");
		return;
	}
#endif
}

int main(void)
{
	printk("Starting Radio Coex Demo 1wire on board %s\n", CONFIG_BOARD);

	if (bt_enable(NULL)) {
		printk("Bluetooth init failed");
		return 0;
	}

	printk("Bluetooth initialized\n");

	setup_grant_pin();
	setup_radio_event_counter();

	if (bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0)) {
		printk("Advertising failed to start");
		return 0;
	}

	printk("Advertising started\n");

	printk("Grant line is set to: %s\n", APP_GRANT_ACTIVE_LOW ? "low" : "high");
	printk("Grant pin is set to: %d\n", APP_GRANT_GPIO_PIN);
	print_welcome_message();

	while (1) {
		k_sleep(K_MSEC(100));
		check_input();
	}
}

K_THREAD_DEFINE(console_print_thread_id, CONFIG_MAIN_STACK_SIZE, console_print_thread, NULL, NULL,
		NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
