#ifndef __OLED_H
#define __OLED_H

#include "stdint.h"
#include "oled_font.h"
#include "u8g2.h"

#define OLED_ADDR 0x3C
#define OLED_SDA_PIN  5
#define OLED_SCL_PIN  4

void oled_init(void);
void u8g2_init(void);
void OLED_DrawBluetoothIcon(uint8_t x, uint8_t y);

extern u8g2_t u8g2;

#endif
