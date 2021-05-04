#include <zephyr.h>
#include <power/reboot.h>
#include <device.h>
#include <string.h>
#include <drivers/flash.h>
#include <storage/flash_map.h>
#include <fs/nvs.h>

#include <soc.h>
#include <hal/nrf_gpio.h>

static struct nvs_fs fs;

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

static uint32_t buf[1024] = { [0 ... 1023] = 0xA5 };

void main(void)
{
	int rc;
	const struct flash_area *fa;
	struct flash_sector hw_flash_sector;
	uint32_t sector_cnt = 1;

	printk("Start\n");

	pin_cfg(3);

	pin_set(3);
	rc = flash_area_open(FLASH_AREA_ID(storage), &fa);
	if (rc) {
		printk("Failed to open flash erase: %d\n", rc);
		while (1);
	}

	rc = flash_area_get_sectors(FLASH_AREA_ID(storage), &sector_cnt,
				    &hw_flash_sector);
	if (rc && rc != -ENOMEM) {
		printk("Failed to get sectors: %d\n", rc);
		while (1);
	}

	fs.sector_size = 2 * hw_flash_sector.fs_size;
	fs.sector_count = 2;
	fs.offset = fa->fa_off;

	rc = nvs_init(&fs, fa->fa_dev_name);
	if (rc) {
		printk("Flash Init failed\n");
		while (1);
	}
	pin_clr(3);


#if 1
	pin_set(3);
	rc = nvs_write(&fs, 1, &buf, sizeof(buf));
	if (rc <= 0) {
		printk("Failed to write: %d\n", rc);
		while (1);
	}
	pin_clr(3);

	pin_set(3);
	rc = nvs_clear(&fs);
	if (rc) {
		printk("Failed to clear: %d\n", rc);
		while (1);
	}
	pin_clr(3);
#endif
	printk("Done\n");

	while(1);
}
