#include <zephyr.h>
#include <sys/printk.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/mesh.h>
#include <bluetooth/mesh/models.h>
#include <bluetooth/mesh/dk_prov.h>
#include <dk_buttons_and_leds.h>

#include <shell/shell.h>
#include <shell/shell_uart.h>

#define LOG_MODEL(mod, fmt, ...) printk("[%04x:%04x]: " fmt,\
					bt_mesh_model_elem(mod)->addr,\
					mod->id, __VA_ARGS__)

static const struct shell *app_shell;

static struct bt_mesh_scene_srv scene_srv;

static void scene_status(struct bt_mesh_scene_cli *cli,
		       struct bt_mesh_msg_ctx *ctx,
		       const struct bt_mesh_scene_state *state)
{
	LOG_MODEL(cli->model, "scene_status: %d %d %d %d\n", state->status, state->current,
		  state->target, state->remaining_time);
}

static void scene_scene_register(struct bt_mesh_scene_cli *cli,
			       struct bt_mesh_msg_ctx *ctx,
			       const struct bt_mesh_scene_register *reg)
{
	LOG_MODEL(cli->model, "scene_scene_register: %d %d %d\n", reg->status, reg->current,
		  reg->count);
}

static struct bt_mesh_scene_cli scene_cli = {
	.status = scene_status,
	.scene_register = scene_scene_register,
};

//static struct bt_mesh_time_srv time_srv = BT_MESH_TIME_SRV_INIT(NULL);
//static struct bt_mesh_time_cli time_cli = BT_MESH_TIME_CLI_INIT(NULL);

//static struct bt_mesh_dtt_srv dtt_srv = BT_MESH_DTT_SRV_INIT(NULL);
//static struct bt_mesh_dtt_cli dtt_cli = BT_MESH_DTT_CLI_INIT(NULL);

//static struct bt_mesh_battery_srv battery_srv = BT_MESH_BATTERY_SRV_INIT(NULL);
//static struct bt_mesh_battery_cli battery_cli = BT_MESH_BATTERY_CLI_INIT(NULL);

//static struct bt_mesh_loc_srv loc_srv = BT_MESH_LOC_SRV_INIT(NULL);
//static struct bt_mesh_loc_cli loc_cli = BT_MESH_LOC_CLI_INIT(NULL);

//static struct bt_mesh_prop_srv prop_srv = BT_MESH_PROP_SRV_USER_INIT();
//static struct bt_mesh_prop_cli prop_cli = BT_MESH_PROP_CLI_INIT(NULL, NULL);

static int32_t remaining_time_get(const struct bt_mesh_model_transition *transition)
{
	if (transition == NULL) {
		printk("transition is null\n");
		return 0;
	}

	return transition->delay + transition->time;
}

static bool on_off;

static void onoff_set(struct bt_mesh_onoff_srv *srv,
			struct bt_mesh_msg_ctx *ctx,
			const struct bt_mesh_onoff_set *set,
			struct bt_mesh_onoff_status *rsp)
{
	LOG_MODEL(srv->model, "onoff_set: %d\n", set->on_off);

	rsp->present_on_off = on_off;
	rsp->target_on_off = set->on_off;
	rsp->remaining_time = remaining_time_get(set->transition);
	on_off = set->on_off;
}

static void onoff_get(struct bt_mesh_onoff_srv *srv,
			struct bt_mesh_msg_ctx *ctx,
			struct bt_mesh_onoff_status *rsp)
{
	LOG_MODEL(srv->model, "onoff_get: %d\n", on_off);

	rsp->present_on_off = on_off;
	rsp->target_on_off = on_off;
	rsp->remaining_time = 0;
}

static struct bt_mesh_onoff_srv_handlers onoff_srv_handlers = {
	.set = onoff_set,
	.get = onoff_get,
};

static void onoff_status_handler(struct bt_mesh_onoff_cli *cli,
				 struct bt_mesh_msg_ctx *ctx,
				 const struct bt_mesh_onoff_status *status)
{
	LOG_MODEL(cli->model, "onoff_status_handler: %d, %d, %d\n", status->present_on_off,
	       status->target_on_off, status->remaining_time);
}

static struct bt_mesh_onoff_srv onoff_srv = BT_MESH_ONOFF_SRV_INIT(&onoff_srv_handlers);
static struct bt_mesh_onoff_cli onoff_cli = BT_MESH_ONOFF_CLI_INIT(&onoff_status_handler);

static int16_t lvl;

static void lvl_get(struct bt_mesh_lvl_srv *srv,
		struct bt_mesh_msg_ctx *ctx,
		struct bt_mesh_lvl_status *rsp)
{
	LOG_MODEL(srv->model, "lvl_get: %d\n", lvl);

	rsp->current = lvl;
	rsp->target = lvl;
	rsp->remaining_time = 0;
}

static void lvl_set(struct bt_mesh_lvl_srv *srv,
		struct bt_mesh_msg_ctx *ctx,
		const struct bt_mesh_lvl_set *set,
		struct bt_mesh_lvl_status *rsp)
{
	LOG_MODEL(srv->model, "lvl_set: %d %d\n", set->lvl, set->new_transaction);

	rsp->current = lvl;
	rsp->target = set->lvl;
	rsp->remaining_time = remaining_time_get(set->transition);

	lvl = set->lvl;
}

static void lvl_delta_set(struct bt_mesh_lvl_srv *srv,
			struct bt_mesh_msg_ctx *ctx,
			const struct bt_mesh_lvl_delta_set *delta_set,
			struct bt_mesh_lvl_status *rsp)
{
	LOG_MODEL(srv->model, "lvl_delta_set: %d %d\n", delta_set->delta,
		  delta_set->new_transaction);

	rsp->current = lvl;
	rsp->target = lvl;
	rsp->remaining_time = remaining_time_get(delta_set->transition);
}

static void lvl_move_set(struct bt_mesh_lvl_srv *srv,
			 struct bt_mesh_msg_ctx *ctx,
			 const struct bt_mesh_lvl_move_set *move_set,
			 struct bt_mesh_lvl_status *rsp)
{
	LOG_MODEL(srv->model, "lvl_move_set: %d %d\n", move_set->delta, move_set->new_transaction);

	rsp->current = lvl;
	rsp->target = lvl;
	rsp->remaining_time = remaining_time_get(move_set->transition);
}

static struct bt_mesh_lvl_srv_handlers lvl_srv_handlers = {
	.get = lvl_get,
	.set = lvl_set,
	.delta_set = lvl_delta_set,
	.move_set = lvl_move_set,
};

static void lvl_status_handler(struct bt_mesh_lvl_cli *cli,
				struct bt_mesh_msg_ctx *ctx,
				const struct bt_mesh_lvl_status *status)
{
	LOG_MODEL(cli->model, "lvl_status_handler: %d %d %d\n", status->current, status->target,
	       status->remaining_time);
}

static struct bt_mesh_lvl_srv lvl_srv = BT_MESH_LVL_SRV_INIT(&lvl_srv_handlers);
static struct bt_mesh_lvl_cli lvl_cli = BT_MESH_LVL_CLI_INIT(&lvl_status_handler);

static void ponoff_update(struct bt_mesh_ponoff_srv *srv,
			  struct bt_mesh_msg_ctx *ctx,
			  enum bt_mesh_on_power_up old_on_power_up,
			  enum bt_mesh_on_power_up new_on_power_up)
{
	LOG_MODEL(srv->ponoff_model, "ponoff_update: %d %d\n", old_on_power_up, new_on_power_up);
}

static void ponoff_status_handler(struct bt_mesh_ponoff_cli *cli,
				  struct bt_mesh_msg_ctx *ctx,
				  enum bt_mesh_on_power_up on_power_up)
{
	LOG_MODEL(cli->model, "ponoff_status_handler: %d\n", on_power_up);
}

static struct bt_mesh_ponoff_srv ponoff_srv =
	BT_MESH_PONOFF_SRV_INIT(&onoff_srv_handlers, NULL, &ponoff_update);
static struct bt_mesh_ponoff_cli ponoff_cli = BT_MESH_PONOFF_CLI_INIT(&ponoff_status_handler);

static uint16_t power_lvl;

static void plvl_power_set(struct bt_mesh_plvl_srv *srv,
			struct bt_mesh_msg_ctx *ctx,
			const struct bt_mesh_plvl_set *set,
			struct bt_mesh_plvl_status *rsp)
{
	LOG_MODEL(srv->plvl_model, "plvl_power_set: %d\n", set->power_lvl);

	rsp->current = power_lvl;
	rsp->target = set->power_lvl;
	rsp->remaining_time = remaining_time_get(set->transition);

	power_lvl = set->power_lvl;
}

static void plvl_power_get(struct bt_mesh_plvl_srv *srv,
			struct bt_mesh_msg_ctx *ctx,
			struct bt_mesh_plvl_status *rsp)
{
	LOG_MODEL(srv->plvl_model, "plvl_power_get: %d\n", power_lvl);

	rsp->current = power_lvl;
	rsp->target = power_lvl;
	rsp->remaining_time = 0;
}

static void plvl_default_update(struct bt_mesh_plvl_srv *srv,
			     struct bt_mesh_msg_ctx *ctx,
			     uint16_t old_default, uint16_t new_default)
{
	LOG_MODEL(srv->plvl_model, "plvl_default_update: %d %d\n", old_default, new_default);
}

static void plvl_range_update(struct bt_mesh_plvl_srv *srv,
			   struct bt_mesh_msg_ctx *ctx,
			   const struct bt_mesh_plvl_range *old_range,
			   const struct bt_mesh_plvl_range *new_range)
{
	LOG_MODEL(srv->plvl_model, "plvl_range_update: %d.%d %d.%d\n", old_range->min,
		  old_range->max, new_range->min, new_range->max);
}

static struct bt_mesh_plvl_srv_handlers plvl_srv_handlers = {
	.power_set = plvl_power_set,
	.power_get = plvl_power_get,
	.default_update = plvl_default_update,
	.range_update = plvl_range_update,
};

static void plvl_power_status(struct bt_mesh_plvl_cli *cli,
				   struct bt_mesh_msg_ctx *ctx,
				   const struct bt_mesh_plvl_status *status)
{
	LOG_MODEL(cli->model, "plvl_power_status: %d %d %d\n", status->current, status->target,
	       status->remaining_time);
}

static void plvl_default_status(struct bt_mesh_plvl_cli *cli,
				struct bt_mesh_msg_ctx *ctx,
				uint16_t default_power)
{
	LOG_MODEL(cli->model, "plvl_default_status: %d\n", default_power);
}

static void plvl_range_status(struct bt_mesh_plvl_cli *cli,
			      struct bt_mesh_msg_ctx *ctx,
			      const struct bt_mesh_plvl_range_status *status)
{
	LOG_MODEL(cli->model, "plvl_range_status: %d %d.%d\n", status->status, status->range.min,
		  status->range.max);
}

static void plvl_last_status(struct bt_mesh_plvl_cli *cli,
			     struct bt_mesh_msg_ctx *ctx, uint16_t last)
{
	LOG_MODEL(cli->model, "plvl_last_status: %d\n", last);
}

static struct bt_mesh_plvl_cli_handlers plvl_cli_handlers = {
	.power_status = plvl_power_status,
	.default_status = plvl_default_status,
	.range_status = plvl_range_status,
	.last_status = plvl_last_status,
};

static struct bt_mesh_plvl_srv plvl_srv = BT_MESH_PLVL_SRV_INIT(&plvl_srv_handlers);
static struct bt_mesh_plvl_cli plvl_cli = BT_MESH_PLVL_CLI_INIT(&plvl_cli_handlers);

static uint16_t light;

static void lightness_light_set(struct bt_mesh_lightness_srv *srv,
			struct bt_mesh_msg_ctx *ctx,
			const struct bt_mesh_lightness_set *set,
			struct bt_mesh_lightness_status *rsp)
{
	LOG_MODEL(srv->lightness_model, "lightness_light_set: %d\n", set->lvl);

	rsp->current = light;
	rsp->target = set->lvl;
	rsp->remaining_time = remaining_time_get(set->transition);

	light = set->lvl;
}

static void lightness_light_get(struct bt_mesh_lightness_srv *srv,
			struct bt_mesh_msg_ctx *ctx,
			struct bt_mesh_lightness_status *rsp)
{
	LOG_MODEL(srv->lightness_model, "lightness_light_get: %d\n", light);

	rsp->current = light;
	rsp->target = light;
	rsp->remaining_time = 0;
}

static void lightness_default_update(struct bt_mesh_lightness_srv *srv,
			     struct bt_mesh_msg_ctx *ctx,
			     uint16_t old_default, uint16_t new_default)
{
	LOG_MODEL(srv->lightness_model, "lightness_default_update: %d %d\n", old_default,
		  new_default);
}

static void lightness_range_update(
	struct bt_mesh_lightness_srv *srv, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_lightness_range *old_range,
	const struct bt_mesh_lightness_range *new_range)
{
	LOG_MODEL(srv->lightness_model, "lightness_range_update: %d.%d %d.%d\n", old_range->min,
		  old_range->max, new_range->min, new_range->max);
}

struct bt_mesh_lightness_srv_handlers lightness_srv_handlers = {
	.light_set = lightness_light_set,
	.light_get = lightness_light_get,
	.default_update = lightness_default_update,
	.range_update = lightness_range_update,
};

static void lightness_light_status(
		struct bt_mesh_lightness_cli *cli, struct bt_mesh_msg_ctx *ctx,
		const struct bt_mesh_lightness_status *status)
{
	LOG_MODEL(cli->model, "lightness_light_status: %d %d %d\n", status->current, status->target,
		  status->remaining_time);
}

static void lightness_default_status(struct bt_mesh_lightness_cli *cli,
				     struct bt_mesh_msg_ctx *ctx,
				     uint16_t default_light)
{
	LOG_MODEL(cli->model, "lightness_default_status: %d\n", default_light);
}

static void lightness_range_status(
		struct bt_mesh_lightness_cli *cli, struct bt_mesh_msg_ctx *ctx,
		const struct bt_mesh_lightness_range_status *status)
{
	LOG_MODEL(cli->model, "lightness_range_status: %d %d.%d\n", status->status,
		  status->range.min, status->range.max);
}

static void lightness_last_light_status(struct bt_mesh_lightness_cli *cli,
					struct bt_mesh_msg_ctx *ctx,
					uint16_t last)
{
	LOG_MODEL(cli->model, "lightness_last_light_status: %d\n", last);
}

static struct bt_mesh_lightness_cli_handlers lightness_cli_handlers = {
	.light_status = lightness_light_status,
	.default_status = lightness_default_status,
	.range_status = lightness_range_status,
	.last_light_status = lightness_last_light_status,
};

static struct bt_mesh_lightness_srv lightness_srv_standalone =
	BT_MESH_LIGHTNESS_SRV_INIT(&lightness_srv_handlers);
static struct bt_mesh_lightness_cli lightness_cli =
	BT_MESH_LIGHTNESS_CLI_INIT(&lightness_cli_handlers);

static void lc_mode(struct bt_mesh_light_ctrl_cli *cli,
		     struct bt_mesh_msg_ctx *ctx, bool enabled)
{
	LOG_MODEL(cli->model, "lc_mode: %d\n", enabled);
}

static void lc_occupancy_mode(struct bt_mesh_light_ctrl_cli *cli,
			       struct bt_mesh_msg_ctx *ctx, bool enabled)
{
	LOG_MODEL(cli->model, "lc_occupancy_mode: %d\n", enabled);
}

static void lc_light_onoff(struct bt_mesh_light_ctrl_cli *cli,
			    struct bt_mesh_msg_ctx *ctx,
			    const struct bt_mesh_onoff_status *status)
{
	LOG_MODEL(cli->model, "lc_light_onoff: %d %d %d\n", status->present_on_off,
		  status->target_on_off, status->remaining_time);
}

static void lc_prop(struct bt_mesh_light_ctrl_cli *cli,
		     struct bt_mesh_msg_ctx *ctx,
		     enum bt_mesh_light_ctrl_prop id,
		     const struct sensor_value *value)
{
	LOG_MODEL(cli->model, "lc_prop: %d %d.%d\n", id, value->val1, value->val2);
}

static void lc_coeff(struct bt_mesh_light_ctrl_cli *cli,
		     struct bt_mesh_msg_ctx *ctx,
		     enum bt_mesh_light_ctrl_coeff id,
		     float value)
{
	LOG_MODEL(cli->model, "lc_coeff: %d %f\n", id, value);
}

static struct bt_mesh_light_ctrl_cli_handlers lc_cli_handlers = {
	.mode = lc_mode,
	.occupancy_mode = lc_occupancy_mode,
	.light_onoff = lc_light_onoff,
	.prop = lc_prop,
	.coeff = lc_coeff,
};

static struct bt_mesh_lightness_srv lightness_srv_lc =
	BT_MESH_LIGHTNESS_SRV_INIT(&lightness_srv_handlers);
static struct bt_mesh_light_ctrl_cli light_lc_cli = BT_MESH_LIGHT_CTRL_CLI_INIT(&lc_cli_handlers);

static struct bt_mesh_light_temp temp;

static void temp_set(struct bt_mesh_light_temp_srv *srv,
			  struct bt_mesh_msg_ctx *ctx,
			  const struct bt_mesh_light_temp_set *set,
			  struct bt_mesh_light_temp_status *rsp)
{
	LOG_MODEL(srv->model, "temp_set: %d %d\n", set->params.temp, set->params.delta_uv);

	rsp->current = temp;
	rsp->target = set->params;
	rsp->remaining_time = remaining_time_get(set->transition);

	temp = set->params;
}

static void temp_get(struct bt_mesh_light_temp_srv *srv,
			  struct bt_mesh_msg_ctx *ctx,
			  struct bt_mesh_light_temp_status *rsp)
{
	LOG_MODEL(srv->model, "temp_get: %d %d\n", temp.temp, temp.delta_uv);

	rsp->current = temp;
	rsp->target = temp;
	rsp->remaining_time = 0;
}

static void temp_range_update(
		struct bt_mesh_light_temp_srv *srv, struct bt_mesh_msg_ctx *ctx,
		const struct bt_mesh_light_temp_range *old_range,
		const struct bt_mesh_light_temp_range *new_range)
{
	LOG_MODEL(srv->model, "temp_range_update: %d.%d %d.%d\n", old_range->min, old_range->max,
	       new_range->min, new_range->max);
}

static void temp_default_update(
		struct bt_mesh_light_temp_srv *srv, struct bt_mesh_msg_ctx *ctx,
		const struct bt_mesh_light_temp *old_default,
		const struct bt_mesh_light_temp *new_default)
{
	LOG_MODEL(srv->model, "temp_default_update: %d %d, %d %d\n", old_default->temp,
		  old_default->delta_uv, new_default->temp, new_default->delta_uv);
}

static struct bt_mesh_light_temp_srv_handlers temp_srv_handlers = {
	.set = temp_set,
	.get = temp_get,
	.range_update = temp_range_update,
	.default_update = temp_default_update,
};

static void ctl_ctl_status(struct bt_mesh_light_ctl_cli *cli,
			 struct bt_mesh_msg_ctx *ctx,
			 const struct bt_mesh_light_ctl_status *status)
{
	LOG_MODEL(cli->model, "ctl_ctl_status: %d %d, %d %d, %d\n", status->current_light,
		  status->current_temp, status->target_light, status->target_temp,
		  status->remaining_time);
}

static void ctl_temp_status(
	struct bt_mesh_light_ctl_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_temp_status *status)
{
	LOG_MODEL(cli->model, "ctl_temp_status: %d %d, %d %d, %d\n", status->current.temp,
	       status->current.delta_uv, status->target.temp, status->target.delta_uv,
	       status->remaining_time);
}

static void ctl_default_status(
	struct bt_mesh_light_ctl_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_ctl *status)
{
	LOG_MODEL(cli->model, "ctl_default_status: %d %d %d\n", status->light, status->temp,
		  status->delta_uv);
}

static void ctl_temp_range_status(
	struct bt_mesh_light_ctl_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_temp_range_status *status)
{
	LOG_MODEL(cli->model, "ctl_temp_range_status: %d %d.%d\n", status->status,
		  status->range.min, status->range.max);
}

static struct bt_mesh_light_ctl_cli_handlers ctl_cli_handlers = {
	.ctl_status = ctl_ctl_status,
	.temp_status = ctl_temp_status,
	.default_status = ctl_default_status,
	.temp_range_status = ctl_temp_range_status,
};

static struct bt_mesh_light_ctl_srv light_ctl_srv =
	BT_MESH_LIGHT_CTL_SRV_INIT(&lightness_srv_handlers, &temp_srv_handlers);
static struct bt_mesh_light_ctl_cli light_ctl_cli = BT_MESH_LIGHT_CTL_CLI_INIT(&ctl_cli_handlers);

static struct bt_mesh_light_ctrl_srv light_lc_srv =
	BT_MESH_LIGHT_CTRL_SRV_INIT(&lightness_srv_lc);

static struct bt_mesh_light_temp_srv light_temp_srv =
	BT_MESH_LIGHT_TEMP_SRV_INIT(&temp_srv_handlers);

static struct bt_mesh_light_xy xy;

static void xyl_xy_set(struct bt_mesh_light_xyl_srv *srv,
		     struct bt_mesh_msg_ctx *ctx,
		     const struct bt_mesh_light_xy_set *set,
		     struct bt_mesh_light_xy_status *rsp)
{
	LOG_MODEL(srv->model, "xyl_xy_set: %d %d\n", set->params.x, set->params.y);

	rsp->current = xy;
	rsp->target = set->params;
	rsp->remaining_time = remaining_time_get(set->transition);

	xy = set->params;
}

static void xyl_xy_get(struct bt_mesh_light_xyl_srv *srv,
		     struct bt_mesh_msg_ctx *ctx,
		     struct bt_mesh_light_xy_status *rsp)
{
	LOG_MODEL(srv->model, "xyl_xy_get: %d.%d\n", xy.x, xy.y);

	rsp->current = xy;
	rsp->target = xy;
	rsp->remaining_time = 0;
}

static void xyl_range_update(struct bt_mesh_light_xyl_srv *srv,
			   struct bt_mesh_msg_ctx *ctx,
			   const struct bt_mesh_light_xy_range *old,
			   const struct bt_mesh_light_xy_range *new)
{
	LOG_MODEL(srv->model, "xyl_range_update: (%d.%d).(%d.%d) (%d.%d).(%d.%d)\n", old->min.x,
		  old->min.y, old->max.x, old->max.y, new->min.x, new->min.y, new->max.x,
		  new->max.y);
}

static void xyl_default_update(struct bt_mesh_light_xyl_srv *srv,
			     struct bt_mesh_msg_ctx *ctx,
			     const struct bt_mesh_light_xy *old,
			     const struct bt_mesh_light_xy *new)
{
	LOG_MODEL(srv->model, "xyl_default_update: %d.%d %d.%d\n", old->x, old->y, new->x, new->y);
}

static struct bt_mesh_light_xyl_srv_handlers xyl_srv_handelrs = {
	.xy_set = xyl_xy_set,
	.xy_get = xyl_xy_get,
	.range_update = xyl_range_update,
	.default_update = xyl_default_update,
};

static struct bt_mesh_light_xyl_srv light_xyl_srv =
	BT_MESH_LIGHT_XYL_SRV_INIT(&lightness_srv_handlers, &xyl_srv_handelrs);

static void xyl_status(struct bt_mesh_light_xyl_cli *cli,
		     struct bt_mesh_msg_ctx *ctx,
		     const struct bt_mesh_light_xyl_status *status)
{
	LOG_MODEL(cli->model, "xyl_status: %d %d %d %d\n", status->params.lightness,
		  status->params.xy.x, status->params.xy.y, status->remaining_time);
}

static void xyl_target_status(
	struct bt_mesh_light_xyl_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_xyl_status *status)
{
	LOG_MODEL(cli->model, "xyl_target_status: %d %d %d %d\n", status->params.lightness,
		  status->params.xy.x, status->params.xy.y, status->remaining_time);
}

static void xyl_default_status(
	struct bt_mesh_light_xyl_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_xyl *status)
{
	LOG_MODEL(cli->model, "xyl_default_status: %d %d %d\n", status->lightness,
		  status->xy.x, status->xy.y);
}

static void xyl_range_status(
	struct bt_mesh_light_xyl_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_xyl_range_status *status)
{
	LOG_MODEL(cli->model, "xyl_range_status: %d (%d.%d).(%d.%d)\n", status->status_code,
		  status->range.min.x, status->range.min.y,
		  status->range.max.x, status->range.max.y);
}

struct bt_mesh_light_xyl_cli_handlers xyl_cli_handlers = {
	.xyl_status = xyl_status,
	.target_status = xyl_target_status,
	.default_status = xyl_default_status,
	.range_status = xyl_range_status,
};

static struct bt_mesh_light_xyl_cli light_xyl_cli = {
	.handlers = &xyl_cli_handlers,
};

static uint16_t hue;

static void hue_set(struct bt_mesh_light_hue_srv *srv,
		  struct bt_mesh_msg_ctx *ctx,
		  const struct bt_mesh_light_hue *set,
		  struct bt_mesh_light_hue_status *rsp)
{
	LOG_MODEL(srv->model, "hue_set: %d\n", set->lvl);

	rsp->current = hue;
	rsp->target = set->lvl;
	rsp->remaining_time = remaining_time_get(set->transition);

	hue = set->lvl;
}

static void hue_delta_set(
	struct bt_mesh_light_hue_srv *srv,
	struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_hue_delta *delta_set,
	struct bt_mesh_light_hue_status *rsp)
{
	LOG_MODEL(srv->model, "hue_delta_set: %d %d\n", delta_set->delta,
		  delta_set->new_transaction);

	rsp->current = hue;
	rsp->target = hue;
	rsp->remaining_time = remaining_time_get(delta_set->transition);
}

static void hue_move_set(struct bt_mesh_light_hue_srv *srv,
		       struct bt_mesh_msg_ctx *ctx,
		       const struct bt_mesh_light_hue_move *move,
		       struct bt_mesh_light_hue_status *rsp)
{
	LOG_MODEL(srv->model, "hue_move_set: %d\n", move->delta);

	rsp->current = hue;
	rsp->target = hue;
	rsp->remaining_time = remaining_time_get(move->transition);
}

static void hue_get(struct bt_mesh_light_hue_srv *srv,
		  struct bt_mesh_msg_ctx *ctx,
		  struct bt_mesh_light_hue_status *rsp)
{
	LOG_MODEL(srv->model, "hue_get: %d\n", hue);

	rsp->current = hue;
	rsp->target = hue;
	rsp->remaining_time = 0;
}

static void hue_default_update(struct bt_mesh_light_hue_srv *srv,
			     struct bt_mesh_msg_ctx *ctx,
			     uint16_t old_default,
			     uint16_t new_default)
{
	LOG_MODEL(srv->model, "hue_default_update: %d %d\n", old_default, new_default);
}

static void hue_range_update(
	struct bt_mesh_light_hue_srv *srv,
	struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_hsl_range *old_range,
	const struct bt_mesh_light_hsl_range *new_range)
{
	LOG_MODEL(srv->model, "hue_range_update: %d.%d %d.%d\n", old_range->min, old_range->max,
	       new_range->min, new_range->max);
}

static struct bt_mesh_light_hue_srv_handlers hue_srv_handlers = {
	.set = hue_set,
	.delta_set = hue_delta_set,
	.move_set = hue_move_set,
	.get = hue_get,
	.default_update = hue_default_update,
	.range_update = hue_range_update,
};

static uint16_t sat;

static void sat_set(struct bt_mesh_light_sat_srv *srv,
		  struct bt_mesh_msg_ctx *ctx,
		  const struct bt_mesh_light_sat *set,
		  struct bt_mesh_light_sat_status *rsp)
{
	LOG_MODEL(srv->model, "sat_set: %d\n", set->lvl);

	rsp->current = sat;
	rsp->target = set->lvl;
	rsp->remaining_time = remaining_time_get(set->transition);

	sat = set->lvl;
}

static void sat_get(struct bt_mesh_light_sat_srv *srv,
		  struct bt_mesh_msg_ctx *ctx,
		  struct bt_mesh_light_sat_status *rsp)
{
	LOG_MODEL(srv->model, "sat_get: %d\n", sat);

	rsp->current = sat;
	rsp->target = sat;
	rsp->remaining_time = 0;
}

static void sat_default_update(struct bt_mesh_light_sat_srv *srv,
			     struct bt_mesh_msg_ctx *ctx,
			     uint16_t old_default,
			     uint16_t new_default)
{
	LOG_MODEL(srv->model, "sat_default_update: %d %d\n", old_default, new_default);
}

static void sat_range_update(
	struct bt_mesh_light_sat_srv *srv,
	struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_hsl_range *old_range,
	const struct bt_mesh_light_hsl_range *new_range)
{
	LOG_MODEL(srv->model, "sat_range_update: %d.%d %d.%d\n", old_range->min, old_range->max,
	       new_range->min, new_range->max);
}

static struct bt_mesh_light_sat_srv_handlers sat_srv_handlers = {
	.set = sat_set,
	.get = sat_get,
	.default_update = sat_default_update,
	.range_update = sat_range_update,
};

static struct bt_mesh_light_hsl_srv light_hsl_srv =
	BT_MESH_LIGHT_HSL_SRV_INIT(&hue_srv_handlers, &sat_srv_handlers, &lightness_srv_handlers);

static void hsl_status(struct bt_mesh_light_hsl_cli *cli,
		     struct bt_mesh_msg_ctx *ctx,
		     const struct bt_mesh_light_hsl_status *status)
{
	LOG_MODEL(cli->model, "hsl_status: %d %d %d\n", status->params.lightness,
		  status->params.hue, status->params.saturation);
}

static void hsl_target_status(
	struct bt_mesh_light_hsl_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_hsl_status *status)
{
	LOG_MODEL(cli->model, "hsl_target_status: %d %d %d %d\n", status->params.lightness,
		  status->params.hue, status->params.saturation, status->remaining_time);
}

static void hsl_default_status(
	struct bt_mesh_light_hsl_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_hsl *status)
{
	LOG_MODEL(cli->model, "hsl_default_status: %d %d %d\n", status->lightness,
		  status->hue, status->saturation);
}

static void hsl_range_status(
	struct bt_mesh_light_hsl_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_hsl_range_status *status)
{
	LOG_MODEL(cli->model, "hsl_range_status: %d (%d.%d).(%d.%d)\n", status->status_code,
		  status->range.min.hue, status->range.min.saturation,
		  status->range.max.hue, status->range.max.saturation);
}

static void hsl_hue_status(
	struct bt_mesh_light_hsl_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_hue_status *status)
{
	LOG_MODEL(cli->model, "hsl_hue_status: %d %d %d\n", status->current,
		  status->target, status->remaining_time);
}

static void hsl_saturation_status(
	struct bt_mesh_light_hsl_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_light_sat_status *status)
{
	LOG_MODEL(cli->model, "hsl_saturation_status: %d %d %d\n", status->current,
		  status->target, status->remaining_time);
}

struct bt_mesh_light_hsl_cli_handlers hsl_cli_handlers = {
	.status = hsl_status,
	.target_status = hsl_target_status,
	.default_status = hsl_default_status,
	.range_status = hsl_range_status,
	.hue_status = hsl_hue_status,
	.saturation_status = hsl_saturation_status,
};

static struct bt_mesh_light_hsl_cli light_hsl_cli = {
	.handlers = &hsl_cli_handlers,
};

static struct bt_mesh_light_hue_srv light_hue_srv = BT_MESH_LIGHT_HUE_SRV_INIT(&hue_srv_handlers);

static struct bt_mesh_light_sat_srv light_sat_srv = BT_MESH_LIGHT_SAT_SRV_INIT(&sat_srv_handlers);

/*************************************************************************************************/
/* Sensor */
/*************************************************************************************************/
static int32_t sensor_val = 50;

static int motion_get(struct bt_mesh_sensor *sensor,
		      struct bt_mesh_msg_ctx *ctx, struct sensor_value *rsp)
{
	rsp[0].val1 = sensor_val;
	rsp[0].val2 = 0;

	printk("Motion Get: %d\n", sensor_val);

	return 0;
}

static struct sensor_value motion_threshold;

static void motion_threshold_get(struct bt_mesh_sensor *sensor,
				 const struct bt_mesh_sensor_setting *setting,
				 struct bt_mesh_msg_ctx *ctx,
				 struct sensor_value *rsp)
{
	printk("motion_threshold_get: sen:[%d], set:[%d], val1:[%d], val2:[%d], wr:[%d]\n",
	       sensor->type->id, setting->type->id,
	       motion_threshold.val1, motion_threshold.val2,
	       setting->set != NULL);

	*rsp = motion_threshold;
}

static int motion_threshold_set(struct bt_mesh_sensor *sensor,
				const struct bt_mesh_sensor_setting *setting,
				struct bt_mesh_msg_ctx *ctx,
				const struct sensor_value *value)
{
	printk("motion_threshold_set: sen:[%d], set:[%d], val1:[%d], val2:[%d], wr:[%d]\n",
	       sensor->type->id, setting->type->id, value->val1, value->val2, setting->set != NULL);

	motion_threshold = *value;
	return 0;
}

static struct bt_mesh_sensor_setting motion_settings[] = { {
	.type = &bt_mesh_sensor_motion_threshold,
	.get = motion_threshold_get,
	.set = motion_threshold_set,
} };

static const struct bt_mesh_sensor_descriptor motion_sensor_descriptor = {
	.tolerance = {
		.positive = { 5, 0 },
		.negative = { 5, 0 },
	},
	.sampling_type = BT_MESH_SENSOR_SAMPLING_ACCUMULATED,
	.period = 1000L, // 1 second
	.update_interval = 50L, // 50 ms
};

static struct bt_mesh_sensor motion_sensor = {
	.type = &bt_mesh_sensor_motion_sensed,
	.descriptor = &motion_sensor_descriptor,
	.get = motion_get,
	.settings = {
		.count = ARRAY_SIZE(motion_settings),
		.list = motion_settings,
	},
};

static struct bt_mesh_sensor *const sensors[] = {
	&motion_sensor,
};

static struct bt_mesh_sensor_srv sensor_srv =
	BT_MESH_SENSOR_SRV_INIT(sensors, ARRAY_SIZE(sensors));

static void sensor_status(struct bt_mesh_sensor_cli *cli,
			  struct bt_mesh_msg_ctx *ctx,
			  const struct bt_mesh_sensor_type *sensor,
			  const struct sensor_value *value)
{
	LOG_MODEL(cli->model, "sensor_status: %d\n", sensor->id);

	for (size_t i = 0; i < sensor->channel_count; i++) {
		LOG_MODEL(cli->model, "\t[%d]: %d.%d\n", i, value->val1, value->val2);
	}
}

static void sensor_descriptor_status(struct bt_mesh_sensor_cli *cli,
				     struct bt_mesh_msg_ctx *ctx,
				     const struct bt_mesh_sensor_info *sensor)
{
	LOG_MODEL(cli->model, "sensor_descriptor_status: %d [%d.%d %d.%d] %d %lld %lld\n",
		  sensor->id,
		  sensor->descriptor.tolerance.positive.val1,
		  sensor->descriptor.tolerance.positive.val2,
		  sensor->descriptor.tolerance.negative.val1,
		  sensor->descriptor.tolerance.negative.val2,
		  sensor->descriptor.sampling_type, sensor->descriptor.period,
		  sensor->descriptor.update_interval);
}

static void
sensor_cadence_status(struct bt_mesh_sensor_cli *cli,
		      struct bt_mesh_msg_ctx *ctx,
		      const struct bt_mesh_sensor_type *sensor,
		      const struct bt_mesh_sensor_cadence_status *cadence)
{
	LOG_MODEL(cli->model, "sensor_cadence_status: %d [%d %d] [%d %d.%d %d.%d] [%d %d.%d %d.%d]\n",
		  sensor->id,
		  cadence->fast_period_div, cadence->min_int,
		  cadence->threshold.delta.type,
		  cadence->threshold.delta.up.val1, cadence->threshold.delta.up.val2,
		  cadence->threshold.delta.down.val1, cadence->threshold.delta.down.val2,
		  cadence->threshold.range.cadence,
		  cadence->threshold.range.low.val1, cadence->threshold.range.low.val2,
		  cadence->threshold.range.high.val1, cadence->threshold.range.high.val2);
}

static void sensor_settings_status(struct bt_mesh_sensor_cli *cli,
				   struct bt_mesh_msg_ctx *ctx,
				   const struct bt_mesh_sensor_type *sensor,
				   const uint16_t *ids, uint32_t count)
{
	LOG_MODEL(cli->model, "sensor_setting*s*_status: %d %d\n", sensor->id, count);
}

static void
sensor_setting_status(struct bt_mesh_sensor_cli *cli,
		      struct bt_mesh_msg_ctx *ctx,
		      const struct bt_mesh_sensor_type *sensor,
		      const struct bt_mesh_sensor_setting_status *setting)
{
	LOG_MODEL(cli->model, "sensor_setting_status: %d %d %d.%d %d\n", sensor->id,
		  setting->type->id, setting->value[0].val1, setting->value[0].val2,
		  setting->writable);
}

static void sensor_series_status(
	struct bt_mesh_sensor_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_sensor_type *sensor, uint8_t index, uint8_t count,
	const struct bt_mesh_sensor_series_entry *entry)
{
	LOG_MODEL(cli->model, "sensor_series_status: %d %d %d %d.%d %d.%d\n",
		  sensor->id, index, count,
		  entry->column.start.val1, entry->column.start.val2,
		  entry->column.end.val1, entry->column.end.val2);

	for (size_t i = 0; i < sensor->channel_count; i++) {
		LOG_MODEL(cli->model, "sensor_series_status: e: %d.%d\n",
			  entry->value[i].val1, entry->value[i].val2);
	}
}

static void sensor_unknown_type(struct bt_mesh_sensor_cli *cli,
				struct bt_mesh_msg_ctx *ctx, uint16_t id,
				uint32_t opcode)
{
	LOG_MODEL(cli->model, "sensor_unknown_type: %d\n", id);
}

static const struct bt_mesh_sensor_cli_handlers sensor_handlers = {
	.data = sensor_status,
	.sensor = sensor_descriptor_status,
	.cadence = sensor_cadence_status,
	.settings = sensor_settings_status,
	.setting_status = sensor_setting_status,
	.series_entry = sensor_series_status,
	.unknown_type = sensor_unknown_type,
};

static struct bt_mesh_sensor_cli sensor_cli =
	BT_MESH_SENSOR_CLI_INIT(&sensor_handlers);

/* Health Server */
static struct k_delayed_work attention_blink_work;

static void attention_blink(struct k_work *work)
{
	static int idx;
	const uint8_t pattern[] = {
		BIT(0) | BIT(1),
		BIT(1) | BIT(2),
		BIT(2) | BIT(3),
		BIT(3) | BIT(0),
	};
	dk_set_leds(pattern[idx++ % ARRAY_SIZE(pattern)]);
	k_delayed_work_submit(&attention_blink_work, K_MSEC(30));
}

static void attention_on(struct bt_mesh_model *mod)
{
	k_delayed_work_submit(&attention_blink_work, K_NO_WAIT);
}

static void attention_off(struct bt_mesh_model *mod)
{
	k_delayed_work_cancel(&attention_blink_work);
	dk_set_leds(DK_NO_LEDS_MSK);
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
	.attn_on = attention_on,
	.attn_off = attention_off,
};

static struct bt_mesh_health_srv health_srv = {
	.cb = &health_srv_cb,
};

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(
		0, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_CFG_SRV,
			BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
#if CONFIG_APP_SCENE
			BT_MESH_MODEL_SCENE_SRV(&scene_srv),
			BT_MESH_MODEL_SCENE_CLI(&scene_cli),
#endif
//			BT_MESH_MODEL_TIME_SRV(&time_srv),
//			BT_MESH_MODEL_TIME_CLI(&time_cli)),
//			BT_MESH_MODEL_DTT_SRV(&dtt_srv),
//			BT_MESH_MODEL_DTT_CLI(&dtt_cli),
//			BT_MESH_MODEL_BATTERY_SRV(&battery_srv),
//			BT_MESH_MODEL_BATTERY_CLI(&battery_cli),
//			BT_MESH_MODEL_LOC_SRV(&loc_srv),
//			BT_MESH_MODEL_LOC_CLI(&loc_cli),
//			BT_MESH_MODEL_PROP_SRV_USER(&prop_srv),
//			BT_MESH_MODEL_PROP_CLI(&prop_cli),

#if CONFIG_APP_GEN_ONOFF_SRV
			BT_MESH_MODEL_ONOFF_SRV(&onoff_srv),
#endif
#if CONFIG_APP_GEN_LVL_SRV
			BT_MESH_MODEL_LVL_SRV(&lvl_srv),
#endif

/* Client models */
#if CONFIG_APP_GEN_ONOFF_CLI
			BT_MESH_MODEL_ONOFF_CLI(&onoff_cli),
#endif
#if CONFIG_APP_GEN_LVL_CLI
			BT_MESH_MODEL_LVL_CLI(&lvl_cli),
#endif
#if CONFIG_APP_GEN_PONOFF_CLI
			BT_MESH_MODEL_PONOFF_CLI(&ponoff_cli),
#endif
#if CONFIG_APP_GEN_PLVL_CLI
			BT_MESH_MODEL_PLVL_CLI(&plvl_cli),
#endif
#if CONFIG_APP_LIGHT_LIGHTNESS_CLI
			BT_MESH_MODEL_LIGHTNESS_CLI(&lightness_cli),
#endif
#if CONFIG_APP_LIGHT_LC_CLI
			BT_MESH_MODEL_LIGHT_CTRL_CLI(&light_lc_cli),
#endif
#if CONFIG_APP_LIGHT_CTL_CLI
			BT_MESH_MODEL_LIGHT_CTL_CLI(&light_ctl_cli),
#endif
#if CONFIG_APP_LIGHT_XYL_CLI
			BT_MESH_MODEL_LIGHT_XYL_CLI(&light_xyl_cli),
#endif
#if CONFIG_APP_LIGHT_HSL_CLI
			BT_MESH_MODEL_LIGHT_HSL_CLI(&light_hsl_cli),
#endif
#if CONFIG_APP_SENSOR_CLI
			BT_MESH_MODEL_SENSOR_CLI(&sensor_cli),
#endif
			),
		BT_MESH_MODEL_NONE),

/* Server models */
#if CONFIG_APP_GEN_PONOFF_SRV
	BT_MESH_ELEM(
		1, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_PONOFF_SRV(&ponoff_srv)),
		BT_MESH_MODEL_NONE),
#endif
#if CONFIG_APP_GEN_PLVL_SRV
	BT_MESH_ELEM(
		2, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_PLVL_SRV(&plvl_srv)),
		BT_MESH_MODEL_NONE),
#endif
#if CONFIG_APP_LIGHT_LIGHTNESS_SRV
	BT_MESH_ELEM(
		3, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_LIGHTNESS_SRV(&lightness_srv_standalone)),
		BT_MESH_MODEL_NONE),
#endif
#if CONFIG_APP_LIGHT_TEMP_SRV
	BT_MESH_ELEM(
		6, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_LIGHT_TEMP_SRV(&light_temp_srv)),
		BT_MESH_MODEL_NONE),
#endif
#if CONFIG_APP_LIGHT_CTL_SRV
	BT_MESH_ELEM(
		7, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_LIGHT_CTL_SRV(&light_ctl_srv)),
		BT_MESH_MODEL_NONE),
	BT_MESH_ELEM(
		8, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_LIGHT_TEMP_SRV(&light_ctl_srv.temp_srv)),
		BT_MESH_MODEL_NONE),
#endif
#if CONFIG_APP_LIGHT_XYL_SRV
	BT_MESH_ELEM(
		9, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_LIGHT_XYL_SRV(&light_xyl_srv)),
		BT_MESH_MODEL_NONE),
#endif
#if CONFIG_APP_LIGHT_HSL_SRV
	BT_MESH_ELEM(
		10, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_LIGHT_HSL_SRV(&light_hsl_srv)),
		BT_MESH_MODEL_NONE),
	BT_MESH_ELEM(
		11, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_LIGHT_HUE_SRV(&light_hsl_srv.hue)),
		BT_MESH_MODEL_NONE),
	BT_MESH_ELEM(
		12, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_LIGHT_SAT_SRV(&light_hsl_srv.sat)),
		BT_MESH_MODEL_NONE),
#endif
#if CONFIG_APP_LIGHT_LC_SRV
	BT_MESH_ELEM(
		4, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_LIGHTNESS_SRV(&lightness_srv_lc)),
		BT_MESH_MODEL_NONE),
	BT_MESH_ELEM(
		5, BT_MESH_MODEL_LIST(
			BT_MESH_MODEL_LIGHT_CTRL_SRV(&light_lc_srv)),
		BT_MESH_MODEL_NONE),
#endif
#if CONFIG_APP_SENSOR_SRV
	BT_MESH_ELEM(13,
	     BT_MESH_MODEL_LIST(
		     BT_MESH_MODEL_SENSOR_SRV(&sensor_srv)),
	     BT_MESH_MODEL_NONE),
#endif
};

static const struct bt_mesh_comp comp = {
	.cid = CONFIG_BT_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

const struct bt_mesh_comp *model_handler_init(void)
{
	k_delayed_work_init(&attention_blink_work, attention_blink);

	return &comp;
}

static int cmd_scene_store(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	int err;
	uint16_t scene = strtol(argv[1], NULL, 0);

	err = bt_mesh_scene_cli_store(&scene_cli, NULL, scene, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_scene_recall(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	int err;
	uint16_t scene = strtol(argv[1], NULL, 0);

	err = bt_mesh_scene_cli_recall(&scene_cli, NULL, scene, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_scene_get(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 1) {
		return -EINVAL;
	}

	int err;

	err = bt_mesh_scene_cli_get(&scene_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_onoff_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	int err;
	struct bt_mesh_onoff_set set = {
		.on_off = strtol(argv[1], NULL, 0),
	};

	err = bt_mesh_onoff_cli_set(&onoff_cli, NULL, &set, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_onoff_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_onoff_cli_get(&onoff_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_level_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	int err;
	struct bt_mesh_lvl_set set = {
		.lvl = strtol(argv[1], NULL, 0),
		.new_transaction = true,
	};

	err = bt_mesh_lvl_cli_set(&lvl_cli, NULL, &set, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_level_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_lvl_cli_get(&lvl_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_ponoff_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	int err;
	enum bt_mesh_on_power_up on_power_up = strtol(argv[1], NULL, 0);

	err = bt_mesh_ponoff_cli_on_power_up_set(&ponoff_cli, NULL, on_power_up, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_ponoff_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_ponoff_cli_on_power_up_get(&ponoff_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_plevel_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	int err;
	struct bt_mesh_plvl_set set = {
		.power_lvl = strtol(argv[1], NULL, 0),
	};

	err = bt_mesh_plvl_cli_power_set(&plvl_cli, NULL, &set, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_plevel_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_plvl_cli_power_get(&plvl_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_lightness_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	int err;
	struct bt_mesh_lightness_set set = {
		.lvl = strtol(argv[1], NULL, 0),
	};

	err = bt_mesh_lightness_cli_light_set(&lightness_cli, NULL, &set, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_lightness_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_lightness_cli_light_get(&lightness_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_lightness_default_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	int err;
	uint16_t default_lvl = strtol(argv[1], NULL, 0);

	err = bt_mesh_lightness_cli_default_set(&lightness_cli, NULL, default_lvl, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_lightness_default_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_lightness_cli_default_get(&lightness_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_lc_mode_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	int err;
	bool enable = strtol(argv[1], NULL, 0);

	err = bt_mesh_light_ctrl_cli_mode_set(&light_lc_cli, NULL, enable, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_lc_mode_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_light_ctrl_cli_mode_get(&light_lc_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_lc_light_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	int err;
	struct bt_mesh_onoff_set set = {
		.on_off = strtol(argv[1], NULL, 0),
	};

	err = bt_mesh_light_ctrl_cli_light_onoff_set(&light_lc_cli, NULL, &set, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_lc_light_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_light_ctrl_cli_light_onoff_get(&light_lc_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_ctl_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 4) {
		return -EINVAL;
	}

	int err;
	struct bt_mesh_light_ctl_set set = {
		.params.light = strtol(argv[1], NULL, 0),
		.params.temp = strtol(argv[2], NULL, 0),
		.params.delta_uv = strtol(argv[3], NULL, 0),
	};

	err = bt_mesh_light_ctl_set(&light_ctl_cli, NULL, &set, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_ctl_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_light_ctl_get(&light_ctl_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_temp_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 3) {
		return -EINVAL;
	}

	int err;
	struct bt_mesh_light_temp_set set = {
		.params.temp = strtol(argv[1], NULL, 0),
		.params.delta_uv = strtol(argv[2], NULL, 0),
	};

	err = bt_mesh_light_temp_set(&light_ctl_cli, NULL, &set, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_temp_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_light_temp_get(&light_ctl_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_xyl_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 4) {
		return -EINVAL;
	}

	int err;
	struct bt_mesh_light_xyl_set_params set = {
		.params.lightness = strtol(argv[1], NULL, 0),
		.params.xy.x = strtol(argv[2], NULL, 0),
		.params.xy.y = strtol(argv[3], NULL, 0),
	};

	err = bt_mesh_light_xyl_set(&light_xyl_cli, NULL, &set, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_xyl_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_light_xyl_get(&light_xyl_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_hsl_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 4) {
		return -EINVAL;
	}

	int err;
	struct bt_mesh_light_hsl_params set = {
		.params.lightness = strtol(argv[1], NULL, 0),
		.params.hue = strtol(argv[2], NULL, 0),
		.params.saturation = strtol(argv[3], NULL, 0),
	};

	err = bt_mesh_light_hsl_set(&light_hsl_cli, NULL, &set, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_hsl_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_light_hsl_get(&light_hsl_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_hsl_range_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 5) {
		return -EINVAL;
	}

	int err;
	struct bt_mesh_light_hue_sat_range set = {
		.min.hue = strtol(argv[1], NULL, 0),
		.min.saturation = strtol(argv[2], NULL, 0),
		.max.hue = strtol(argv[3], NULL, 0),
		.max.saturation = strtol(argv[4], NULL, 0),
	};

	err = bt_mesh_light_hsl_range_set(&light_hsl_cli, NULL, &set, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_hsl_range_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err;

	err = bt_mesh_light_hsl_range_get(&light_hsl_cli, NULL, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_sensor_setting_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 3) {
		return -EINVAL;
	}

	struct sensor_value setting_value = {
		.val1 = strtol(argv[1], NULL, 0),
		.val2 = strtol(argv[2], NULL, 0),
	};

	int err = bt_mesh_sensor_cli_setting_set(&sensor_cli, NULL,
					      &bt_mesh_sensor_motion_sensed,
					      &bt_mesh_sensor_motion_threshold,
					      &setting_value, NULL);
	shell_print(app_shell, "err: %d", err);
	return 0;
}

static int cmd_sensor_setting_get(const struct shell *shell, size_t argc, char *argv[])
{
	int err = bt_mesh_sensor_cli_setting_get(&sensor_cli, NULL,
					      &bt_mesh_sensor_motion_sensed,
					      &bt_mesh_sensor_motion_threshold,
					      NULL);
	shell_print(app_shell, "err: %d", err);
	return 0;
}

static struct bt_mesh_sensor_cadence_status cadence_data = {
	.fast_period_div = 4,
	.min_int = 9,
	.threshold.delta.type = BT_MESH_SENSOR_DELTA_VALUE,
	.threshold.delta.up.val1 = 1,
	.threshold.delta.up.val2 = 0,
	.threshold.delta.down.val1 = 1,
	.threshold.delta.down.val2 = 0,
	.threshold.range.cadence = BT_MESH_SENSOR_CADENCE_FAST,
	.threshold.range.low.val1 = 49,
	.threshold.range.low.val2 = 0,
	.threshold.range.high.val1 = 100,
	.threshold.range.high.val2 = 0,
};

static int cmd_sensor_cadence_fast_pub_div_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	cadence_data.fast_period_div = strtol(argv[1], NULL, 0);

	return 0;
}

static int cmd_sensor_cadence_min_int_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	cadence_data.min_int = strtol(argv[1], NULL, 0);

	return 0;
}

static int cmd_sensor_cadence_threshold_delta_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 6) {
		return -EINVAL;
	}

	cadence_data.threshold.delta.type = strtol(argv[1], NULL, 0);
	cadence_data.threshold.delta.up.val1 = strtol(argv[2], NULL, 0);
	cadence_data.threshold.delta.up.val2 = strtol(argv[3], NULL, 0);
	cadence_data.threshold.delta.down.val1 = strtol(argv[4], NULL, 0);
	cadence_data.threshold.delta.down.val2 = strtol(argv[5], NULL, 0);

	return 0;
}

static int cmd_sensor_cadence_threshold_range_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 6) {
		return -EINVAL;
	}

	cadence_data.threshold.range.cadence = strtol(argv[1], NULL, 0);
	cadence_data.threshold.range.low.val1 = strtol(argv[2], NULL, 0);
	cadence_data.threshold.range.low.val2 = strtol(argv[3], NULL, 0);
	cadence_data.threshold.range.high.val1 = strtol(argv[4], NULL, 0);
	cadence_data.threshold.range.high.val2 = strtol(argv[5], NULL, 0);

	return 0;
}

static int cmd_sensor_cadence_set(const struct shell *shell, size_t argc, char *argv[])
{
	int err = bt_mesh_sensor_cli_cadence_set(&sensor_cli, NULL,
					bt_mesh_sensor_type_get(BT_MESH_PROP_ID_MOTION_SENSED),
					&cadence_data, NULL);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_sensor_value_set(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	sensor_val = strtol(argv[1], NULL, 0);

	return 0;
}

static int cmd_sensor_pub(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 3) {
		return -EINVAL;
	}

	struct sensor_value value = {
		.val1 = strtol(argv[1], NULL, 0),
		.val2 = strtol(argv[2], NULL, 0),
	};

	int err = bt_mesh_sensor_srv_pub(&sensor_srv, NULL, &motion_sensor, &value);
	shell_print(app_shell, "err: %d", err);

	return 0;
}

static int cmd_rpl_store(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

#if CONFIG_BT_SETTINGS
	uint16_t addr = strtol(argv[1], NULL, 0);
	bt_mesh_rpl_pending_store(addr);
#endif

	return 0;
}

static int cmd_iv_update_test(const struct shell *shell, size_t argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	if (!IS_ENABLED(CONFIG_BT_MESH_IV_UPDATE_TEST)) {
		shell_print(app_shell, "Manual IV Update not supported");
		return 0;
	}

	bool enable = strtol(argv[1], NULL, 0);
	bt_mesh_iv_update_test(enable);

	return 0;
}

static int cmd_iv_update_toggle(const struct shell *shell, size_t argc, char *argv[])
{
	bool enabled;

	if (!IS_ENABLED(CONFIG_BT_MESH_IV_UPDATE_TEST)) {
		shell_print(app_shell, "Manual IV Update not supported");
		return 0;
	}

	enabled = bt_mesh_iv_update();
	shell_print(app_shell, "IV Update %sentered", enabled ? "" : "not ");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(app_cmds,
	SHELL_CMD_ARG(scene_store, NULL, "Send Scene Store message: <scene>", cmd_scene_store, 2, 0),
	SHELL_CMD_ARG(scene_recall, NULL, "Send Scene Recall message: <scene>", cmd_scene_recall, 2, 0),
	SHELL_CMD_ARG(scene_get, NULL, "Send Scene Get message", cmd_scene_get, 1, 0),

	SHELL_CMD_ARG(onoff_set, NULL, "Send Generic OnOff Set message: <onoff>", cmd_onoff_set, 2, 0),
	SHELL_CMD_ARG(onoff_get, NULL, "Send Generic OnOff Get message", cmd_onoff_get, 1, 0),

	SHELL_CMD_ARG(level_set, NULL, "Send Generic Level Set message: <level>", cmd_level_set, 2, 0),
	SHELL_CMD_ARG(level_get, NULL, "Send Generic Level Get message", cmd_level_get, 1, 0),

	SHELL_CMD_ARG(ponoff_set, NULL, "Send Generic On Power Up Set message: <on_power_up>", cmd_ponoff_set, 2, 0),
	SHELL_CMD_ARG(ponoff_get, NULL, "Send Generic On Power Up Get message", cmd_ponoff_get, 1, 0),

	SHELL_CMD_ARG(plevel_set, NULL, "Send Generic Power Level Set message: <level>", cmd_plevel_set, 2, 0),
	SHELL_CMD_ARG(plevel_get, NULL, "Send Generic Power Level Get message", cmd_plevel_get, 1, 0),

	SHELL_CMD_ARG(lightness_set, NULL, "Send Light Ligthness Set message: <lightness>", cmd_lightness_set, 2, 0),
	SHELL_CMD_ARG(lightness_get, NULL, "Send Light Lightness Get message", cmd_lightness_get, 1, 0),

	SHELL_CMD_ARG(lightness_default_set, NULL, "Send Light Ligthness Default Set message: <lightness>", cmd_lightness_default_set, 2, 0),
	SHELL_CMD_ARG(lightness_default_get, NULL, "Send Light Lightness Default Get message", cmd_lightness_default_get, 1, 0),

	SHELL_CMD_ARG(lc_mode_set, NULL, "Send Light LC Mode Set message: <enable>", cmd_lc_mode_set, 2, 0),
	SHELL_CMD_ARG(lc_mode_get, NULL, "Send Light LC Mode Get message", cmd_lc_mode_get, 1, 0),

	SHELL_CMD_ARG(lc_light_set, NULL, "Send Light LC Light OnOff Set message: <onoff>", cmd_lc_light_set, 2, 0),
	SHELL_CMD_ARG(lc_light_get, NULL, "Send Light LC Light OnOff Get message", cmd_lc_light_get, 1, 0),

	SHELL_CMD_ARG(ctl_set, NULL, "Send Light CTL Set message: <lightness> <temperature> <delta_uv>", cmd_ctl_set, 4, 0),
	SHELL_CMD_ARG(ctl_get, NULL, "Send Light CTL Get message", cmd_ctl_get, 1, 0),

	SHELL_CMD_ARG(temp_set, NULL, "Send Light Temperature Set message: <temperature> <delta_uv>", cmd_temp_set, 3, 0),
	SHELL_CMD_ARG(temp_get, NULL, "Send Light Temperature Get message", cmd_temp_get, 1, 0),

	SHELL_CMD_ARG(xyl_set, NULL, "Send Light xyL Set message: <lightness> <x> <y>", cmd_xyl_set, 4, 0),
	SHELL_CMD_ARG(xyl_get, NULL, "Send Light xyL Get message", cmd_xyl_get, 1, 0),

	SHELL_CMD_ARG(hsl_set, NULL, "Send Light HSL Set message: <lightness> <hue> <saturation>", cmd_hsl_set, 4, 0),
	SHELL_CMD_ARG(hsl_get, NULL, "Send Light HSL Get message", cmd_hsl_get, 1, 0),

	SHELL_CMD_ARG(hsl_range_set, NULL, "Send Light HSL Range Set message: <min.hue> <min.saturation> <max.hue> <max.saturation>", cmd_hsl_range_set, 5, 0),
	SHELL_CMD_ARG(hsl_range_get, NULL, "Send Light HSL Range Get message", cmd_hsl_range_get, 1, 0),

	SHELL_CMD_ARG(sensor_setting_set, NULL, "Send Sensor Setting Set message: <val1> <val2>", cmd_sensor_setting_set, 3, 0),
	SHELL_CMD_ARG(sensor_setting_get, NULL, "Send Sensor Setting Get message", cmd_sensor_setting_get, 1, 0),
	SHELL_CMD_ARG(sensor_cadence_fast_pub_div_set, NULL, "Set Sensor Cadence Fast Pub Div: <div>", cmd_sensor_cadence_fast_pub_div_set, 2, 0),
	SHELL_CMD_ARG(sensor_cadence_min_int_set, NULL, "Set Sensor Cadence Min Int: <int>", cmd_sensor_cadence_min_int_set, 2, 0),
	SHELL_CMD_ARG(sensor_cadence_threshold_delta_set, NULL, "Set Sensor Cadence Threshold Delta: <delta...>", cmd_sensor_cadence_threshold_delta_set, 6, 0),
	SHELL_CMD_ARG(sensor_cadence_threshold_range_set, NULL, "Set Sensor Cadence Threshold Range: <range...>", cmd_sensor_cadence_threshold_range_set, 6, 0),
	SHELL_CMD_ARG(sensor_cadence_set, NULL, "Send Sensor Cadence Set message", cmd_sensor_cadence_set, 1, 0),
	SHELL_CMD_ARG(sensor_value_set, NULL, "Set Sensor value: <val1>", cmd_sensor_value_set, 2, 0),
	SHELL_CMD_ARG(sensor_pub, NULL, "Send Sensor Status message: <val1> <val2>", cmd_sensor_pub, 3, 0),

	SHELL_CMD_ARG(rpl_store, NULL, "Store pending RPL entries", cmd_rpl_store, 2, 0),
	SHELL_CMD_ARG(iv_update_test, NULL, "IV Update test", cmd_iv_update_test, 2, 0),
	SHELL_CMD_ARG(iv_update_toggle, NULL, "IV Update toggle", cmd_iv_update_toggle, 1, 0),

	SHELL_SUBCMD_SET_END
);

static int cmd_app(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(shell);
		/* shell returns 1 when help is printed */
		return 1;
	}

	shell_error(shell, "%s unknown parameter: %s", argv[0], argv[1]);

	return -EINVAL;
}

SHELL_CMD_ARG_REGISTER(app, &app_cmds, "App commands", cmd_app, 1, 1);

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	dk_leds_init();
	dk_buttons_init(NULL);

	err = bt_mesh_init(bt_mesh_dk_prov_init(), model_handler_init());
	if (err) {
		printk("Initializing mesh failed (err %d)\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	/* This will be a no-op if settings_load() loaded provisioning info */
	bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);

	printk("Mesh initialized\n");
}

void main(void)
{
	int err;

	printk("Initializing...\n");

	app_shell = shell_backend_uart_get_ptr();
	shell_print(app_shell, ">>> Shell is ready <<<");

	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}
}
