#include <zephyr.h>
#include <device.h>
#include <logging/log.h>
#include <drivers/led_strip.h>
#include <drivers/spi.h>

#include "led_strip.h"
#include "neopixel.h"

LOG_MODULE_REGISTER(app_led_strip, LOG_LEVEL_DBG);

#define STRIP_NODE		DT_ALIAS(led_strip)
#define STRIP_NUM_PIXELS	DT_PROP(STRIP_NODE, chain_length)

static const struct led_rgb colors[] = {
	RGB(0x0f, 0x00, 0x00), /* red */
	RGB(0x00, 0x0f, 0x00), /* green */
	RGB(0x00, 0x00, 0x0f), /* blue */
};

struct led_rgb pixels[STRIP_NUM_PIXELS];
static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);

static bool strip_enabled = false;
static size_t cursor = 0, color = 0;

int init_led_strip(void) {
	if (device_is_ready(strip)) {
		LOG_INF("Found LED strip device %s", strip->name);
		strip_enabled = true;
	} else {
		LOG_ERR("LED strip device %s is not ready", strip->name);
		return -ENOTSUP;
	}

    // Exercise the LEDs a bit to give user feedback
    set_led_strip(UINT16_MAX, 0, 0);
    k_sleep(K_MSEC(250));
    set_led_strip(0, UINT16_MAX, 0);
    k_sleep(K_MSEC(250));
    set_led_strip(0, 0, UINT16_MAX);
    k_sleep(K_MSEC(250));
    set_led_strip(0, 0, 0);
    k_sleep(K_MSEC(250));
    for(int i = 0; i < 2*STRIP_NUM_PIXELS; i++) {
        update_led_strip();
        k_sleep(K_MSEC(100));
    }
    set_led_strip(0, 0, 0);

    return 0;    

}

void set_led_strip(uint16_t red, uint16_t green, uint16_t blue) {
    if (!strip_enabled) { return; }

    struct led_rgb color = RGB(SCALE_16_TO_8(red), SCALE_16_TO_8(green), SCALE_16_TO_8(blue));

    LOG_INF("Setting LED Strip to color: (%d, %d, %d)", color.r, color.g, color.b);

    memset(&pixels, 0x00, sizeof(pixels));
    for(int i = 0; i < STRIP_NUM_PIXELS; i++) 
    { 
        memcpy(&pixels[i], &color, sizeof(struct led_rgb));
    }

    int rc = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
    if (rc) {
        LOG_ERR("couldn't update strip: %d", rc);
    }
}

void update_led_strip(void) {
    if (!strip_enabled) { return; }

	if (cursor == 0) {
        LOG_INF("Displaying pattern on strip");
    }

    memset(&pixels, 0x00, sizeof(pixels));
    memcpy(&pixels[cursor], &colors[color], sizeof(struct led_rgb));
    int rc = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);

    if (rc) {
        LOG_ERR("couldn't update strip: %d", rc);
    }

    cursor++;
    if (cursor >= STRIP_NUM_PIXELS) {
        cursor = 0;
        color++;
        if (color == ARRAY_SIZE(colors)) {
            color = 0;
        }
    }
}
