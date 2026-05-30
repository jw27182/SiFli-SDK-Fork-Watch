/**
 * GH30x 心率结果 → 上层 UI：仅 BPM + 有效标志，经回调入队显示。
 */
#ifndef GH30X_HR_UI_NOTIFY_H__
#define GH30X_HR_UI_NOTIFY_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 佩戴状态定义 (对应NADT result[0]) */
#define GH30X_WEAR_STATUS_UNKNOWN   0   /* 未知/无变化 */
#define GH30X_WEAR_STATUS_WEARING   1   /* 佩戴中 */
#define GH30X_WEAR_STATUS_OFF       2   /* 未佩戴 */

typedef struct gh30x_hr_ui_sample {
    uint32_t seq;
    uint8_t hr_bpm;
    uint8_t hr_valid;
    uint8_t wear_status;   /* GH30X_WEAR_STATUS_* */
    uint8_t reserved[1];
} gh30x_hr_ui_sample_t;

void gh30x_hr_ui_notify_register(void (*cb)(const gh30x_hr_ui_sample_t *sample));

void gh30x_hr_ui_notify_hr(uint8_t bpm, int hr_valid);

void gh30x_hr_ui_notify_wear(uint8_t wear_status);

/** 重置通知状态（模组停止/重新启动时调用） */
void gh30x_hr_ui_notify_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* GH30X_HR_UI_NOTIFY_H__ */
