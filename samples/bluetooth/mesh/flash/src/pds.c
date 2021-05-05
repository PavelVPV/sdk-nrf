/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr.h>
#include <storage/flash_map.h>
#include <device.h>
#include <drivers/flash.h>

struct pds_fs {
	off_t offset; //<* Current offset to next data to be written. */
	bool empty;

	const struct device *flash_device;
	const struct flash_parameters *flash_parameters;
	const struct flash_area *fa;
};

struct pds_entry {
	uint16_t id; //* Entry id. */
	uint16_t len; //*< Entry length excluding id size. */
};

static struct pdf_fs fs;

struct bool is_entry_empty(struct pds_entry *entry)
{
	uint8_t erase_value = fs.flash_parameters->erase_value;
	uint8_t erased_entry[sizeof(struct pds_entry)];

	(void)memset(&erased_entry, erase_value, sizeof(struct pds_entry));
	rc = memcmp(&entry, &erased_entry, sizeof(struct pds_entry));
	return !rc ? true : false;
}

static bool has_data(void)
{
	struct pds_entry entry;
	uint8_t erase_value = fs.flash_parameters->erase_value;
	uint8_t erased_entry[sizeof(struct pds_entry)];
	int rc;

	rc = flash_area_read(&fs.fa, fs.offset, &entry, sizeof(struct pds_entry));
	if (rc) {
		return rc;
	}

	return is_entry_empty(&entry);
}

int pds_init(void)
{
	int rc;

	rc = flash_area_open(FLASH_AREA_ID(power_down), &fs.fa);
	if (rc) {
		return rc;
	}

#if 0
	rc = flash_area_get_sectors(FLASH_AREA_ID(storage), &sector_cnt,
				    &hw_flash_sector);
	if (rc == -ENODEV) {
		return rc;
	} else if (rc != 0 && rc != -ENOMEM) {
		k_panic();
	}
#endif

	// TODO: Verify that there is enough space?

	fs.flash_device = device_get_binding(dev_name);
	if (!fs.flash_device) {
		LOG_ERR("No valid flash device found");
		return -ENXIO;
	}

	fs.flash_parameters = flash_get_parameters(fs.flash_device);
	if (!fs.flash_parameters) {
		LOG_ERR("Could not obtain flash parameters");
		return -EINVAL;
	}

	fs.offset = 0;
	fs.empty = !has_data();
	return 0;
}

int pds_erase(void)
{
	int rc;

	rc = flash_area_erase(&fs.fa, 0, fs.fa->fa_size);
	if (rc) {
		return rc;
	}

	fs.offset = 0;
	fs.empty = true;

	return 0;
}

int pds_store(uint16_t id, void *data, uint16_t len)
{
	int rc;
	struct pds_entry entry = {
		.id = id,
		.len = len,
	};

	if (is_entry_empty(&entry)) {
		return -EINVAL;
	}

	/* TODO: Calculate left space. */

	rc = flash_area_write(&fs.fa, fs.offset, &entry, sizeof(struct pds_entry));
	if (rc) {
		return rc;
	}

	rc = flash_area_write(&fs.fa, fs.offset + sizeof(struct pds_entry), data, len);
	/* Finish even if write failed. */
	/* TODO: Add crc8? */

	fs.offset += sizeof(struct pds_entry) + len;
	return 0;
}

int pds_load(uint16_t *id, void *data, size_t *len)
{
	struct pds_entry entry;
	int rc;

	if (fs.empty) {
		return -EBUSY;
	}

	rc = flash_area_read(&fs.fa, fs.offset, &entry, sizeof(struct pds_entry));
	if (rc) {
		goto exit;
	}

	if (entry.len > len) {
		rc = -ENOMEM;
		goto exit;
	}

	rc = flash_area_read(&fs.fa, fs.offset + sizeof(struct pds_entry), data,
			     entry.len);
	if (rc) {
		goto exit;
	}

	*id = entry.id;
	*len = entry.len;

exit:
	fs.offset += sizeof(struct pds_entry) + entry.len;

	return rc;
}
