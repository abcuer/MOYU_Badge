#ifndef __OLED_H
#define __OLED_H

#include "stdint.h"
#include "oled_font.h"

#define OLED_ADDR 0x3C
#define OLED_SDA_PIN  9
#define OLED_SCL_PIN  8

void oled_init(void);
void u8g2_init(void);
void OLED_DrawBluetoothIcon(uint8_t x, uint8_t y);
#endif
