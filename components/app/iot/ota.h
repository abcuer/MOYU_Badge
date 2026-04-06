#ifndef _APP_OTA_H_
#define _APP_OTA_H_

#include <stddef.h>

#include "esp_err.h"

void ota_mark_app_valid_if_needed(void);
esp_err_t ota_start_from_url(const char *url, const char *version);
esp_err_t ota_start_from_onenet_payload(const char *payload, size_t payload_len);

#endif
