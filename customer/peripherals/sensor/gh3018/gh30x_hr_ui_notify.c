#include "gh30x_hr_ui_notify.h"
#include <string.h>

static void (*s_cb)(const gh30x_hr_ui_sample_t *sample);
static gh30x_hr_ui_sample_t s_last;

static void emit(void)
{
    if (s_cb == 0) return;
    s_last.seq++;
    s_cb(&s_last);
}

void gh30x_hr_ui_notify_register(void (*cb)(const gh30x_hr_ui_sample_t *sample))
{
    s_cb = cb;
    memset(&s_last, 0, sizeof(s_last));
}

void gh30x_hr_ui_notify_hr(uint8_t bpm, int hr_valid)
{
    s_last.hr_bpm = bpm;
    s_last.hr_valid = hr_valid ? 1u : 0u;
    emit();
}
