#ifndef __APP_NEOPIXEL_H
#define __APP_NEOPIXEL_H

#ifdef __cplusplus
extern "C" {
#endif

#define RGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b) }
#define SET_DIR(_input, _dir, _min, _max, _incr) if ((_dir && _input > (_max - _incr)) || (!_dir && _input < (_incr - _min))) { _dir = !_dir; }
#define SAFE_BOUNCE(_input, _dir, _min, _max, _incr) MIN(MAX(_min, (_input + (_dir ? _incr : (-1 * _incr)))), _max)

int init_neopixel(void);
void update_neopixel(void);
void set_neopixel(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif
