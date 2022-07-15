/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <bluetooth/mesh/models.h>
#include <bluetooth/mesh/light_hsl.h>
#include <bluetooth/mesh/light_hue_srv.h>
#include <bluetooth/mesh/light_sat_srv.h>
#include <bluetooth/mesh/light_hsl_srv.h>
#include <dk_buttons_and_leds.h>
#include "model_handler.h"
#include "led_strip.h"

struct light_hsl_ctx {
	const struct bt_mesh_light_hsl_srv *hsl_srv;

	struct k_work_delayable lvl_per_work;
	uint16_t target_lvl;
	uint16_t current_lvl;
	uint32_t lvl_time_per;
	uint32_t lvl_rem_time;

	struct k_work_delayable hue_per_work;
	uint16_t target_hue;
	uint16_t current_hue;
	uint32_t hue_time_per;
	uint32_t hue_rem_time;

	struct k_work_delayable sat_per_work;
	uint16_t target_sat;
	uint16_t current_sat;
	uint32_t sat_time_per;
	uint32_t sat_rem_time;
};

/* Pre-declare local static functions for use in callback definitions */
static void light_set(struct bt_mesh_lightness_srv *srv, struct bt_mesh_msg_ctx *ctx, const struct bt_mesh_lightness_set *set, struct bt_mesh_lightness_status *rsp);
static void light_get(struct bt_mesh_lightness_srv *srv, struct bt_mesh_msg_ctx *ctx, struct bt_mesh_lightness_status *rsp);

static void light_hue_set(struct bt_mesh_light_hue_srv *srv, struct bt_mesh_msg_ctx *ctx, const struct bt_mesh_light_hue *set, struct bt_mesh_light_hue_status *rsp);
static void light_hue_get(struct bt_mesh_light_hue_srv *srv, struct bt_mesh_msg_ctx *ctx, struct bt_mesh_light_hue_status *rsp);
static void light_hue_delta_set(struct bt_mesh_light_hue_srv *srv, struct bt_mesh_msg_ctx *ctx, const struct bt_mesh_light_hue_delta *delta_set, struct bt_mesh_light_hue_status *rsp);
static void light_hue_move_set(struct bt_mesh_light_hue_srv *srv, struct bt_mesh_msg_ctx *ctx, const struct bt_mesh_light_hue_move *move, struct bt_mesh_light_hue_status *rsp);

static void light_sat_set(struct bt_mesh_light_sat_srv *srv, struct bt_mesh_msg_ctx *ctx, const struct bt_mesh_light_sat *set, struct bt_mesh_light_sat_status *rsp);
static void light_sat_get(struct bt_mesh_light_sat_srv *srv, struct bt_mesh_msg_ctx *ctx, struct bt_mesh_light_sat_status *rsp);			  
/* End Pre-declarations */

static const struct bt_mesh_lightness_srv_handlers lightness_srv_handlers = {
	.light_set = light_set,
	.light_get = light_get,
};

static const struct bt_mesh_light_hue_srv_handlers light_hue_handlers = {
	.set = light_hue_set,
	.get = light_hue_get,
	.delta_set = light_hue_delta_set,
	.move_set = light_hue_move_set
};
static const struct bt_mesh_light_sat_srv_handlers light_sat_handlers = {
	.get = light_sat_get,
	.set = light_sat_set
};

static struct bt_mesh_lightness_srv my_lightness_srv = BT_MESH_LIGHTNESS_SRV_INIT(&lightness_srv_handlers);
static struct bt_mesh_light_hsl_srv my_hsl_srv = BT_MESH_LIGHT_HSL_SRV_INIT(&my_lightness_srv, &light_hue_handlers, &light_sat_handlers);

static struct light_hsl_ctx my_hsl_ctx = {
	.hsl_srv = &my_hsl_srv
};

/* Set up a repeating delayed work to blink the DK's LEDs when attention is
 * requested.
 */
static struct k_work_delayable attention_blink_work;
static bool attention;

static void attention_blink(struct k_work *work)
{
	static int idx;
	const uint8_t pattern[] = {
#if DT_NODE_EXISTS(DT_ALIAS(led0))
		BIT(0),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led1))
		BIT(1),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led2))
		BIT(2),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led3))
		BIT(3),
#endif
	};

	if (attention) {
		dk_set_leds(pattern[idx++ % ARRAY_SIZE(pattern)]);
		k_work_reschedule(&attention_blink_work, K_MSEC(30));
	} else {
		dk_set_leds(DK_NO_LEDS_MSK);
	}
}

static void attention_on(struct bt_mesh_model *mod)
{
	attention = true;
	k_work_reschedule(&attention_blink_work, K_NO_WAIT);
}

static void attention_off(struct bt_mesh_model *mod)
{
	/* Will stop rescheduling blink timer */
	attention = false;
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
	.attn_on = attention_on,
	.attn_off = attention_off,
};

static struct bt_mesh_health_srv health_srv = {
	.cb = &health_srv_cb,
};

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

static void start_new_light_trans(const struct bt_mesh_lightness_set *set,
				  struct light_hsl_ctx *ctx)
{
	uint32_t step_cnt = abs(set->lvl - ctx->current_lvl);
	uint32_t time = set->transition ? set->transition->time : 0;
	uint32_t delay = set->transition ? set->transition->delay : 0;

	ctx->target_lvl = set->lvl;
	ctx->lvl_time_per = (step_cnt ? time / step_cnt : 0);
	ctx->lvl_rem_time = time;
	k_work_reschedule(&ctx->lvl_per_work, K_MSEC(delay));

	printk("New light transition-> Lvl: %d, Time: %d, Delay: %d\n",
	       set->lvl, time, delay);
}

static void periodic_led_work(struct k_work *work)
{
	struct light_hsl_ctx *lh_ctx = &my_hsl_ctx;
	lh_ctx->lvl_rem_time -= lh_ctx->lvl_time_per;

	if ((lh_ctx->lvl_rem_time <= lh_ctx->lvl_time_per) ||
	    (abs(lh_ctx->target_lvl - lh_ctx->current_lvl) <= 1)) {

		struct bt_mesh_lightness_status status = {
			.current = lh_ctx->target_lvl,
			.target = lh_ctx->target_lvl,
		};

		lh_ctx->current_lvl = lh_ctx->target_lvl;
		lh_ctx->lvl_rem_time = 0;

		bt_mesh_lightness_srv_pub(lh_ctx->hsl_srv->lightness, NULL, &status);

		struct bt_mesh_light_hsl_status hsl_status = {
			.params = {
				.hue = lh_ctx->current_hue,
				.lightness = lh_ctx->current_lvl,
				.saturation = lh_ctx->current_sat,
			},
			.remaining_time = 0,
		};

		bt_mesh_light_hsl_srv_pub(lh_ctx->hsl_srv, NULL, &hsl_status);
	} else {
		lh_ctx->current_lvl += lh_ctx->target_lvl > lh_ctx->current_lvl ? 1 : -1;
		k_work_reschedule(&lh_ctx->lvl_per_work, K_MSEC(lh_ctx->lvl_time_per));
	}

	struct bt_mesh_light_hsl hsl = {
		.hue = lh_ctx->current_hue,
		.lightness = lh_ctx->current_lvl,
		.saturation = lh_ctx->current_sat,
	};

	set_led_strip(
		bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_RED), 
		bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_GREEN),
		bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_BLUE));

	printk("Current light lvl: %u/65535\n", lh_ctx->current_lvl);
}

static void light_set(struct bt_mesh_lightness_srv *srv,
		      struct bt_mesh_msg_ctx *ctx,
		      const struct bt_mesh_lightness_set *set,
		      struct bt_mesh_lightness_status *rsp)
{
	struct light_hsl_ctx *lh_ctx = &my_hsl_ctx;

	if (lh_ctx->current_lvl == set->lvl) { 
		// we're already at the desired level, don't start a new transition
		lh_ctx->target_lvl = set->lvl;
		lh_ctx->lvl_rem_time = 0;

		// struct bt_mesh_lightness_status status = {
		// 	.current = lh_ctx->target_lvl,
		// 	.target = lh_ctx->target_lvl,
		// };

		// bt_mesh_lightness_srv_pub(lh_ctx->hsl_srv->lightness, NULL, &status);

		struct bt_mesh_light_hsl hsl = {
			.hue = lh_ctx->current_hue,
			.lightness = lh_ctx->current_lvl,
			.saturation = lh_ctx->current_sat,
		};

		set_led_strip(
			bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_RED), 
			bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_GREEN),
			bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_BLUE));
	}
	else {
		start_new_light_trans(set, lh_ctx);
	}
	rsp->current = lh_ctx->current_lvl;
	rsp->target = lh_ctx->target_lvl;
	rsp->remaining_time = set->transition ? set->transition->time : 0;
}

static void light_get(struct bt_mesh_lightness_srv *srv,
		      struct bt_mesh_msg_ctx *ctx,
		      struct bt_mesh_lightness_status *rsp)
{
	struct light_hsl_ctx *lh_ctx = &my_hsl_ctx;

	rsp->current = lh_ctx->current_lvl;
	rsp->target = lh_ctx->target_lvl;
	rsp->remaining_time = lh_ctx->lvl_rem_time;
}

static void periodic_hue_work(struct k_work *work)
{
	struct light_hsl_ctx *lh_ctx = &my_hsl_ctx;
	lh_ctx->hue_rem_time -= lh_ctx->hue_time_per;

	if ((lh_ctx->hue_rem_time <= lh_ctx->hue_time_per) ||
	    (abs(lh_ctx->target_hue - lh_ctx->current_hue) <= 1)) {
		struct bt_mesh_light_hsl_status status = {
			.params = {
				.hue = lh_ctx->current_hue,
				.lightness = lh_ctx->current_lvl,
				.saturation = lh_ctx->current_sat,
			},
			.remaining_time = 0,
		};

		lh_ctx->current_hue = lh_ctx->target_hue;
		lh_ctx->hue_rem_time = 0;

		bt_mesh_light_hsl_srv_pub(lh_ctx->hsl_srv, NULL, &status);
	} else {
		lh_ctx->current_hue += lh_ctx->target_hue > lh_ctx->current_hue ? 1 : -1;
		k_work_reschedule(&lh_ctx->hue_per_work, K_MSEC(lh_ctx->hue_time_per));
	}

	struct bt_mesh_light_hsl hsl = {
		.hue = lh_ctx->current_hue,
		.lightness = lh_ctx->current_lvl,
		.saturation = lh_ctx->current_sat,
	};

	set_led_strip(
		bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_RED), 
		bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_GREEN),
		bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_BLUE));

	printk("Current light hue: %u/65535\n", lh_ctx->current_hue);
}

static void start_new_hue_trans(const struct bt_mesh_light_hue *set,
				  struct light_hsl_ctx *ctx)
{
	uint32_t step_cnt = abs(set->lvl - ctx->current_hue);
	uint32_t time = set->transition ? set->transition->time : 0;
	uint32_t delay = set->transition ? set->transition->delay : 0;

	ctx->target_hue = set->lvl;
	ctx->hue_time_per = (step_cnt ? time / step_cnt : 0);
	ctx->hue_rem_time = time;
	k_work_reschedule(&ctx->hue_per_work, K_MSEC(delay));

	printk("New hue transition-> Lvl: %d, Time: %d, Delay: %d\n",
	       set->lvl, time, delay);
}

static void light_hue_set(struct bt_mesh_light_hue_srv *srv,
			  struct bt_mesh_msg_ctx *ctx,
			  const struct bt_mesh_light_hue *set,
			  struct bt_mesh_light_hue_status *rsp)
{
	struct light_hsl_ctx *lh_ctx = &my_hsl_ctx;

	if (lh_ctx->current_hue == set->lvl) {
		lh_ctx->target_hue = set->lvl;
		lh_ctx->hue_rem_time = 0;

		// struct bt_mesh_light_hue_status status = {
		// 	.current = lh_ctx->target_hue,
		// 	.target = lh_ctx->target_hue,
		// };

		// int rc = bt_mesh_light_hue_srv_pub(&lh_ctx->hsl_srv->hue, ctx, &status);
		// if (rc < 0) {
		// 	printk("Error publishing hue status update: %u/65535\n", rc);
		// }

		struct bt_mesh_light_hsl hsl = {
			.hue = lh_ctx->current_hue,
			.lightness = lh_ctx->current_lvl,
			.saturation = lh_ctx->current_sat,
		};

		set_led_strip(
			bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_RED), 
			bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_GREEN),
			bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_BLUE));

	}
	else {
		start_new_hue_trans(set, lh_ctx);
	}

	rsp->current = lh_ctx->current_hue;
	rsp->remaining_time = lh_ctx->hue_rem_time;
	rsp->target = lh_ctx->target_hue;
}

static void light_hue_get(struct bt_mesh_light_hue_srv *srv,
			  struct bt_mesh_msg_ctx *ctx,
			  struct bt_mesh_light_hue_status *rsp)
{
	struct light_hsl_ctx *lh_ctx = &my_hsl_ctx;

	rsp->current = lh_ctx->current_hue;
	rsp->remaining_time = lh_ctx->hue_rem_time;
	rsp->target = lh_ctx->target_hue;
}

static void light_hue_delta_set(
		struct bt_mesh_light_hue_srv *srv,
		struct bt_mesh_msg_ctx *ctx,
		const struct bt_mesh_light_hue_delta *delta_set,
		struct bt_mesh_light_hue_status *rsp)
{
	struct light_hsl_ctx *lh_ctx = &my_hsl_ctx;

	// TODO: Actually implement the delta process

	rsp->current = lh_ctx->current_hue;
	rsp->remaining_time = lh_ctx->hue_rem_time;
	rsp->target = lh_ctx->target_hue;
}

static void light_hue_move_set(struct bt_mesh_light_hue_srv *srv,
			       struct bt_mesh_msg_ctx *ctx,
			       const struct bt_mesh_light_hue_move *move,
			       struct bt_mesh_light_hue_status *rsp)
{
	struct light_hsl_ctx *lh_ctx = &my_hsl_ctx;

	// TODO: Actually implement the move process

	rsp->current = lh_ctx->current_hue;
	rsp->remaining_time = lh_ctx->hue_rem_time;
	rsp->target = lh_ctx->target_hue;
}

static void periodic_sat_work(struct k_work *work)
{
	struct light_hsl_ctx *lh_ctx = &my_hsl_ctx;
	lh_ctx->sat_rem_time -= lh_ctx->sat_time_per;

	if ((lh_ctx->sat_rem_time <= lh_ctx->sat_time_per) ||
	    (abs(lh_ctx->target_sat - lh_ctx->current_sat) <= 1)) {
		struct bt_mesh_light_hsl_status status = {
			.params = {
				.hue = lh_ctx->current_hue,
				.lightness = lh_ctx->current_lvl,
				.saturation = lh_ctx->current_sat,
			},
			.remaining_time = 0,
		};

		lh_ctx->current_sat = lh_ctx->target_sat;
		lh_ctx->sat_rem_time = 0;

		bt_mesh_light_hsl_srv_pub(lh_ctx->hsl_srv, NULL, &status);
	} else {
		lh_ctx->current_sat += lh_ctx->target_sat > lh_ctx->current_sat ? 1 : -1;
		k_work_reschedule(&lh_ctx->sat_per_work, K_MSEC(lh_ctx->sat_time_per));
	}

	struct bt_mesh_light_hsl hsl = {
		.hue = lh_ctx->current_hue,
		.lightness = lh_ctx->current_lvl,
		.saturation = lh_ctx->current_sat,
	};

	set_led_strip(
		bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_RED), 
		bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_GREEN),
		bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_BLUE));

	printk("Current light sat: %u/65535\n", lh_ctx->current_sat);
}

static void start_new_sat_trans(const struct bt_mesh_light_sat *set,
				  struct light_hsl_ctx *ctx)
{
	uint32_t step_cnt = abs(set->lvl - ctx->current_sat);
	uint32_t time = set->transition ? set->transition->time : 0;
	uint32_t delay = set->transition ? set->transition->delay : 0;

	ctx->target_sat = set->lvl;
	ctx->sat_time_per = (step_cnt ? time / step_cnt : 0);
	ctx->sat_rem_time = time;
	k_work_reschedule(&ctx->sat_per_work, K_MSEC(delay));

	printk("New sat transition-> Lvl: %d, Time: %d, Delay: %d\n",
	       set->lvl, time, delay);
}

static void light_sat_set(struct bt_mesh_light_sat_srv *srv,
			  struct bt_mesh_msg_ctx *ctx,
			  const struct bt_mesh_light_sat *set,
			  struct bt_mesh_light_sat_status *rsp)
{
	struct light_hsl_ctx *lh_ctx = &my_hsl_ctx;

	if (lh_ctx->current_sat == set->lvl) {
		lh_ctx->target_sat = set->lvl;
		lh_ctx->sat_rem_time = 0;

		// struct bt_mesh_light_sat_status status = {
		// 	.current = lh_ctx->target_sat,
		// 	.target = lh_ctx->target_sat,
		// };

		// bt_mesh_light_sat_srv_pub(&lh_ctx->hsl_srv->sat, NULL, &status);

		struct bt_mesh_light_hsl hsl = {
			.hue = lh_ctx->current_hue,
			.lightness = lh_ctx->current_lvl,
			.saturation = lh_ctx->current_sat,
		};

		set_led_strip(
			bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_RED), 
			bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_GREEN),
			bt_mesh_light_hsl_to_rgb(&hsl, BT_MESH_RGB_CH_BLUE));
	}
	else {
		start_new_sat_trans(set, lh_ctx);
	}

	rsp->current = lh_ctx->current_sat;
	rsp->remaining_time = lh_ctx->sat_rem_time;
	rsp->target = lh_ctx->target_sat;
}

static void light_sat_get(struct bt_mesh_light_sat_srv *srv,
			  struct bt_mesh_msg_ctx *ctx,
			  struct bt_mesh_light_sat_status *rsp)
{
	struct light_hsl_ctx *lh_ctx = &my_hsl_ctx;

	rsp->current = lh_ctx->current_sat;
	rsp->remaining_time = lh_ctx->sat_rem_time;
	rsp->target = lh_ctx->target_sat;
}

static struct bt_mesh_light_ctrl_srv light_ctrl_srv = BT_MESH_LIGHT_CTRL_SRV_INIT(&my_lightness_srv);

static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(1,
		     BT_MESH_MODEL_LIST(
			     BT_MESH_MODEL_CFG_SRV,
			     BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
			     BT_MESH_MODEL_LIGHTNESS_SRV(&my_lightness_srv),
				 BT_MESH_MODEL_LIGHT_HSL_SRV(&my_hsl_srv)),
		     BT_MESH_MODEL_NONE),
	BT_MESH_ELEM(2,
		     BT_MESH_MODEL_LIST(
				 BT_MESH_MODEL_LIGHT_HUE_SRV(&my_hsl_srv.hue)),
		     BT_MESH_MODEL_NONE),
	BT_MESH_ELEM(3,
			 BT_MESH_MODEL_LIST(
				BT_MESH_MODEL_LIGHT_SAT_SRV(&my_hsl_srv.sat)),
			 BT_MESH_MODEL_NONE),
	BT_MESH_ELEM(4,
			 BT_MESH_MODEL_LIST(
			     BT_MESH_MODEL_LIGHT_CTRL_SRV(&light_ctrl_srv)),
			 BT_MESH_MODEL_NONE),
};

static const struct bt_mesh_comp comp = {
	.cid = CONFIG_BT_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

const struct bt_mesh_comp *model_handler_init(void)
{
	k_work_init_delayable(&attention_blink_work, attention_blink);
	k_work_init_delayable(&my_hsl_ctx.lvl_per_work, periodic_led_work);
	k_work_init_delayable(&my_hsl_ctx.hue_per_work, periodic_hue_work);
	k_work_init_delayable(&my_hsl_ctx.sat_per_work, periodic_sat_work);

	return &comp;
}

void model_handler_start(void)
{
	int err;

	if (bt_mesh_is_provisioned()) {
		return;
	}

	err = bt_mesh_light_ctrl_srv_enable(&light_ctrl_srv);
	if (!err) {
		printk("Successfully enabled LC server\n");
	}
}
