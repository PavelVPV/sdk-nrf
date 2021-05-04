/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "settings/settings.h"

#include <errno.h>
#include <sys/printk.h>

#include <soc.h>
#include <hal/nrf_gpio.h>

int test_handle_set(const char *name, size_t len, settings_read_cb read_cb,
		  void *cb_arg);
int test_handle_commit(void);

/* static subtree handler */
SETTINGS_STATIC_HANDLER_DEFINE(test, "", NULL,
			       test_handle_set, test_handle_commit,
			       NULL);

int test_handle_set(const char *name, size_t len, settings_read_cb read_cb,
		  void *cb_arg)
{
	return 0;
}

int test_handle_commit(void)
{
	printk("loading all settings under <beta> handler is done\n");
	return 0;
}

static void pin_cfg(uint32_t pin)
{
	static uint32_t pins_configured_mask;
	if (pins_configured_mask & (1 << pin)) {
		return;
	}

	pins_configured_mask |= (1 << pin);

	NRF_P0->PIN_CNF[pin] = 0;
	NRF_P0->PIN_CNF[pin] |= (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) & GPIO_PIN_CNF_DIR_Msk;
	NRF_P0->PIN_CNF[pin] |= (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) & GPIO_PIN_CNF_PULL_Msk;
	NRF_P0->PIN_CNF[pin] |= (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) & GPIO_PIN_CNF_DRIVE_Msk;
	NRF_P0->PIN_CNF[pin] |= (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos) & GPIO_PIN_CNF_SENSE_Msk;
	NRF_P0->OUTCLR = 1 << pin;
}

static inline void pin_set(uint32_t pin)
{
	NRF_P0->OUTSET = 1 << pin;
}

static inline void pin_clr(uint32_t pin)
{
	NRF_P0->OUTCLR = 1 << pin;
}

void main(void)
{
	int rc;

	printk("\n*** Settings usage example ***\n\n");

	/* settings initialization */
	rc = settings_subsys_init();
	if (rc) {
		printk("settings subsys initialization: fail (err %d)\n", rc);
		return;
	}

	printk("settings subsys initialization: OK.\n");

	char path[4];
	char buf[SETTINGS_MAX_VAL_LEN] = { [0 ... (SETTINGS_MAX_VAL_LEN - 1)] = 0xA0 };

	pin_cfg(3);
	pin_set(3);

	for (int i = 0; i < 4096; i += SETTINGS_MAX_VAL_LEN) {
		snprintk(path, sizeof(path), "%x", i / SETTINGS_MAX_VAL_LEN);

		rc = settings_save_one(path, buf, SETTINGS_MAX_VAL_LEN);
		if (rc) {
			printk("settings_save_one failed: %d\n", rc);
		}
	}

	pin_clr(3);

	printk("\n*** THE END  ***\n");
}
