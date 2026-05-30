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
static lv_obj_t *lbl_wear = NULL;
static lv_obj_t *btn_retry = NULL;
static lv_timer_t *refresh_timer = NULL;

#ifdef HR_USING_GH3018
#include "gh3018.h"

static rt_bool_t s_gh3018_module_ready = RT_FALSE;
static rt_bool_t s_sensor_running = RT_FALSE;

static void heart_rate_sensor_start(void)
{
    if (!s_gh3018_module_ready) {
        if (init_gh3018_sensor() != 0) return;
        s_gh3018_module_ready = RT_TRUE;
    }
    gh30x_api_lock();
    set_gh3018_hr_mode();
    gh30x_api_unlock();
    s_sensor_running = RT_TRUE;
}

static void heart_rate_sensor_stop(void)
{
    if (!s_gh3018_module_ready) return;
    if (!s_sensor_running) return;
    gh30x_api_lock();
    close_gh3018();
    gh30x_api_unlock();
    s_sensor_running = RT_FALSE;
}

static gh30x_hr_ui_sample_t s_last_sample;

/* 传感器看门狗：如果传感器运行中但超过此时间没有收到 mq 消息，
 * 则认为传感器已挂起，自动停止并显示重试按钮。
 * 原因：GH3018 芯片可能因 I2C 总线异常或中断引脚停止触发而静默挂起，
 * 导致 NADT 永远无法输出 wear-off 事件。 */
#define SENSOR_WATCHDOG_MS  15000  /* 15 秒超时 */
static rt_tick_t s_last_data_tick = 0;
static rt_bool_t s_watchdog_fired = RT_FALSE;

/* "重新检测" 按钮回调：用户点击后重启传感器 */
static void retry_btn_cb(lv_event_t *e)
{
    (void)e;
#if 1 /* [DBG_UI] */
    rt_kprintf("[DBG_UI] retry button clicked, restarting sensor\r\n");
#endif

    /* 重置状态 */
    memset(&s_last_sample, 0, sizeof(s_last_sample));
    heart_rate_mq_flush();
    gh30x_hr_ui_notify_reset();
    s_last_data_tick = rt_tick_get_millisecond();
    s_watchdog_fired = RT_FALSE;

    /* 重启传感器 */
    heart_rate_sensor_start();

    /* 更新 UI */
    if (btn_retry) lv_obj_add_flag(btn_retry, LV_OBJ_FLAG_HIDDEN);
    if (lbl_bpm) lv_label_set_text(lbl_bpm, "-- BPM");
    if (lbl_wear) {
        lv_label_set_text(lbl_wear, "\xe6\xb5\x8b\xe9\x87\x8f\xe4\xb8\xad...");  /* "测量中..." */
        lv_obj_set_style_text_color(lbl_wear, lv_color_hex(0x888888), LV_PART_MAIN);
    }
}

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
        s_last_data_tick = rt_tick_get_millisecond();
        s_watchdog_fired = RT_FALSE;
#if 1 /* [DBG_UI] */
        rt_kprintf("[DBG_UI] mq_recv: seq=%lu bpm=%u valid=%u wear=%u\r\n",
                   (unsigned long)s.seq, (unsigned)s.hr_bpm,
                   (unsigned)s.hr_valid, (unsigned)s.wear_status);
#endif
    }

    /* 传感器看门狗：如果传感器运行中但长时间没有 mq 消息，
     * 说明传感器可能已挂起（I2C 总线异常或中断停止触发）。
     * 此时主动停止传感器并显示重试按钮，避免 UI 永久卡在"佩戴中"。 */
    if (s_sensor_running && !s_watchdog_fired && s_last_data_tick > 0) {
        rt_tick_t now = rt_tick_get_millisecond();
        if ((now - s_last_data_tick) > SENSOR_WATCHDOG_MS) {
            rt_kprintf("[DBG_UI] WATCHDOG: no mq data for %d ms, "
                       "sensor may be hung. Stopping.\r\n",
                       SENSOR_WATCHDOG_MS);
            s_watchdog_fired = RT_TRUE;
            heart_rate_sensor_stop();
            if (lbl_bpm) lv_label_set_text(lbl_bpm, "-- BPM");
            if (lbl_wear) {
                lv_label_set_text(lbl_wear,
                    "\xe4\xbc\xa0\xe6\x84\x9f\xe5\x99\xa8\xe5\xbc\x82\xe5\xb8\xb8\xef\xbc\x8c\xe8\xaf\xb7\xe9\x87\x8d\xe8\xaf\x95");  /* "传感器异常，请重试" */
                lv_obj_set_style_text_color(lbl_wear, lv_color_hex(0xff0000), LV_PART_MAIN);
            }
            if (btn_retry) lv_obj_clear_flag(btn_retry, LV_OBJ_FLAG_HIDDEN);
            return;
        }
    }

    /*
     * 佩戴状态判定逻辑：
     * 1. NADT 报告 wear_status == OFF → 停止传感器，显示"未佩戴"，显示"重新检测"按钮
     * 2. NADT 报告 wear_status == WEARING → 显示"佩戴中"
     * 3. 其他（UNKNOWN，NADT 尚未决策）→ 显示"测量中..."
     *
     * 不再使用 HR 有效（bpm > 0）作为佩戴判断的后备条件。
     * 原因：重置后 wear_status 为 UNKNOWN，若 HR 算法先于 NADT 产出 BPM，
     *       会误触发"佩戴中"，导致未佩戴时 UI 卡在"佩戴中"状态。
     */
    if (s_last_sample.wear_status == GH30X_WEAR_STATUS_OFF) {
        /* 未佩戴：停止传感器，显示提示和重试按钮 */
        heart_rate_sensor_stop();
        if (lbl_bpm) lv_label_set_text(lbl_bpm, "-- BPM");
        if (lbl_wear) {
            lv_label_set_text(lbl_wear,
                "\xe6\x9c\xaa\xe4\xbd\xa9\xe6\x88\xb4\xef\xbc\x8c\xe8\xaf\xb7\xe4\xbd\xa9\xe6\x88\xb4");  /* "未佩戴，请佩戴" */
            lv_obj_set_style_text_color(lbl_wear, lv_color_hex(0xff0000), LV_PART_MAIN);
        }
        if (btn_retry) lv_obj_clear_flag(btn_retry, LV_OBJ_FLAG_HIDDEN);
    } else if (s_last_sample.wear_status == GH30X_WEAR_STATUS_WEARING) {
        /* 佩戴中：NADT 确认佩戴 */
        if (s_last_sample.hr_valid && s_last_sample.hr_bpm > 0u) {
            rt_snprintf(line_a, sizeof(line_a), "%u BPM",
                          (unsigned)s_last_sample.hr_bpm);
        } else {
            rt_snprintf(line_a, sizeof(line_a), "-- BPM");
        }
        if (lbl_bpm) lv_label_set_text(lbl_bpm, line_a);
        if (lbl_wear) {
            lv_label_set_text(lbl_wear, "\xe4\xbd\xa9\xe6\x88\xb4\xe4\xb8\xad");  /* "佩戴中" */
            lv_obj_set_style_text_color(lbl_wear, lv_color_hex(0x00ff00), LV_PART_MAIN);
        }
        if (btn_retry) lv_obj_add_flag(btn_retry, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* 测量中（等待 NADT 确认） */
        if (lbl_bpm) lv_label_set_text(lbl_bpm, "-- BPM");
        if (lbl_wear) {
            lv_label_set_text(lbl_wear, "\xe6\xb5\x8b\xe9\x87\x8f\xe4\xb8\xad...");  /* "测量中..." */
            lv_obj_set_style_text_color(lbl_wear, lv_color_hex(0x888888), LV_PART_MAIN);
        }
    }
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
    lv_obj_align(lbl_bpm, LV_ALIGN_CENTER, 0, -20);

    lbl_wear = lv_label_create(screen);
    lv_ext_set_local_font(lbl_wear, FONT_SUBTITLE, lv_color_hex(0x888888));
    lv_label_set_text(lbl_wear, "--");
    lv_obj_align(lbl_wear, LV_ALIGN_CENTER, 0, 30);

    /* "重新检测" 按钮（默认隐藏，未佩戴时显示） */
    btn_retry = lv_btn_create(screen);
    lv_obj_set_size(btn_retry, 140, 40);
    lv_obj_align(btn_retry, LV_ALIGN_CENTER, 0, 80);
    lv_obj_add_flag(btn_retry, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(btn_retry, retry_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_btn = lv_label_create(btn_retry);
    lv_ext_set_local_font(lbl_btn, FONT_SUBTITLE, lv_color_hex(0xffffff));
    lv_label_set_text(lbl_btn, "\xe9\x87\x8d\xe6\x96\xb0\xe6\xa3\x80\xe6\xb5\x8b");  /* "重新检测" */
    lv_obj_center(lbl_btn);

    refresh_timer = lv_timer_create(heart_rate_timer_cb, 200, NULL);
    lv_timer_set_repeat_count(refresh_timer, -1);
}

static void on_start(void)
{
    (void)heart_rate_mq_init();
#ifdef HR_USING_GH3018
    memset(&s_last_sample, 0, sizeof(s_last_sample));
    heart_rate_mq_flush();          /* 清空旧消息，防止队列残留 */
    gh30x_hr_ui_notify_reset();     /* 重置通知状态 */
    s_last_data_tick = rt_tick_get_millisecond();
    s_watchdog_fired = RT_FALSE;
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
    heart_rate_mq_flush();          /* 清空旧消息，防止队列残留 */
    gh30x_hr_ui_notify_reset();     /* 重置通知状态 */
    s_last_data_tick = rt_tick_get_millisecond();
    s_watchdog_fired = RT_FALSE;
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
    lbl_wear = NULL;
    btn_retry = NULL;
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
