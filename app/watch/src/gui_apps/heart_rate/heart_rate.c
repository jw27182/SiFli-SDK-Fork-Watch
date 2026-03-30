/*********************
 *      INCLUDES
 *********************/

#include <rtthread.h>
#include <string.h>

#include "gui_app_fwk.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "lvsf_comp.h"

#include "heart_rate_mq.h"

static lv_obj_t *lbl_bpm = NULL;
static lv_timer_t *refresh_timer = NULL;

#ifdef HR_USING_GH3018
#include "gh3018.h"

static rt_bool_t s_gh3018_module_ready = RT_FALSE;

static void heart_rate_sensor_start(void)
{
    if (!s_gh3018_module_ready) {
        if (init_gh3018_sensor() != 0) return;
        s_gh3018_module_ready = RT_TRUE;
    }
    gh30x_api_lock();
    set_gh3018_hr_mode();
    gh30x_api_unlock();
}

static void heart_rate_sensor_stop(void)
{
    if (!s_gh3018_module_ready) return;
    gh30x_api_lock();
    close_gh3018();
    gh30x_api_unlock();
}

static gh30x_hr_ui_sample_t s_last_sample;

static void heart_rate_timer_cb(lv_timer_t *timer)
{
    rt_mq_t mq;
    gh30x_hr_ui_sample_t s;
    char line_a[32];

    (void)timer;

    mq = heart_rate_mq_get();
    if (mq == RT_NULL) {
        if (lbl_bpm) lv_label_set_text(lbl_bpm, "-- BPM");
        return;
    }

    while (rt_mq_recv(mq, &s, sizeof(s), RT_WAITING_NO) == RT_EOK) {
        s_last_sample = s;
    }

    if (s_last_sample.hr_valid && s_last_sample.hr_bpm > 0u) {
        rt_snprintf(line_a, sizeof(line_a), "%u BPM",
                      (unsigned)s_last_sample.hr_bpm);
    } else {
        rt_snprintf(line_a, sizeof(line_a), "-- BPM");
    }
    if (lbl_bpm) lv_label_set_text(lbl_bpm, line_a);
}
#else
static void heart_rate_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (lbl_bpm) lv_label_set_text(lbl_bpm, "-- BPM");
}
#endif

static void create_ui(void)
{
    lv_obj_t *screen = lv_scr_act();

    lbl_bpm = lv_label_create(screen);
    lv_ext_set_local_font(lbl_bpm, FONT_BIGL, lv_color_hex(0xffffff));
    lv_label_set_text(lbl_bpm, "-- BPM");
    lv_obj_align(lbl_bpm, LV_ALIGN_CENTER, 0, 0);

    refresh_timer = lv_timer_create(heart_rate_timer_cb, 200, NULL);
    lv_timer_set_repeat_count(refresh_timer, -1);
}

static void on_start(void)
{
    (void)heart_rate_mq_init();
#ifdef HR_USING_GH3018
    memset(&s_last_sample, 0, sizeof(s_last_sample));
    heart_rate_sensor_start();
#endif
    create_ui();
    lv_img_cache_invalidate_src(NULL);
}

static void on_pause(void)
{
    if (refresh_timer) {
        lv_timer_del(refresh_timer);
        refresh_timer = NULL;
    }
#ifdef HR_USING_GH3018
    heart_rate_sensor_stop();
#endif
}

static void on_resume(void)
{
#ifdef HR_USING_GH3018
    heart_rate_sensor_start();
#endif
    if (refresh_timer == NULL) {
        refresh_timer = lv_timer_create(heart_rate_timer_cb, 200, NULL);
        lv_timer_set_repeat_count(refresh_timer, -1);
    }
}

static void on_stop(void)
{
    if (refresh_timer) {
        lv_timer_del(refresh_timer);
        refresh_timer = NULL;
    }
#ifdef HR_USING_GH3018
    heart_rate_sensor_stop();
#endif
    lbl_bpm = NULL;
}

static void msg_handler(gui_app_msg_type_t msg, void *param)
{
    switch (msg) {
        case GUI_APP_MSG_ONSTART:
            on_start();
            break;

        case GUI_APP_MSG_ONRESUME:
            on_resume();
            break;

        case GUI_APP_MSG_ONPAUSE:
            on_pause();
            break;

        case GUI_APP_MSG_ONSTOP:
            on_stop();
            break;
        default:
            break;
    }
    (void)param;
}

LV_IMG_DECLARE(img_heart_rate);
#define APP_ID "heart_rate"
static int app_main(intent_t i)
{
    gui_app_regist_msg_handler(APP_ID, msg_handler);

    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(heart_rate), LV_EXT_IMG_GET(img_heart_rate),
                   APP_ID, app_main);
