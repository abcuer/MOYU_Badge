#ifndef __MAX98357_H
#define __MAX98357_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX98357_LRC_PIN  42
#define MAX98357_BCLK_PIN 41
#define MAX98357_DIN_PIN  40

esp_err_t max98357_init(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);
void max98357_deinit(void);
esp_err_t max98357_write(const uint8_t *pcm_data, size_t pcm_len, uint8_t channels, TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif

#endif
