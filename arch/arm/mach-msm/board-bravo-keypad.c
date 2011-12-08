/* arch/arm/mach-msm/board-bravo-keypad.c
 *
 * Copyright (C) 2009 Google, Inc
 * Copyright (C) 2009 HTC Corporation.
 *
 * Author: Dima Zavin <dima@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/gpio_event.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/keyreset.h>
#include <linux/platform_device.h>
#include <mach/vreg.h>

#include <asm/mach-types.h>

#include "board-bravo.h"

struct jog_axis_info {
	struct gpio_event_axis_info	info;
	uint16_t			in_state;
	uint16_t			out_state;
};

static struct vreg *jog_vreg;
static bool jog_just_on;
static unsigned long jog_on_jiffies;

static unsigned int bravo_col_gpios[] = { 33, 32, 31 };
static unsigned int bravo_row_gpios[] = { 42, 41, 40 };

#define KEYMAP_INDEX(col, row)	((col)*ARRAY_SIZE(bravo_row_gpios) + (row))
#define KEYMAP_SIZE		(ARRAY_SIZE(bravo_col_gpios) * \
				 ARRAY_SIZE(bravo_row_gpios))

/* keypad */
static const unsigned short bravo_keymap[KEYMAP_SIZE] = {
	[KEYMAP_INDEX(0, 0)] = KEY_VOLUMEUP,
	[KEYMAP_INDEX(0, 1)] = KEY_VOLUMEDOWN,
	[KEYMAP_INDEX(1, 1)] = MATRIX_KEY(1, BTN_MOUSE),
};

static const unsigned short bravo_cdma_keymap[KEYMAP_SIZE] = {
	[KEYMAP_INDEX(0, 0)] = KEY_VOLUMEUP,
	[KEYMAP_INDEX(0, 1)] = KEY_VOLUMEDOWN,
	[KEYMAP_INDEX(1, 1)] = MATRIX_KEY(1, BTN_MOUSE),

	/* Key (2, 2) is not a physical key on bravo. The purpose of
	 * registering the unused matrix key as a dummy <end> key is to make
	 * userland able to send/receive the key event for some requested tests
	 * in lab. of some CDMA carriers (e.g. Verizon).
	 */
	[KEYMAP_INDEX(2, 2)] = KEY_END,
};

static struct gpio_event_matrix_info bravo_keypad_matrix_info = {
	.info.func = gpio_event_matrix_func,
	.keymap = bravo_keymap,
	.output_gpios = bravo_col_gpios,
	.input_gpios = bravo_row_gpios,
	.noutputs = ARRAY_SIZE(bravo_col_gpios),
	.ninputs = ARRAY_SIZE(bravo_row_gpios),
	.settle_time.tv.nsec = 40 * NSEC_PER_USEC,
	.poll_time.tv.nsec = 20 * NSEC_PER_MSEC,
	.flags = (GPIOKPF_LEVEL_TRIGGERED_IRQ |
		  GPIOKPF_REMOVE_PHANTOM_KEYS |
		  GPIOKPF_PRINT_UNMAPPED_KEYS),
};

static struct gpio_event_direct_entry bravo_keypad_key_map[] = {
	{
		.gpio	= BRAVO_GPIO_POWER_KEY,
		.code	= KEY_POWER,
	},
};

static struct gpio_event_input_info bravo_keypad_key_info = {
	.info.func = gpio_event_input_func,
	.info.no_suspend = true,
	.debounce_time.tv.nsec = 5 * NSEC_PER_MSEC,
	.flags = 0,
	.type = EV_KEY,
	.keymap = bravo_keypad_key_map,
	.keymap_size = ARRAY_SIZE(bravo_keypad_key_map)
};

/* jogball */
static uint16_t jogball_axis_map(struct gpio_event_axis_info *info, uint16_t in)
{
	struct jog_axis_info *ai =
		container_of(info, struct jog_axis_info, info);
	uint16_t out = ai->out_state;

	if (jog_just_on) {
		if (jiffies == jog_on_jiffies || jiffies == jog_on_jiffies + 1)
			goto ignore;
		jog_just_on = 0;
	}
	if((ai->in_state ^ in) & 1)
		out--;
	if((ai->in_state ^ in) & 2)
		out++;
	ai->out_state = out;
ignore:
	ai->in_state = in;
	return out;
}

static int jogball_power(const struct gpio_event_platform_data *pdata, bool on)
{
	if (on) {
		vreg_enable(jog_vreg);
		jog_just_on = 1;
		jog_on_jiffies = jiffies;
	} else {
		vreg_disable(jog_vreg);
	}

	return 0;
}

static int jogball_power_cdma(const struct gpio_event_platform_data *pdata, bool on)
{
	if (on) {
		gpio_set_value(BRAVO_CDMA_JOG_2V6_EN, 1);
		jog_just_on = 1;
		jog_on_jiffies = jiffies;
	} else {
		gpio_set_value(BRAVO_CDMA_JOG_2V6_EN, 0);
	}

	return 0;
}

static uint32_t jogball_x_gpios[] = {
	BRAVO_GPIO_BALL_LEFT, BRAVO_GPIO_BALL_RIGHT,
};
static uint32_t jogball_y_gpios[] = {
	BRAVO_GPIO_BALL_UP, BRAVO_GPIO_BALL_DOWN,
};

static struct jog_axis_info jogball_x_axis = {
	.info = {
		.info.func = gpio_event_axis_func,
		.count = ARRAY_SIZE(jogball_x_gpios),
		.dev = 1,
		.type = EV_REL,
		.code = REL_X,
		.decoded_size = 1U << ARRAY_SIZE(jogball_x_gpios),
		.map = jogball_axis_map,
		.gpio = jogball_x_gpios,
		.flags = GPIOEAF_PRINT_UNKNOWN_DIRECTION,
	}
};

static struct jog_axis_info jogball_y_axis = {
	.info = {
		.info.func = gpio_event_axis_func,
		.count = ARRAY_SIZE(jogball_y_gpios),
		.dev = 1,
		.type = EV_REL,
		.code = REL_Y,
		.decoded_size = 1U << ARRAY_SIZE(jogball_y_gpios),
		.map = jogball_axis_map,
		.gpio = jogball_y_gpios,
		.flags = GPIOEAF_PRINT_UNKNOWN_DIRECTION,
	}
};

static struct gpio_event_info *bravo_input_info[] = {
	&bravo_keypad_matrix_info.info,
	&bravo_keypad_key_info.info,
	&jogball_x_axis.info.info,
	&jogball_y_axis.info.info,
};

static struct gpio_event_platform_data bravo_input_data = {
	.names = {
		"bravo-keypad",
		"bravo-nav",
		NULL,
	},
	.info = bravo_input_info,
	.info_count = ARRAY_SIZE(bravo_input_info),
	.power = jogball_power,
};

static struct platform_device bravo_input_device = {
	.name = GPIO_EVENT_DEV_NAME,
	.id = 0,
	.dev = {
		.platform_data = &bravo_input_data,
	},
};

static int bravo_reset_keys_up[] = {
	KEY_VOLUMEUP,
	0,
};

static struct keyreset_platform_data bravo_reset_keys_pdata = {
	.keys_up	= bravo_reset_keys_up,
	.keys_down	= {
		KEY_POWER,
		KEY_VOLUMEDOWN,
		BTN_MOUSE,
		0
	},
};

struct platform_device bravo_reset_keys_device = {
	.name	= KEYRESET_NAME,
	.dev	= {
		.platform_data = &bravo_reset_keys_pdata,
	},
};


static int __init bravo_init_keypad_jogball(void)
{
	int ret;

	if (!machine_is_bravo())
		return 0;

	ret = platform_device_register(&bravo_reset_keys_device);
	if (ret != 0)
		return ret;

	if (is_cdma_version(system_rev)) {
		bravo_keypad_matrix_info.keymap = bravo_cdma_keymap;
		/* In the CDMA version, jogball power is supplied by a gpio. */
		ret = gpio_request(BRAVO_CDMA_JOG_2V6_EN, "jog_en");
		if (ret < 0) {
			pr_err("%s: gpio_request(%d) failed: %d\n", __func__,
				BRAVO_CDMA_JOG_2V6_EN, ret);
			return ret;
		}
		bravo_input_data.power = jogball_power_cdma;
	} else {
		/* in UMTS version, jogball power is supplied by pmic */
		jog_vreg = vreg_get(&bravo_input_device.dev, "gp2");
		if (jog_vreg == NULL)
			return -ENOENT;
	}

	ret = platform_device_register(&bravo_input_device);
	if (ret != 0)
		return ret;

	return 0;
}

device_initcall(bravo_init_keypad_jogball);
