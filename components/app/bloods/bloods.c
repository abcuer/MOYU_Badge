#include "headfile.h"

BloodData_t b_data = {0};
BloodTaskState_t b_state = BLOOD_IDLE;

// ── 滑动平均缓冲 ─────────────────────────
static uint32_t ir_buf[SMOOTH_SIZE]  = {0};
static uint32_t red_buf[SMOOTH_SIZE] = {0};
static int      buf_idx = 0;
static bool     buf_full = false;

// ── 峰谷检测状态 ─────────────────────────
static uint32_t ir_smooth_prev  = 0;
static uint32_t ir_smooth_prev2 = 0;

static uint32_t peak_val  = 0;   // 上一个峰值
static uint32_t valley_val = 0;  // 上一个谷值
static uint32_t peak_tick  = 0;  // 上一个峰的时间(tick)
static bool     rising    = false;

// 心率历史（滑动平均，减少抖动）
#define HR_BUF_SIZE  5
static int hr_history[HR_BUF_SIZE] = {0};
static int hr_idx = 0;

// DC分量估算（指数滑动平均）
static float dc_ir  = 0;
static float dc_red = 0;
#define DC_ALPHA  0.98f   // 时间常数，越大DC跟踪越慢

void blood_reset(void)
{
    memset(ir_buf,  0, sizeof(ir_buf));
    memset(red_buf, 0, sizeof(red_buf));
    memset(hr_history, 0, sizeof(hr_history));
    buf_idx = 0; buf_full = false;
    ir_smooth_prev = ir_smooth_prev2 = 0;
    peak_val = valley_val = peak_tick = 0;
    rising = false;
    dc_ir = dc_red = 0;
    b_data.heart = 0;
    b_data.SpO2  = 0;
    b_data.valid = false;
    hr_idx = 0;
}

// 滑动平均滤波
static uint32_t smooth(uint32_t *buf, uint32_t new_val)
{
    buf[buf_idx % SMOOTH_SIZE] = new_val;
    uint64_t sum = 0;
    int count = buf_full ? SMOOTH_SIZE : (buf_idx + 1);
    for (int i = 0; i < count; i++) sum += buf[i];
    return (uint32_t)(sum / count);
}

void blood_sample_once(void)
{
    max30102_read_fifo();
    // 1. 滑动平均滤波
    uint32_t ir_s  = smooth(ir_buf,  fifo_ir);
    uint32_t red_s = smooth(red_buf, fifo_red);
    buf_idx++;
    if (buf_idx >= SMOOTH_SIZE) buf_full = true;

    // 2. 更新DC分量（指数滑动平均）
    if (dc_ir == 0) {
        dc_ir  = (float)ir_s;
        dc_red = (float)red_s;
    } else {
        dc_ir  = DC_ALPHA * dc_ir  + (1.0f - DC_ALPHA) * ir_s;
        dc_red = DC_ALPHA * dc_red + (1.0f - DC_ALPHA) * red_s;
    }

    // 3. 峰值检测（三点比较：prev2 < prev > current = 峰）
    if (buf_full && ir_smooth_prev2 > 0)
    {
        bool is_peak   = (ir_smooth_prev > ir_smooth_prev2) &&
                         (ir_smooth_prev > ir_s);
        bool is_valley = (ir_smooth_prev < ir_smooth_prev2) &&
                         (ir_smooth_prev < ir_s);

        uint32_t now_tick = xTaskGetTickCount();

        if (is_peak && rising)
        {
            uint32_t amplitude = ir_smooth_prev - valley_val;

            // 幅度足够大才认为是有效峰
            if (amplitude > PEAK_MIN_HEIGHT && peak_tick > 0)
            {
                uint32_t interval_ms = (now_tick - peak_tick) * portTICK_PERIOD_MS;

                // 心率合理范围：40~180 bpm → 间隔 333ms~1500ms
                if (interval_ms > 333 && interval_ms < 1500)
                {
                    int hr_new = (int)(60000 / interval_ms);

                    // 存入心率历史缓冲
                    hr_history[hr_idx % HR_BUF_SIZE] = hr_new;
                    hr_idx++;

                    // 计算平均心率
                    int sum = 0, cnt = 0;
                    for (int i = 0; i < HR_BUF_SIZE; i++) {
                        if (hr_history[i] > 0) { sum += hr_history[i]; cnt++; }
                    }
                    if (cnt >= 2) {  // 至少2个峰才输出
                        b_data.heart = sum / cnt;
                    }

                    // 5. 计算血氧（用AC峰谷差 / DC）
                    float ac_ir  = (float)(peak_val - valley_val);
                    float ac_red = ac_ir * 0.8f;  // 简化：红光AC约为红外的0.8倍
                                                   // 精确做法：同步记录红光峰谷

                    if (dc_ir > 0 && dc_red > 0 && ac_ir > 0) {
                        float R = (ac_red / dc_red) / (ac_ir / dc_ir);
                        float spo2 = 110.0f - 25.0f * R;
                        if (spo2 > 100.0f) spo2 = 100.0f;
                        if (spo2 < 80.0f)  spo2 = 0.0f;
                        b_data.SpO2 = spo2;
                    }

                    b_data.valid = (cnt >= 2);
                }
            }
            peak_val  = ir_smooth_prev;
            peak_tick = now_tick;
            rising    = false;
        }

        if (is_valley)
        {
            valley_val = ir_smooth_prev;
            rising     = true;
        }
    }

    ir_smooth_prev2 = ir_smooth_prev;
    ir_smooth_prev  = ir_s;
}

void blood_detect(void)
{
    // 检查手指
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