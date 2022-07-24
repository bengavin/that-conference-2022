#include <zephyr.h>
#include <device.h>
#include <logging/log.h>
#include <drivers/led_strip.h>

#include "neopixel.h"

LOG_MODULE_REGISTER(app_neopixel, LOG_LEVEL_INF);

#define NEOPIXEL_NODE	DT_ALIAS(board_neopixel)

#if DT_NODE_EXISTS(NEOPIXEL_NODE) == 1
static struct led_rgb pixel = RGB(0x00, 0x00, 0x00);
static const struct device *neopixel = DEVICE_DT_GET(NEOPIXEL_NODE);
static bool rClimbing = true, gClimbing = true, bClimbing = true;
#endif

int init_neopixel(void) {
#if DT_NODE_EXISTS(NEOPIXEL_NODE) == 1
	if (device_is_ready(neopixel)) {
		LOG_INF("Found NEOPIXEL device %s", neopixel->name);
	} else {
		LOG_ERR("NEOPIXEL device %s is not ready", neopixel->name);
		return -ENOTSUP;
	}

    set_neopixel(255, 0, 0);
    k_sleep(K_MSEC(500));
    set_neopixel(0, 255, 0);
    k_sleep(K_MSEC(500));
    set_neopixel(0, 0, 255);
    k_sleep(K_MSEC(500));
    set_neopixel(0, 0, 0);
    k_sleep(K_MSEC(500));
    for(int i = 0; i < 100; i++)
    {
        update_neopixel();
        k_sleep(K_MSEC(50));
    }
#endif

    return 0;
}

void set_neopixel(uint8_t r, uint8_t g, uint8_t b)
{
#if DT_NODE_EXISTS(NEOPIXEL_NODE) == 1
    struct led_rgb my_pixel = RGB(r, g, b);
    int rc = led_strip_update_rgb(neopixel, &my_pixel, 1);
    if (rc) {
        LOG_ERR("couldn't update neopixel: %d", rc);
    }
#endif
}

void update_neopixel(void) {
#if DT_NODE_EXISTS(NEOPIXEL_NODE) == 1
    SET_DIR(pixel.r, rClimbing, 0, 255, 10)
    SET_DIR(pixel.g, gClimbing, 0, 255, 7)
    SET_DIR(pixel.b, bClimbing, 0, 255, 2)

    uint8_t newR = SAFE_BOUNCE(pixel.r, rClimbing, 0, 255, 10),
            newG = SAFE_BOUNCE(pixel.g, gClimbing, 0, 255, 7),
            newB = SAFE_BOUNCE(pixel.b, bClimbing, 0, 255, 2);
    pixel.r = newR; 
    pixel.g = newG;
    pixel.b = newB;
    int rc = led_strip_update_rgb(neopixel, &pixel, 1);
    if (rc) {
        LOG_ERR("couldn't update neopixel: %d", rc);
    }
#endif
}

