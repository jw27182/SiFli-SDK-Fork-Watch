/**
 * GH30x 心率结果 → 上层 UI：仅 BPM + 有效标志，经回调入队显示。
 */
#ifndef GH30X_HR_UI_NOTIFY_H__
#define GH30X_HR_UI_NOTIFY_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gh30x_hr_ui_sample {
    uint32_t seq;
    uint8_t hr_bpm;
    uint8_t hr_valid;
    uint8_t reserved[2];
} gh30x_hr_ui_sample_t;

void gh30x_hr_ui_notify_register(void (*cb)(const gh30x_hr_ui_sample_t *sample));

void gh30x_hr_ui_notify_hr(uint8_t bpm, int hr_valid);

#ifdef __cplusplus
}
#endif

#endif /* GH30X_HR_UI_NOTIFY_H__ */
