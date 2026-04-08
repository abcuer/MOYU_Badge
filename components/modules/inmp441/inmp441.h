#ifndef __INMP441_H
#define __INMP441_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INMP441_SD_PIN  20
#define INMP441_SCK_PIN 21
#define INMP441_WS_PIN  47


typedef struct {
    int32_t raw_min;
    int32_t raw_max;
    uint32_t abs_max_shift_8;
    uint32_t abs_max_shift_10;
    uint32_t abs_max_shift_12;
    uint32_t abs_max_shift_14;
    uint32_t abs_max_shift_16;
} inmp441_diag_t;

esp_err_t inmp441_init(uint32_t sample_rate, uint8_t bits_per_sample);
void inmp441_deinit(void);
esp_err_t inmp441_read_raw(int32_t *buffer, size_t sample_count, size_t *samples_read, TickType_t timeout_ticks);
esp_err_t inmp441_read(int16_t *buffer, size_t sample_count, size_t *samples_read, TickType_t timeout_ticks);
void inmp441_get_diag(inmp441_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif
