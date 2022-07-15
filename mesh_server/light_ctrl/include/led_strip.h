#ifndef __APP_LED_STRIP_H
#define __APP_LED_STRIP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SCALE_16_TO_8(_val) ((uint8_t)((_val / (float)UINT16_MAX) * UINT8_MAX))

int init_led_strip(void);
void update_led_strip(void);
void set_led_strip(uint16_t red, uint16_t green, uint16_t blue);

#ifdef __cplusplus
}
#endif

#endif
