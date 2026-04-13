#include "headfile.h"

BloodData_t b_data = {0};
BloodTaskState_t b_state = BLOOD_IDLE;

static uint32_t s_ir_buf[SMOOTH_SIZE] = {0};
static uint32_t s_red_buf[SMOOTH_SIZE] = {0};
static int s_buf_index = 0;
static bool s_buf_full = false;

static uint32_t s_ir_smooth_prev = 0;
static uint32_t s_ir_smooth_prev2 = 0;

static uint32_t s_peak_value = 0;
static uint32_t s_valley_value = 0;
static uint32_t s_peak_tick = 0;
static bool s_rising = false;
static int s_hr_history[HR_BUF_SIZE] = {0};
static int s_hr_index = 0;

static float s_dc_ir = 0;
static float s_dc_red = 0;

void blood_reset(void)
{
    memset(s_ir_buf, 0, sizeof(s_ir_buf));
    memset(s_red_buf, 0, sizeof(s_red_buf));
    memset(s_hr_history, 0, sizeof(s_hr_history));

    reset_blood_ui_timer();

    s_buf_index = 0;
    s_buf_full = false;
    s_ir_smooth_prev = 0;
    s_ir_smooth_prev2 = 0;
    s_peak_value = 0;
    s_valley_value = 0;
    s_peak_tick = 0;
    s_rising = false;
    s_dc_ir = 0;
    s_dc_red = 0;
    b_data.heart = 0;
    b_data.SpO2 = 0;
    b_data.valid = false;
    s_hr_index = 0;
}

static uint32_t bloods_smooth(uint32_t *buf, uint32_t new_val)
{
    buf[s_buf_index % SMOOTH_SIZE] = new_val;
    uint64_t sum = 0;
    int count = s_buf_full ? SMOOTH_SIZE : (s_buf_index + 1);
    for (int i = 0; i < count; i++) {
        sum += buf[i];
    }
    return (uint32_t)(sum / count);
}

void blood_sample_once(void)
{
    max30102_read_fifo();

    uint32_t ir_s = bloods_smooth(s_ir_buf, fifo_ir);
    uint32_t red_s = bloods_smooth(s_red_buf, fifo_red);
    s_buf_index++;
    if (s_buf_index >= SMOOTH_SIZE) {
        s_buf_full = true;
    }

    if (s_dc_ir == 0) {
        s_dc_ir = (float)ir_s;
        s_dc_red = (float)red_s;
    } else {
        s_dc_ir = DC_ALPHA * s_dc_ir + (1.0f - DC_ALPHA) * ir_s;
        s_dc_red = DC_ALPHA * s_dc_red + (1.0f - DC_ALPHA) * red_s;
    }

    if (s_buf_full && s_ir_smooth_prev2 > 0) {
        bool is_peak = (s_ir_smooth_prev > s_ir_smooth_prev2) && (s_ir_smooth_prev > ir_s);
        bool is_valley = (s_ir_smooth_prev < s_ir_smooth_prev2) && (s_ir_smooth_prev < ir_s);
        uint32_t now_tick = xTaskGetTickCount();

        if (is_peak && s_rising) {
            uint32_t amplitude = s_ir_smooth_prev - s_valley_value;

            if (amplitude > PEAK_MIN_HEIGHT && s_peak_tick > 0) {
                uint32_t interval_ms = (now_tick - s_peak_tick) * portTICK_PERIOD_MS;

                if (interval_ms > 350 && interval_ms < 1500) {
                    int hr_new = (int)(60000 / interval_ms);
                    s_hr_history[s_hr_index % HR_BUF_SIZE] = hr_new;
                    s_hr_index++;

                    int hr_sum = 0;
                    int hr_cnt = 0;
                    for (int i = 0; i < HR_BUF_SIZE; i++) {
                        if (s_hr_history[i] > 0) {
                            hr_sum += s_hr_history[i];
                            hr_cnt++;
                        }
                    }
                    if (hr_cnt >= 2) {
                        b_data.heart = hr_sum / hr_cnt;
                    }

                    float ac_ir = (float)(s_peak_value - s_valley_value);
                    float ac_red = ac_ir * 0.8f;

                    if (s_dc_ir > 0 && s_dc_red > 0 && ac_ir > 0) {
                        float R = (ac_red / s_dc_red) / (ac_ir / s_dc_ir);
                        float spo2 = 110.0f - 25.0f * R;
                        if (spo2 > 100.0f) {
                            spo2 = 100.0f;
                        }
                        if (spo2 < 80.0f) {
                            spo2 = 0.0f;
                        }
                        b_data.SpO2 = spo2;
                    }

                    b_data.valid = (hr_cnt >= 2);
                }
            }

            s_peak_value = s_ir_smooth_prev;
            s_peak_tick = now_tick;
            s_rising = false;
        }

        if (is_valley) {
            s_valley_value = s_ir_smooth_prev;
            s_rising = true;
        }
    }

    s_ir_smooth_prev2 = s_ir_smooth_prev;
    s_ir_smooth_prev = ir_s;
}

void blood_detect(void)
{
    max30102_read_fifo();
    if (fifo_ir < 10000) {
        b_state = BLOOD_IDLE;
        blood_reset();
        return;
    }

    b_state = BLOOD_SAMPLING;
    blood_sample_once();

    if (b_data.valid) {
        b_state = BLOOD_DONE;
    }
}
