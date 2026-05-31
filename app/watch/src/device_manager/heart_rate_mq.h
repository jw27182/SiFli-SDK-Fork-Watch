#ifndef HEART_RATE_MQ_H__
#define HEART_RATE_MQ_H__

#include <rtthread.h>

#ifdef HR_USING_GH3018
#include "gh30x_hr_ui_notify.h"
#endif

int heart_rate_mq_init(void);

/* 心率页从该队列非阻塞取 gh30x_hr_ui_sample_t；未启用 GH3018 时返回 RT_NULL */
rt_mq_t heart_rate_mq_get(void);

/** 清空消息队列中的旧消息（HR页面重新进入时调用） */
void heart_rate_mq_flush(void);

/* ========== PPG 原始波形消息队列（ECG 波形图数据源）========== */

/* PPG 原始波形采样点 */
typedef struct {
    int32_t ppg_value;   /* 17-bit PPG ADC 值，范围 0 ~ 0x1FFFF */
} ppg_sample_t;

/* PPG 消息队列对外接口 */
rt_mq_t ppg_mq_get(void);
void    ppg_mq_flush(void);
void    ppg_mq_send(int32_t ppg_value);

#endif /* HEART_RATE_MQ_H__ */
