#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#define LOG_LEVEL LOG_LEVEL_DBG
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tc_to_psa_converter);

static int keys_load_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
			void *param)
{
	uint8_t buf[256];

	LOG_WRN("Loading setting key %s:%d", key, len);

	if (len > sizeof(buf)) {
		LOG_ERR("Key %s too long (%d)", key, len);
		return 0;
	}

	if (read_cb(cb_arg, buf, len) < 0) {
		LOG_ERR("Failed to load key %s", key);
		return 0;
	}

	LOG_HEXDUMP_DBG(buf, len, "val");

	return 0;
}

static int import_keys(void)
{
	int err;

	err = settings_subsys_init();
	if (err) {
		LOG_ERR("settings_subsys_init failed (err %d)", err);
		return err;
	}

	LOG_INF("Importing keys...");
	settings_load_subtree_direct("bt/mesh", keys_load_cb, NULL);
	LOG_INF("Importing keys done");

	return 0;
}

SYS_INIT(import_keys, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
