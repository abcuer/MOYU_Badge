#ifndef __OLED_H
#define __OLED_H

#include "stdint.h"
#include "u8g2.h"

#define OLED_ADDR 0x3C
#define OLED_SDA_PIN  16
#define OLED_SCL_PIN  15

void u8g2_init(void);

extern u8g2_t u8g2;

#endif
