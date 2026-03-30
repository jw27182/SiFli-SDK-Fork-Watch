#ifndef HEART_RATE_MQ_H__
#define HEART_RATE_MQ_H__

#include <rtthread.h>

#ifdef HR_USING_GH3018
#include "gh30x_hr_ui_notify.h"
#endif

int heart_rate_mq_init(void);

/* 心率页从该队列非阻塞取 gh30x_hr_ui_sample_t；未启用 GH3018 时返回 RT_NULL */
rt_mq_t heart_rate_mq_get(void);

#endif /* HEART_RATE_MQ_H__ */
