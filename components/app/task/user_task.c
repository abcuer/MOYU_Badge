#include "headfile.h"

volatile bool is_first_sync_done = false;
volatile bool has_started_ap_config = false;

static volatile bool s_sensor_power_sync_required = false;

#define OLED_FAST_REFRESH_MS    30
#define OLED_MEDIUM_REFRESH_MS  100
#define OLED_SLOW_REFRESH_MS    200
#define OLED_CLOCK_REFRESH_MS   1000

static bool sensor_mode_needs_imu(void)
{
    return mode == MODE_BALL ||
           mode == MODE_DINO ||
           mode == MODE_PLANE ||
           (mode == MODE_SETTING && setting_ui_is_volume_editing()) ||
           (mode == MODE_RADIO && radio_ui_is_volume_editing());
}

static bool sensor_mode_needs_bmp280(void)
{
    return mode == MODE_CLOCK;
}

static bool sensor_mode_needs_max30102(void)
{
    return mode == MODE_BLOOD;
}

static uint32_t oled_get_refresh_interval_ms(void)
{
    if (in_select) {
        return OLED_SLOW_REFRESH_MS;
    }

    switch (mode)
    {
        case MODE_CLOCK:
            return OLED_CLOCK_REFRESH_MS;

        case MODE_BLOOD:
        case MODE_BALL:
        case MODE_DINO:
        case MODE_PLANE:
            return OLED_FAST_REFRESH_MS;

        case MODE_RECORDER:
            return (recorder_get_state() == RECORDER_STATE_IDLE)
                       ? OLED_SLOW_REFRESH_MS
                       : OLED_FAST_REFRESH_MS;

        case MODE_RADIO:
            return radio_ui_is_volume_editing()
                       ? OLED_FAST_REFRESH_MS
                       : OLED_MEDIUM_REFRESH_MS;

        case MODE_SETTING:
            if (setting_ui_is_volume_editing()) {
                return OLED_FAST_REFRESH_MS;
            }
            return setting_ui_is_info_page()
                       ? OLED_SLOW_REFRESH_MS
                       : OLED_MEDIUM_REFRESH_MS;

        case MODE_GAME_SELECT:
            return OLED_SLOW_REFRESH_MS;

        default:
            return OLED_MEDIUM_REFRESH_MS;
    }
}

void sensor_request_power_sync(void)
{
    s_sensor_power_sync_required = true;
}

void start_sync_task(void *pvParameters)
{
    xEventGroupWaitBits(wifi_ev, WIFI_CONNECT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(2000));

    fetch_time();
    fetch_weather();

    xEventGroupSetBits(wifi_ev, TIME_SYNC_BIT);
    is_first_sync_done = true;

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(30 * 60 * 1000));
        fetch_weather();
    }
}

void start_sensor_task(void *pvParameters)
{
    bool imu_active = false;
    bool bmp280_active = false;
    bool max30102_active = false;

    mpu6050_init();
    bmp280_init();
    max30102_init();

    mpu6050_sleep(true);
    bmp280_sleep(true);
    max30102_sleep(true);

    xEventGroupWaitBits(wifi_ev, TIME_SYNC_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    while (1)
    {
        bool need_imu;
        bool need_bmp280;
        bool need_max30102;

        if (s_sensor_power_sync_required) {
            imu_active = false;
            bmp280_active = false;
            max30102_active = false;
            s_sensor_power_sync_required = false;
        }

        need_imu = sensor_mode_needs_imu();
        need_bmp280 = sensor_mode_needs_bmp280();
        need_max30102 = sensor_mode_needs_max30102();

        if (need_imu != imu_active) {
            mpu6050_sleep(!need_imu);
            imu_active = need_imu;
        }

        if (!need_bmp280 && bmp280_active) {
            bmp280_sleep(true);
            bmp280_active = false;
        } else if (need_bmp280 && !bmp280_active) {
            bmp280_active = true;
        }

        if (need_max30102 != max30102_active) {
            max30102_sleep(!need_max30102);
            max30102_active = need_max30102;
            blood_reset();
        }

        if (need_imu) {
            imu_get_angle(&gyroAccel, &euler_angle, 20.0f / 1000.0f);
            if (mode == MODE_SETTING) {
                setting_ui_update_volume_tilt(euler_angle.roll);
            } else if (mode == MODE_RADIO) {
                radio_ui_update_volume_tilt(euler_angle.roll);
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        } else if (need_bmp280) {
            bmp280_read_data(&bmp280);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else if (need_max30102) {
            blood_detect();
            vTaskDelay(pdMS_TO_TICKS(20));
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

void start_onenet_task(void *pvParameters)
{
    xEventGroupWaitBits(wifi_ev, TIME_SYNC_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    onenet_start();

    while (1)
    {
        cJSON *prop_json = onenet_property_upload_dm();

        if (prop_json != NULL)
        {
            char *post_data = cJSON_PrintUnformatted(prop_json);

            onenet_post_property_data(post_data);

            cJSON_free(post_data);
            cJSON_Delete(prop_json);
        }

        vTaskDelay(pdMS_TO_TICKS(onenet_PERIOD));
    }
}

void start_key_task(void *pvParameters)
{
    key_device_init();

    while (1)
    {
        key_scan();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void start_oled_task(void *pvParameters)
{
    uint32_t refresh_interval_ms;

    u8g2_init();
    reset_sync_ui_timer();

    while (!is_first_sync_done)
    {
        has_started_ap_config = ap_wifi_is_config_mode_active();
        draw_syncing_ui(&u8g2);
        vTaskDelay(pdMS_TO_TICKS(OLED_PERIOD));
    }

    last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while (1)
    {
        if (mode_try_enter_light_sleep()) {
            continue;
        }

        if (in_select)
        {
            draw_select_ui(&u8g2, selected_game);
        }
        else
        {
            switch (mode)
            {
                case MODE_CLOCK:    draw_main_clock_ui(&u8g2); break;
                case MODE_RECORDER: draw_recorder_ui(&u8g2);   break;
                case MODE_BLOOD:    draw_blood_ui(&u8g2);      break;
                case MODE_BALL:     draw_ball_game(&u8g2);     break;
                case MODE_DINO:     draw_dino_game(&u8g2);     break;
                case MODE_PLANE:    draw_plane_game(&u8g2);    break;
                case MODE_RADIO:    draw_radio_ui(&u8g2);      break;
                case MODE_SETTING:  draw_setting_ui(&u8g2);    break;
                default: break;
            }
        }

        refresh_interval_ms = oled_get_refresh_interval_ms();
        vTaskDelay(pdMS_TO_TICKS(refresh_interval_ms));
    }
}
