#ifndef __SETTINGS_H
#define __SETTINGS_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_DEFAULT_VOLUME 12

esp_err_t settings_init(void);
uint8_t settings_get_volume(void);
void settings_set_volume(uint8_t volume);
esp_err_t settings_save_volume(void);

#ifdef __cplusplus
}
#endif

#endif
