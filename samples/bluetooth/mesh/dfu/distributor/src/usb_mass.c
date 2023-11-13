/*
 * Copyright (c) 2016 Intel Corporation.
 * Copyright (c) 2019-2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/fs/fs.h>
#include <stdio.h>

#include <zephyr/bluetooth/mesh.h>

LOG_MODULE_REGISTER(main);

#include <zephyr/storage/flash_map.h>
#include <ff.h>

#define STORAGE_PARTITION		external_storage
#define STORAGE_PARTITION_ID		FIXED_PARTITION_ID(STORAGE_PARTITION)

static struct fs_mount_t fs_mnt;

static int setup_flash(struct fs_mount_t *mnt)
{
	int rc = 0;
	unsigned int id;
	const struct flash_area *pfa;

	mnt->storage_dev = (void *)STORAGE_PARTITION_ID;
	id = STORAGE_PARTITION_ID;

	rc = flash_area_open(id, &pfa);
	printk("Area %u at 0x%x on %s for %u bytes\n",
	       id, (unsigned int)pfa->fa_off, pfa->fa_dev->name,
	       (unsigned int)pfa->fa_size);

	if (rc < 0 /*&& IS_ENABLED(CONFIG_APP_WIPE_STORAGE) */) {
		printk("Erasing flash area ... ");
		rc = flash_area_erase(pfa, 0, pfa->fa_size);
		printk("%d\n", rc);
	}

	if (rc < 0) {
		flash_area_close(pfa);
	}

	return rc;
}

static int mount_app_fs(struct fs_mount_t *mnt)
{
	int rc;

	static FATFS fat_fs;

	mnt->type = FS_FATFS;
	mnt->fs_data = &fat_fs;
	mnt->mnt_point = "/NAND:";

	rc = fs_mount(mnt);

	return rc;
}

static void setup_disk(void)
{
	struct fs_mount_t *mp = &fs_mnt;
	struct fs_dir_t dir;
	struct fs_statvfs sbuf;
	int rc;

	fs_dir_t_init(&dir);

	rc = setup_flash(mp);
	if (rc < 0) {
		LOG_ERR("Failed to setup flash area");
		return;
	}

	rc = mount_app_fs(mp);
	if (rc < 0) {
		LOG_ERR("Failed to mount filesystem");
		return;
	}

	/* Allow log messages to flush to avoid interleaved output */
	k_sleep(K_MSEC(50));

	printk("Mount %s: %d\n", fs_mnt.mnt_point, rc);

	rc = fs_statvfs(mp->mnt_point, &sbuf);
	if (rc < 0) {
		printk("FAIL: statvfs: %d\n", rc);
		return;
	}

	printk("%s: bsize = %lu ; frsize = %lu ;"
	       " blocks = %lu ; bfree = %lu\n",
	       mp->mnt_point,
	       sbuf.f_bsize, sbuf.f_frsize,
	       sbuf.f_blocks, sbuf.f_bfree);

	rc = fs_opendir(&dir, mp->mnt_point);
	printk("%s opendir: %d\n", mp->mnt_point, rc);

	if (rc < 0) {
		LOG_ERR("Failed to open directory");
	}

	while (rc >= 0) {
		struct fs_dirent ent = { 0 };

		rc = fs_readdir(&dir, &ent);
		if (rc < 0) {
			LOG_ERR("Failed to read directory entries");
			break;
		}
		if (ent.name[0] == 0) {
			printk("End of files\n");
			break;
		}
		printk("  %c %u %s\n",
		       (ent.type == FS_DIR_ENTRY_FILE) ? 'F' : 'D',
		       ent.size,
		       ent.name);
	}

	(void)fs_closedir(&dir);

	return;
}

static struct fs_file_t image_file;

static int blob_io_open(const struct bt_mesh_blob_io *io,
		    const struct bt_mesh_blob_xfer *xfer,
		    enum bt_mesh_blob_io_mode mode)
{
	char fname[100];
	int err;

	snprintf(fname, sizeof(fname), "%s/APP_UP~1.BIN", fs_mnt.mnt_point);

	err = fs_open(&image_file, fname, FS_O_READ);
	if (err) {
		printk("Unable to open file: %d\n", err);
		return -1;
	}

	return 0;
}

static void blob_io_close(const struct bt_mesh_blob_io *io,
		      const struct bt_mesh_blob_xfer *xfer)
{
	int err;

	err = fs_close(&image_file);
	if (err) {
		printk("Unable to close file: %d\n", err);
	}
}

static int blob_io_block_start(const struct bt_mesh_blob_io *io,
			   const struct bt_mesh_blob_xfer *xfer,
			   const struct bt_mesh_blob_block *block)
{
	return 0;
}

static void blob_io_block_end(const struct bt_mesh_blob_io *io,
			  const struct bt_mesh_blob_xfer *xfer,
			  const struct bt_mesh_blob_block *block)
{
}

static int blob_io_wr(const struct bt_mesh_blob_io *io,
		  const struct bt_mesh_blob_xfer *xfer,
		  const struct bt_mesh_blob_block *block,
		  const struct bt_mesh_blob_chunk *chunk)
{
	return 0;
}

static int blob_io_rd(const struct bt_mesh_blob_io *io,
		  const struct bt_mesh_blob_xfer *xfer,
		  const struct bt_mesh_blob_block *block,
		  const struct bt_mesh_blob_chunk *chunk)
{
	int err;

	err = fs_seek(&image_file, block->offset + chunk->offset, FS_SEEK_SET);
	if (err) {
		printk("Failed to set new file position: %d\n", err);
		return -1;
	}

	err = fs_read(&image_file, chunk->data, chunk->size);
	if (err < 0) {
		printk("Failed to read from file: %d\n", err);
	}

	return 0;
}

static struct bt_mesh_blob_io usb_mass_blob_io = {
	.open = blob_io_open,
	.close = blob_io_close,
	.block_start = blob_io_block_start,
	.block_end = blob_io_block_end,
	.wr = blob_io_wr,
	.rd = blob_io_rd,
};

int usb_flash_stream_init(struct bt_mesh_blob_io **blob_io)
{
	int ret;

	setup_disk();

	ret = usb_enable(NULL);
	if (ret != 0) {
		LOG_ERR("Failed to enable USB");
		return 0;
	}

	LOG_INF("The device is put in USB mass storage mode.\n");

	*blob_io = &usb_mass_blob_io;
	fs_file_t_init(&image_file);
	return 0;
}
