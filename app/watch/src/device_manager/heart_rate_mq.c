#include "heart_rate_mq.h"

#ifdef HR_USING_GH3018

#ifndef HR_UI_MQ_TRACE
#define HR_UI_MQ_TRACE 0
#endif

static rt_mq_t s_hr_mq;

static void hr_mq_bridge(const gh30x_hr_ui_sample_t *sample)
{
    gh30x_hr_ui_sample_t copy;
    if (s_hr_mq == RT_NULL || sample == RT_NULL) {
#if 1 /* [DBG_MQ] */
        rt_kprintf("[DBG_MQ] bridge SKIP: mq=%p sample=%p\r\n",
                   (void*)s_hr_mq, (void*)sample);
#endif
        return;
    }
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
    rt_err_t err = rt_mq_send(s_hr_mq, &copy, sizeof(copy));
    if (err != RT_EOK) {
#if 1 /* [DBG_MQ] */
        rt_kprintf("[DBG_MQ] bridge FAIL: mq_send err=%d\r\n", (int)err);
#endif
    }
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

void heart_rate_mq_flush(void)
{
#ifdef HR_USING_GH3018
    if (s_hr_mq == RT_NULL) return;
    gh30x_hr_ui_sample_t dummy;
    rt_uint32_t flushed = 0;
    while (rt_mq_recv(s_hr_mq, &dummy, sizeof(dummy), 0) == RT_EOK) {
        flushed++;
    }
    if (flushed > 0) {
#if 1 /* [DBG_MQ] */
        rt_kprintf("[DBG_MQ] flush: %lu stale messages discarded\r\n",
                   (unsigned long)flushed);
#endif
    }
#endif
}

INIT_APP_EXPORT(heart_rate_mq_init);
