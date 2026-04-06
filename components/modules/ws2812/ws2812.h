#ifndef __WS2812_H
#define __WS2812_H

#include <stdint.h>

#define WS2812_PIN 48
#define WS2812_NUM 1

void ws2812_init(void);
void ws2812_flash(uint8_t r, uint8_t g, uint8_t b);
void ws2812_off(void);

#endif /* __WS2812_H */
