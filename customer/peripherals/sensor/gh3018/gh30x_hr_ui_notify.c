#include "gh30x_hr_ui_notify.h"
#include <string.h>
#include <rtthread.h>

static void (*s_cb)(const gh30x_hr_ui_sample_t *sample);
static gh30x_hr_ui_sample_t s_last;

static void emit(void)
{
#if 0 /* [DBG_NOTIFY] */
    rt_kprintf("[DBG_NOTIFY] emit: seq=%lu bpm=%u valid=%u wear=%u cb=%p\r\n",
               (unsigned long)s_last.seq, (unsigned)s_last.hr_bpm,
               (unsigned)s_last.hr_valid, (unsigned)s_last.wear_status, (void*)s_cb);
#endif
    if (s_cb == 0) {
#if 0 /* [DBG_NOTIFY] */
        rt_kprintf("[DBG_NOTIFY] emit SKIP: no callback registered!\r\n");
#endif
        return;
    }
    s_last.seq++;
    s_cb(&s_last);
#if 0 /* [DBG_NOTIFY] */
    rt_kprintf("[DBG_NOTIFY] emit DONE: seq=%lu\r\n", (unsigned long)s_last.seq);
#endif
}

void gh30x_hr_ui_notify_register(void (*cb)(const gh30x_hr_ui_sample_t *sample))
{
#if 0 /* [DBG_NOTIFY] */
    rt_kprintf("[DBG_NOTIFY] register cb=%p\r\n", (void*)cb);
#endif
    s_cb = cb;
    memset(&s_last, 0, sizeof(s_last));
}

void gh30x_hr_ui_notify_reset(void)
{
#if 0 /* [DBG_NOTIFY] */
    rt_kprintf("[DBG_NOTIFY] reset (seq=%lu)\r\n", (unsigned long)s_last.seq);
#endif
    /* 保留回调注册，只清零状态 */
    memset(&s_last, 0, sizeof(s_last));
    /* wear_status 默认为 UNKNOWN (0)。
     * NADT 检测到佩戴(1)或脱下(2)时会触发状态变化通知。
     * 如果默认设为 WEARING，NADT 报告 wear=1 时会被
     * "s_last.wear_status == wear_status" 拦截，导致
     * 永远不发送通知，UI 始终显示 "--"。 */
    /* 发送重置状态到 mq，确保 UI 立即更新为 UNKNOWN，
     * 避免 UI 保留旧的 wear_status 导致显示错误。 */
    emit();
}

void gh30x_hr_ui_notify_hr(uint8_t bpm, int hr_valid)
{
#if 0 /* [DBG_NOTIFY] */
    rt_kprintf("[DBG_NOTIFY] notify_hr: bpm=%u valid=%d\r\n", (unsigned)bpm, hr_valid);
#endif
    s_last.hr_bpm = bpm;
    s_last.hr_valid = hr_valid ? 1u : 0u;
    emit();
}

void gh30x_hr_ui_notify_wear(uint8_t wear_status)
{
    /* 只在佩戴状态实际变化时才发送通知，避免每帧都触发mq消息导致队列溢出 */
    if (s_last.wear_status == wear_status) {
        return;
    }
#if 0 /* [DBG_NOTIFY] */
    rt_kprintf("[DBG_NOTIFY] wear CHANGED: %u -> %u\r\n",
               (unsigned)s_last.wear_status, (unsigned)wear_status);
#endif
    s_last.wear_status = wear_status;
    emit();
}
