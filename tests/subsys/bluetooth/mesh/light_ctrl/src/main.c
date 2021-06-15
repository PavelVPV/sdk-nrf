/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <ztest.h>
#include <bluetooth/enocean.h>
#include <bluetooth/mesh.h>
#include <bluetooth/mesh/models.h>
#include <bluetooth/mesh/light_ctrl_srv.h>

/** Mocks ******************************************/

static struct bt_mesh_light_ctrl_srv srv;

static struct bt_mesh_model mock_model = {
	.user_data = &srv
};

void bt_mesh_model_msg_init(struct net_buf_simple *msg, uint32_t opcode)
{
	ztest_check_expected_value(opcode);
	net_buf_simple_init(msg, 0);
	/* Opcodes sent by enocean model are always 3 octets */
	net_buf_simple_add_u8(msg, ((opcode >> 16) & 0xff));
	net_buf_simple_add_le16(msg, opcode & 0xffff);
}

int bt_mesh_model_data_store(struct bt_mesh_model *model, bool vnd,
			     const char *name, const void *data,
			     size_t data_len)
{
	zassert_equal(model, &mock_model, "Incorrect model");
	zassert_true(vnd, "vnd is not true");
	zassert_is_null(name, "unexpected name supplied");
	ztest_check_expected_value(data_len);
	ztest_check_expected_data(data, data_len);
	return 0;
}

int model_send(struct bt_mesh_model *model, struct bt_mesh_msg_ctx *ctx,
	       struct net_buf_simple *buf)
{
	ztest_check_expected_value(model);
	ztest_check_expected_value(ctx);
	ztest_check_expected_value(buf->len);
	ztest_check_expected_data(buf->data, buf->len);
	return 0;
}

void bt_mesh_scene_invalidate(struct bt_mesh_model *mod)
{
	// FIXME
}


void k_work_init_delayable(struct k_work_delayable *dwork,
				  k_work_handler_t handler)
{
	// FIXME
}

int k_work_cancel_delayable(struct k_work_delayable *dwork)
{
	// FIXME
	return 0;
}

/*
 * This is mocked, as k_work_reschedule is inline and can't be, but calls this
 * underneath
 */
int k_work_reschedule_for_queue(struct k_work_q *queue,
				struct k_work_delayable *dwork,
				k_timeout_t delay)
{
	// FIXME
	return 0;
}

static ssize_t mock_read_cb(void *cb_arg, void *data, size_t len)
{
	// FIXME
	return 0;
}

/** End of mocks ***********************************/

static void test_scene_storage(void)
{
}

static void setup(void)
{
	zassert_not_null(_bt_mesh_light_ctrl_srv_cb.init, "Init cb is null");
	zassert_not_null(_bt_mesh_light_ctrl_srv_cb.start, "Start cb is null");
	_bt_mesh_light_ctrl_srv_cb.init(&mock_model);
	_bt_mesh_light_ctrl_srv_cb.start(&mock_model);
}

static void teardown(void)
{
	zassert_not_null(_bt_mesh_light_ctrl_srv_cb.reset, "Reset cb is null");
	_bt_mesh_light_ctrl_srv_cb.reset(&mock_model);
}

void test_main(void)
{
	ztest_test_suite(
		light_ctrl_test,
		ztest_unit_test_setup_teardown(test_scene_storage, setup, teardown),
			 );

	ztest_run_test_suite(light_ctrl_test);
}
