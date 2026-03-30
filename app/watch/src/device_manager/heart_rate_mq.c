#include "heart_rate_mq.h"

#ifdef HR_USING_GH3018

#ifndef HR_UI_MQ_TRACE
#define HR_UI_MQ_TRACE 0
#endif

static rt_mq_t s_hr_mq;

static void hr_mq_bridge(const gh30x_hr_ui_sample_t *sample)
{
    gh30x_hr_ui_sample_t copy;
    if (s_hr_mq == RT_NULL || sample == RT_NULL) return;
    copy = *sample;
#if HR_UI_MQ_TRACE
    {
        static rt_uint32_t s_mq_tr;
        s_mq_tr++;
        if ((s_mq_tr % 50u) == 0u)
        {
            rt_kprintf("[HR_UI_mq] seq=%lu bpm=%u v=%u\r\n",
                       (unsigned long)copy.seq, (unsigned)copy.hr_bpm,
                       (unsigned)copy.hr_valid);
        }
    }
#endif
    (void)rt_mq_send(s_hr_mq, &copy, sizeof(copy));
}

#endif

int heart_rate_mq_init(void)
{
#ifdef HR_USING_GH3018
    if (s_hr_mq != RT_NULL) return RT_EOK;

    s_hr_mq = rt_mq_create("hr_ui", sizeof(gh30x_hr_ui_sample_t), 16, RT_IPC_FLAG_FIFO);
    if (s_hr_mq == RT_NULL) return -RT_ENOMEM;

    gh30x_hr_ui_notify_register(hr_mq_bridge);
#endif
    return RT_EOK;
}

rt_mq_t heart_rate_mq_get(void)
{
#ifdef HR_USING_GH3018
    return s_hr_mq;
#else
    return RT_NULL;
#endif
}

INIT_APP_EXPORT(heart_rate_mq_init);
