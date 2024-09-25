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

#include <protocol/mpsl_dppi_protocol_api.h>

#if 0
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
#endif

#define APP_COUNTER ((NRF_TIMER_Type *) DT_REG_ADDR(DT_ALIAS(timer)))
#define APP_COUNTER_RADIO_ACTIVITY_CC 4

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

#define APP_GRANT_ACTIVE_LOW                                                                       \
	(GPIO_ACTIVE_LOW & DT_GPIO_FLAGS(COEX_NODE, grant_gpios) ? true : false)

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define PRINT_REGS() do { \
		printk("%d: RADIO: %x, TIMER10: %x, EGU->S: %x, EGU->P: %x\n", __LINE__, \
		       *(unsigned int *) 0x5008A300, *(unsigned int *) 0x50085088, \
		       *(unsigned int *) 0x500C9080, *(unsigned int *) 0x500C9180); \
	} while (0)

static const nrfx_timer_t app_timer_instance = NRFX_TIMER_INSTANCE(20);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

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

static bool granted;
static uint8_t channel = MPSL_DPPI_RADIO_PUBLISH_READY_CHANNEL_IDX;

static void check_input(void)
{
	char input_char;
	int err;

#if 0
	nrfx_gppi_channel_endpoints_setup(channel,
		nrf_radio_event_address_get(NRF_RADIO, NRF_RADIO_EVENT_READY),
#if 1
		nrf_timer_task_address_get(APP_COUNTER, NRF_TIMER_TASK_COUNT));
#else
		nrfx_timer_task_address_get(&app_timer_instance, NRF_TIMER_TASK_COUNT));
#endif
#endif
	PRINT_REGS();

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
		granted = true;
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
		granted = false;
		printk("Current state is: Deny every request\n");
	} else {
		return;
	}
	PRINT_REGS();
}

static uint32_t egu_counter;

static void console_print_thread(void)
{
	while (1) {
		uint32_t capture = 0;

		PRINT_REGS();
#if 0
#if 0
		do {
			k_yield();
			nrf_timer_task_trigger(APP_COUNTER,
				       nrf_timer_capture_task_get(APP_COUNTER_RADIO_ACTIVITY_CC));
			capture = nrf_timer_cc_get(APP_COUNTER, APP_COUNTER_RADIO_ACTIVITY_CC);
		} while (capture == 0 && granted);
#else
		for (int i = 0; i < 12; i++) {
			nrf_timer_task_trigger(APP_COUNTER, NRF_TIMER_TASK_COUNT);
		}
		nrf_timer_task_trigger(APP_COUNTER,
				       nrf_timer_capture_task_get(APP_COUNTER_RADIO_ACTIVITY_CC));
		capture = nrf_timer_cc_get(APP_COUNTER, APP_COUNTER_RADIO_ACTIVITY_CC);
#endif

		printk("Egu counter: %d\n", egu_counter);

		nrf_timer_task_trigger(APP_COUNTER, NRF_TIMER_TASK_CLEAR);
#else
		capture = nrfx_timer_capture(&app_timer_instance, NRF_TIMER_CC_CHANNEL4);
		nrfx_timer_clear(&app_timer_instance);
#endif
		printk("Number of radio events in the last second: %d\n", capture);
		printk("DPPIC channels set: %x\n", *(unsigned int *) 0x50082504);
//		nrfy_dppi_channels_set((NRF_DPPIC_Type *) 0x50082000, NRFX_BIT((uint32_t)channel), true);
		PRINT_REGS();

		k_sleep(K_MSEC(1000));
	}
}

static uint8_t allocate_gppi_channel(void)
{
	uint8_t channel;

	if (nrfx_gppi_channel_alloc(&channel) != NRFX_SUCCESS) {
		__ASSERT(false, "(D)PPI channel allocation error");
	}
	return channel;
}

static void unused_timer_isr_handler(nrf_timer_event_t event_type, void *ctx)
{
	ARG_UNUSED(event_type);
	ARG_UNUSED(ctx);
}

static void egu_handler(const void *context)
{
	egu_counter++;
}

static void setup_radio_event_counter(void)
{
#if 0
	/* This function sets up a timer as a counter to count radio events. */
	//nrf_timer_mode_set(APP_COUNTER, NRF_TIMER_MODE_LOW_POWER_COUNTER);
	nrf_timer_mode_set(APP_COUNTER, NRF_TIMER_MODE_LOW_POWER_COUNTER);
#else
	int ret;
	const nrfx_timer_config_t timer_cfg = {
		.frequency = NRFX_MHZ_TO_HZ(16UL),
		.mode = NRF_TIMER_MODE_LOW_POWER_COUNTER,
		.bit_width = NRF_TIMER_BIT_WIDTH_24,
		.interrupt_priority = NRFX_TIMER_DEFAULT_CONFIG_IRQ_PRIORITY,
		.p_context = NULL};
	ret = nrfx_timer_init(&app_timer_instance, &timer_cfg, unused_timer_isr_handler);
	if (ret != NRFX_SUCCESS) {
		printk("Failed initializing timer (ret: %d)\n", ret - NRFX_ERROR_BASE_NUM);
	}
#endif

//	/*uint8_t*/ channel = allocate_gppi_channel();
//	channel = MPSL_DPPI_RADIO_PUBLISH_READY_CHANNEL_IDX;
	printk("Channel: %d\n", channel);

#if 0
	nrfx_gppi_fork_endpoint_setup(channel, nrf_timer_task_address_get(APP_COUNTER, NRF_TIMER_TASK_COUNT));
#elif 0
	nrf_timer_subscribe_set(APP_COUNTER, NRF_TIMER_TASK_COUNT, channel);
#elif 0
	uint8_t dppi_chan_ready;
	dppi_chan_ready = allocate_gppi_channel();

	printk("dppi_chan_ready %d\n", dppi_chan_ready);

	nrf_egu_subscribe_set(NRF_EGU20, NRF_EGU_TASK_TRIGGER0, channel);

	nrfx_gppi_channel_endpoints_setup(dppi_chan_ready,
					  nrf_egu_event_address_get(NRF_EGU20, NRF_EGU_EVENT_TRIGGERED0),
		nrf_timer_task_address_get(APP_COUNTER, NRF_TIMER_TASK_COUNT));

	nrfx_gppi_channels_enable(BIT(dppi_chan_ready));

	IRQ_DIRECT_CONNECT(EGU20_IRQn, 5, egu_handler, 0);
	nrf_egu_int_enable(NRF_EGU20, NRF_EGU_INT_TRIGGERED0);
	NVIC_EnableIRQ(EGU20_IRQn);

#elif 1
	NRF_DPPI_ENDPOINT_SETUP(nrfx_timer_task_address_get(&app_timer_instance, NRF_TIMER_TASK_COUNT), channel);
	nrf_ppib_subscribe_set(NRF_PPIB11, NRF_PPIB_TASK_SEND_0, channel);
	nrf_ppib_publish_set(NRF_PPIB21, NRF_PPIB_EVENT_RECEIVE_0, channel);

#else
	nrfx_gppi_channel_endpoints_setup(channel,
		nrf_radio_event_address_get(NRF_RADIO, NRF_RADIO_EVENT_READY),
#if 0
		nrf_timer_task_address_get(APP_COUNTER, NRF_TIMER_TASK_COUNT));
#else
		nrfx_timer_task_address_get(&app_timer_instance, NRF_TIMER_TASK_COUNT));
#endif
#endif
	nrfx_gppi_channels_enable(BIT(channel));
	nrfy_dppi_channels_set((NRF_DPPIC_Type *) 0x50082000, NRFX_BIT((uint32_t)channel), true);
	nrfy_dppi_channels_set((NRF_DPPIC_Type *) 0x500C2000, NRFX_BIT((uint32_t)channel), true);
	printk("DPPIC channels set: %x\n", *(unsigned int *) 0x50082504);
	printk("Channel %d is %s\n", channel, nrfx_gppi_channel_check(channel) ? "enabled" : "disabled");
//	nrfx_timer_enable(&app_timer_instance);
	PRINT_REGS();
}

static void setup_grant_pin(void)
{
#if 1
	nrf_gpio_cfg_output(APP_GRANT_GPIO_PIN);
	if (APP_GRANT_ACTIVE_LOW) {
		nrf_gpio_pin_clear(APP_GRANT_GPIO_PIN);
		printk("Grant line is set to: %s\n", APP_GRANT_ACTIVE_LOW ? "low" : "high");
		granted = false;
	} else {
		nrf_gpio_pin_set(APP_GRANT_GPIO_PIN);
		printk("Grant line is set to: %s\n", APP_GRANT_ACTIVE_LOW ? "low" : "high");
		granted = true;
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

#define ADV_NCONN BT_LE_ADV_PARAM(0, BT_GAP_ADV_FAST_INT_MIN_2, \
					BT_GAP_ADV_FAST_INT_MAX_2, NULL)

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

	PRINT_REGS();
	if (bt_le_adv_start(ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0)) {
		printk("Advertising failed to start");
		return 0;
	}
	PRINT_REGS();

	printk("Advertising started\n");

	printk("Grant line is set to: %s\n", APP_GRANT_ACTIVE_LOW ? "low" : "high");
	printk("Grant pin is set to: %d\n", APP_GRANT_GPIO_PIN);
	print_welcome_message();

	PRINT_REGS();

	while (1) {
		k_sleep(K_MSEC(100));
		check_input();
	}
}

K_THREAD_DEFINE(console_print_thread_id, CONFIG_MAIN_STACK_SIZE, console_print_thread, NULL, NULL,
		NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
