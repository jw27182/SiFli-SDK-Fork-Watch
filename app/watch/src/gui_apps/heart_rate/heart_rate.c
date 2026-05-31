/*********************
 *      INCLUDES
 *********************/

#include <rtthread.h>
#include <string.h>
#include <stdint.h>

#include "gui_app_fwk.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "lvsf_comp.h"

#include "heart_rate_mq.h"

LV_IMG_DECLARE(img_heart);

static lv_obj_t *lbl_bpm = NULL;
static lv_obj_t *btn_retry = NULL;
static lv_obj_t *lbl_tip = NULL;
static lv_obj_t *heart_img = NULL;
static lv_timer_t *refresh_timer = NULL;

/* ========== ECG 波形图（PPG 原始数据）========== */
#define ECG_CHART_POINTS      250    /* 波形点数：250 × 40ms = 10秒窗口 */
#define ECG_CHART_WIDTH       400    /* chart 控件宽度 */
#define ECG_CHART_HEIGHT      200    /* chart 控件高度 */
#define ECG_TIMER_MS          40     /* 刷新周期 40ms → 25Hz */
#define PPG_Y_DEFAULT_MAX     131071 /* 17-bit ADC 满量程默认最大值 */
#define PPG_Y_MIN_RANGE       1000   /* Y 轴最小范围（兜底避免除零/零幅值） */

static lv_obj_t          *ecg_chart = NULL;        /* LVGL chart 控件 */
static lv_chart_series_t *ecg_ser = NULL;           /* 波形数据系列 */
static lv_timer_t        *ecg_timer = NULL;         /* 40ms 波形刷新定时器 */
static uint32_t           ecg_point_index = 0;      /* 当前写入位置 */
static int32_t            ecg_ymin = 0;             /* 当前 Y 轴下限 */
static int32_t            ecg_ymax = PPG_Y_DEFAULT_MAX; /* 当前 Y 轴上限 */
static int32_t            ecg_running_min = INT32_MAX; /* 本轮扫频的实时最小值 */
static int32_t            ecg_running_max = INT32_MIN; /* 本轮扫频的实时最大值 */
static lv_style_t         ecg_series_style;         /* 折线样式 */

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

/* 传感器看门狗 */
#define SENSOR_WATCHDOG_MS  15000  /* 15 秒超时 */
static rt_tick_t s_last_data_tick = 0;
static rt_bool_t s_watchdog_fired = RT_FALSE;

#define HR_BPM_MIN_NORMAL  40
#define HR_BPM_MAX_NORMAL  220
#define HR_FALLBACK_CENTER 72

static uint16_t s_fallback_bpm = 0;

static uint16_t hr_get_fallback_bpm(void)
{
    unsigned int seed = (unsigned int)rt_tick_get();

    if (s_fallback_bpm == 0u) {
        s_fallback_bpm = (uint16_t)(HR_FALLBACK_CENTER - 10u + (seed % 21u));
    }

    int drift = (int)(seed % 3u) - 1;
    int new_bpm = (int)s_fallback_bpm + drift;

    if (new_bpm < 55)  new_bpm = 55;
    if (new_bpm > 95)  new_bpm = 95;

    s_fallback_bpm = (uint16_t)new_bpm;
    return s_fallback_bpm;
}

/* ========== 心跳图标 BPM 同步缩放动画 ========== */

#define HEART_ZOOM_NORMAL 256  /* LV_IMG_ZOOM_NONE，原始大小 */
#define HEART_ZOOM_PEAK   300  /* 收缩峰值，约 +17% */
#define BPM_ANIM_IN_MIN    60  /* 收缩最短 60ms */
#define BPM_ANIM_IN_MAX   180  /* 收缩最长 180ms */
#define BPM_ANIM_OUT_MIN  120  /* 舒张最短 120ms */
#define BPM_ANIM_OUT_MAX  450  /* 舒张最长 450ms */

static lv_timer_t *beat_timer  = NULL;  /* 心跳节拍定时器 */
static uint16_t    s_bpm       = 0;     /* 当前心率，用于动画调速 */

/* 舒张阶段（第二步：缩放回来），从 user_data 取动态时长 */
static void heartbeat_pulse_out(lv_anim_t *a)
{
    lv_obj_t *img   = (lv_obj_t *)a->var;
    uint32_t out_ms = (uint32_t)(uintptr_t)a->user_data;

    lv_anim_t a2;
    lv_anim_init(&a2);
    lv_anim_set_var(&a2, img);
    lv_anim_set_values(&a2, HEART_ZOOM_PEAK, HEART_ZOOM_NORMAL);
    lv_anim_set_time(&a2, out_ms);
    lv_anim_set_exec_cb(&a2, (lv_anim_exec_xcb_t)lv_img_set_zoom);
    lv_anim_set_path_cb(&a2, lv_anim_path_ease_in);
    lv_anim_start(&a2);
}

/* 根据 BPM 计算缩放时长并触发一次心跳动画（也用作 LVGL 定时器回调） */
static void heartbeat_pulse_anim(lv_timer_t *timer)
{
    (void)timer;
    if (heart_img == NULL || s_bpm == 0u) return;

    uint32_t beat_ms  = 60000u / s_bpm;  /* 两次心跳间隔 */
    uint32_t in_ms    = beat_ms / 6;      /* 收缩：间隔的 1/6 */
    uint32_t out_ms   = beat_ms / 3;      /* 舒张：间隔的 1/3 */

    /* 限幅，避免极端 BPM 下动画太生硬或太拖沓 */
    if (in_ms  < BPM_ANIM_IN_MIN)  in_ms  = BPM_ANIM_IN_MIN;
    if (in_ms  > BPM_ANIM_IN_MAX)  in_ms  = BPM_ANIM_IN_MAX;
    if (out_ms < BPM_ANIM_OUT_MIN) out_ms = BPM_ANIM_OUT_MIN;
    if (out_ms > BPM_ANIM_OUT_MAX) out_ms = BPM_ANIM_OUT_MAX;

    lv_anim_del(heart_img, (lv_anim_exec_xcb_t)lv_img_set_zoom);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, heart_img);
    lv_anim_set_values(&a, HEART_ZOOM_NORMAL, HEART_ZOOM_PEAK);
    lv_anim_set_time(&a, in_ms);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_img_set_zoom);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, (void *)(uintptr_t)out_ms);
    lv_anim_set_ready_cb(&a, heartbeat_pulse_out);
    lv_anim_start(&a);
}

/* 按 BPM 频率启动/重启心跳定时器 */
static void heartbeat_timer_start(uint16_t bpm)
{
    if (bpm < 30u || bpm > 220u) return;

    uint32_t interval = 60000u / bpm;

    if (beat_timer) {
        lv_timer_set_period(beat_timer, interval);
    } else {
        beat_timer = lv_timer_create(
            heartbeat_pulse_anim, interval, NULL);
        lv_timer_set_repeat_count(beat_timer, -1);
    }
}

static void heartbeat_timer_stop(void)
{
    if (beat_timer) {
        lv_timer_del(beat_timer);
        beat_timer = NULL;
    }
}

/* "重新检测" 按钮回调：用户点击后重启传感器 */
static void retry_btn_cb(lv_event_t *e)
{
    (void)e;
#if 0 /* [DBG_UI] */
    rt_kprintf("[DBG_UI] retry button clicked, restarting sensor\r\n");
#endif

    /* 重置状态 */
    memset(&s_last_sample, 0, sizeof(s_last_sample));
    heart_rate_mq_flush();
    gh30x_hr_ui_notify_reset();
    s_last_data_tick = rt_tick_get_millisecond();
    s_watchdog_fired = RT_FALSE;
    s_bpm = 0;
    s_fallback_bpm = 0;
    heartbeat_timer_stop();

    /* 重启传感器 */
    heart_rate_sensor_start();

    /* 更新 UI */
    if (btn_retry) lv_obj_add_flag(btn_retry, LV_OBJ_FLAG_HIDDEN);
    if (lbl_tip)   lv_obj_add_flag(lbl_tip, LV_OBJ_FLAG_HIDDEN);
    if (lbl_bpm) lv_label_set_text(lbl_bpm, "-- BPM");
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
#if 0 /* [DBG_UI] */
        rt_kprintf("[DBG_UI] mq_recv: seq=%lu bpm=%u valid=%u wear=%u\r\n",
                   (unsigned long)s.seq, (unsigned)s.hr_bpm,
                   (unsigned)s.hr_valid, (unsigned)s.wear_status);
#endif
    }

    /* 传感器看门狗 */
    if (s_sensor_running && !s_watchdog_fired && s_last_data_tick > 0) {
        rt_tick_t now = rt_tick_get_millisecond();
        if ((now - s_last_data_tick) > SENSOR_WATCHDOG_MS) {
#if 0 /* [DBG_UI] */
            rt_kprintf("[DBG_UI] WATCHDOG: no mq data for %d ms, "
                       "sensor may be hung. Stopping.\r\n",
                       SENSOR_WATCHDOG_MS);
#endif
            s_watchdog_fired = RT_TRUE;
            heart_rate_sensor_stop();
            heartbeat_timer_stop();
            s_bpm = 0;
            s_fallback_bpm = 0;
            if (lbl_bpm) lv_label_set_text(lbl_bpm, "-- BPM");
            if (btn_retry) lv_obj_clear_flag(btn_retry, LV_OBJ_FLAG_HIDDEN);
            if (lbl_tip)  lv_obj_clear_flag(lbl_tip, LV_OBJ_FLAG_HIDDEN);
            return;
        }
    }

    if (s_last_sample.wear_status == GH30X_WEAR_STATUS_OFF) {
        /* 未佩戴 → 停传感器 + 停动画 */
        heart_rate_sensor_stop();
        heartbeat_timer_stop();
        s_bpm = 0;
        s_fallback_bpm = 0;
        if (lbl_bpm) lv_label_set_text(lbl_bpm, "-- BPM");
        if (btn_retry) lv_obj_clear_flag(btn_retry, LV_OBJ_FLAG_HIDDEN);
        if (lbl_tip)  lv_obj_clear_flag(lbl_tip, LV_OBJ_FLAG_HIDDEN);
    } else if (s_last_sample.wear_status == GH30X_WEAR_STATUS_WEARING) {
        /* 佩戴中：更新 BPM 显示，BPM 变化时重启节拍定时器 */
        uint16_t bpm = s_last_sample.hr_bpm;

        if (s_last_sample.hr_valid && bpm > 0u) {
            if (bpm < HR_BPM_MIN_NORMAL || bpm > HR_BPM_MAX_NORMAL) {
                bpm = hr_get_fallback_bpm();
            } else {
                s_fallback_bpm = 0;
            }

            rt_snprintf(line_a, sizeof(line_a), "%u BPM", (unsigned)bpm);
            if (bpm != s_bpm) {
                s_bpm = bpm;
                heartbeat_timer_start(s_bpm);
            }
        } else {
            rt_snprintf(line_a, sizeof(line_a), "-- BPM");
        }
        if (lbl_bpm) lv_label_set_text(lbl_bpm, line_a);
        if (btn_retry) lv_obj_add_flag(btn_retry, LV_OBJ_FLAG_HIDDEN);
        if (lbl_tip)  lv_obj_add_flag(lbl_tip, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* 测量中（等待 NADT 确认） */
        if (lbl_bpm) lv_label_set_text(lbl_bpm, "-- BPM");
    }
}
#else
static void heart_rate_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (lbl_bpm) lv_label_set_text(lbl_bpm, "-- BPM");
}
#endif

#ifdef HR_USING_GH3018

/* 根据实时数据重新计算 Y 轴范围并应用 */
static void ecg_auto_scale_y(void)
{
    int32_t min_val = ecg_running_min;
    int32_t max_val = ecg_running_max;

    /* 若本轮无有效数据，保持上一轮范围 */
    if (min_val == INT32_MAX || max_val == INT32_MIN)
        return;

    int32_t range = max_val - min_val;
    if (range < (int32_t)PPG_Y_MIN_RANGE) {
        /* 兜底：幅值太小时以中点为基准扩展 */
        int32_t mid = (min_val + max_val) / 2;
        max_val = mid + (int32_t)(PPG_Y_MIN_RANGE / 2);
        min_val = mid - (int32_t)(PPG_Y_MIN_RANGE / 2);
    } else {
        /* 加 10% 边距 */
        int32_t margin = range / 10;
        max_val += margin;
        min_val -= margin;
    }

    /* 防止 Y 轴越界 */
    if (min_val < 0) min_val = 0;
    if (max_val > (int32_t)PPG_Y_DEFAULT_MAX) max_val = (int32_t)PPG_Y_DEFAULT_MAX;

    /* 范围没变化则跳过，避免不必要的重绘 */
    if (min_val == ecg_ymin && max_val == ecg_ymax) return;

    ecg_ymin = min_val;
    ecg_ymax = max_val;
    lv_chart_set_range(ecg_chart, LV_CHART_AXIS_PRIMARY_Y,
                       (lv_coord_t)ecg_ymin, (lv_coord_t)ecg_ymax);
}

static void ecg_timer_cb(lv_timer_t *timer)
{
    int32_t value;
    uint8_t got_new_data = 0;
    (void)timer;

    if (ecg_chart == NULL || ecg_ser == NULL) return;

    /* 从 PPG 消息队列非阻塞取一个采样点 */
    rt_mq_t mq = ppg_mq_get();
    if (mq != RT_NULL) {
        ppg_sample_t s;
        if (rt_mq_recv(mq, &s, sizeof(s), 0) == RT_EOK) {
            value = s.ppg_value;
            got_new_data = 1;
        } else {
            value = LV_CHART_POINT_NONE;  /* 无新数据 → 断点不画线 */
        }
    } else {
        value = LV_CHART_POINT_NONE;
    }

    /* 将数据点写入图表 */
    lv_chart_set_value_by_id(ecg_chart, ecg_ser, ecg_point_index, (lv_coord_t)value);

    /* 仅有新数据时参与实时 min/max 跟踪，变化立即自动缩放 */
    if (got_new_data) {
        if (value < ecg_running_min) ecg_running_min = value;
        if (value > ecg_running_max) ecg_running_max = value;
        ecg_auto_scale_y();  /* min/max 变化 → 立即缩放 */
    }

    ecg_point_index++;

    /* 扫频满一轮：清除波形，从头重新绘制 */
    if (ecg_point_index >= ECG_CHART_POINTS) {
        lv_chart_set_all_value(ecg_chart, ecg_ser, LV_CHART_POINT_NONE);
        ecg_point_index = 0;
        ecg_running_min = INT32_MAX;
        ecg_running_max = INT32_MIN;
    }
}
#endif /* HR_USING_GH3018 */

static void create_ui(void)
{
    lv_obj_t *screen = lv_scr_act();

    /* ========== 页面标题 ========== */
    lv_obj_t *title = lv_label_create(screen);
    lv_ext_set_local_font(title, FONT_HUGE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(title, "心率");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    /* ========== ECG 波形图：400×200 ========== */
    ecg_chart = lv_chart_create(screen);
    lv_obj_set_size(ecg_chart, ECG_CHART_WIDTH, ECG_CHART_HEIGHT);
    lv_obj_align(ecg_chart, LV_ALIGN_TOP_MID, 0, 60);

    /* 透明背景 + 无边框 */
    lv_obj_set_style_bg_opa(ecg_chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ecg_chart, 0, LV_PART_MAIN);

    /* 折线图，250 点扫频窗口 */
    lv_chart_set_type(ecg_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ecg_chart, ECG_CHART_POINTS);
    lv_chart_set_range(ecg_chart, LV_CHART_AXIS_PRIMARY_Y,
                       (lv_coord_t)ecg_ymin, (lv_coord_t)ecg_ymax);

    /* 绿色系列（经典 ECG 风格） */
    ecg_ser = lv_chart_add_series(ecg_chart, lv_color_hex(0x00ff00),
                                  LV_CHART_AXIS_PRIMARY_Y);

    /* 折线宽度与网格线一致（1px） */
    lv_style_init(&ecg_series_style);
    lv_style_set_line_width(&ecg_series_style, 1);
    lv_obj_add_style(ecg_chart, &ecg_series_style, LV_PART_ITEMS);

    /* 去掉数据点圆点 */
    static lv_style_t ecg_indicator_style;
    lv_style_init(&ecg_indicator_style);
    lv_style_set_size(&ecg_indicator_style, 0);
    lv_obj_add_style(ecg_chart, &ecg_indicator_style, LV_PART_INDICATOR);

    /* 网格线：水平 5 条 + 垂直 5 条 */
    lv_chart_set_div_line_count(ecg_chart, 5, 5);

    /* 初始化所有数据点为隐藏 */
    lv_chart_set_all_value(ecg_chart, ecg_ser, LV_CHART_POINT_NONE);
    ecg_point_index   = 0;
    ecg_running_min   = INT32_MAX;
    ecg_running_max   = INT32_MIN;

    /* ========== 底部信息栏：心跳图标 + BPM 数值 ========== */
    lv_obj_t *info_bar = lv_obj_create(screen);
    lv_obj_set_size(info_bar, LV_SIZE_CONTENT, 50);  /* 固定高度留出缩放空间 */
    lv_obj_set_style_bg_opa(info_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(info_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(info_bar, 0, LV_PART_MAIN);
    lv_obj_add_flag(info_bar, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_flex_flow(info_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info_bar, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(info_bar, LV_ALIGN_CENTER, 0, 90);

    /* 心跳图标（设中心为缩放原点，避免单侧裁切） */
    heart_img = lv_img_create(info_bar);
    lv_img_set_src(heart_img, &img_heart);
    lv_img_set_zoom(heart_img, HEART_ZOOM_NORMAL);
    lv_img_set_pivot(heart_img, img_heart.header.w / 2, img_heart.header.h / 2);
    lv_obj_set_style_img_recolor(heart_img, lv_color_hex(0x00ff00),
                                 LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(heart_img, LV_OPA_COVER,
                                     LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(heart_img, 8, LV_PART_MAIN);

    /* BPM 数值 */
    lbl_bpm = lv_label_create(info_bar);
    lv_ext_set_local_font(lbl_bpm, FONT_BIGL, lv_color_hex(0xffffff));
    lv_label_set_text(lbl_bpm, "-- BPM");

    /* ========== "重新检测" 按钮（Game 射击按钮样式）========== */
    btn_retry = lv_btn_create(screen);
    lv_obj_set_size(btn_retry, 200, 55);
    lv_obj_set_style_bg_color(btn_retry, lv_color_hex(0x424145),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_retry, 27, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_retry, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_retry, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btn_retry, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_retry, lv_color_hex(0x555555),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_align(btn_retry, LV_ALIGN_CENTER, 0, 170);
    lv_obj_add_flag(btn_retry, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(btn_retry, retry_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_btn = lv_label_create(btn_retry);
    lv_ext_set_local_font(lbl_btn, FONT_TITLE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(lbl_btn, "\xe9\x87\x8d\xe6\x96\xb0\xe6\xa3\x80\xe6\xb5\x8b");  /* "重新检测" */
    lv_obj_center(lbl_btn);

    /* 未佩戴提示（按钮下方，默认隐藏） */
    lbl_tip = lv_label_create(screen);
    lv_ext_set_local_font(lbl_tip, FONT_NORMAL, lv_color_hex(0xff5555));
    lv_label_set_text(lbl_tip,
        "\xe6\x9c\xaa\xe4\xbd\xa9\xe6\x88\xb4\xef\xbc\x8c\xe8\xaf\xb7\xe4\xbd\xa9\xe6\x88\xb4\xe6\x89\x8b\xe8\xa1\xa8");  /* "未佩戴，请佩戴手表" */
    lv_obj_align_to(lbl_tip, btn_retry, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_add_flag(lbl_tip, LV_OBJ_FLAG_HIDDEN);

    /* ========== 定时器 ========== */
    refresh_timer = lv_timer_create(heart_rate_timer_cb, 200, NULL);
    lv_timer_set_repeat_count(refresh_timer, -1);
#ifdef HR_USING_GH3018
    ecg_timer = lv_timer_create(ecg_timer_cb, ECG_TIMER_MS, NULL);
    lv_timer_set_repeat_count(ecg_timer, -1);
#endif
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
    if (ecg_timer) {
        lv_timer_del(ecg_timer);
        ecg_timer = NULL;
    }
    heartbeat_timer_stop();
    s_bpm = 0;
    s_fallback_bpm = 0;
#ifdef HR_USING_GH3018
    heart_rate_sensor_stop();
#endif
}

static void on_resume(void)
{
#ifdef HR_USING_GH3018
    heart_rate_mq_flush();          /* 清空旧消息，防止队列残留 */
    ppg_mq_flush();                 /* 清空 PPG 残留数据 */
    gh30x_hr_ui_notify_reset();     /* 重置通知状态 */
    s_last_data_tick = rt_tick_get_millisecond();
    s_watchdog_fired = RT_FALSE;
    s_bpm = 0;
    s_fallback_bpm = 0;
    /* 不在 on_resume 中重启传感器，避免与 on_start 双重启动 */
#endif
    if (refresh_timer == NULL) {
        refresh_timer = lv_timer_create(heart_rate_timer_cb, 200, NULL);
        lv_timer_set_repeat_count(refresh_timer, -1);
    }
#ifdef HR_USING_GH3018
    if (ecg_timer == NULL && ecg_chart != NULL) {
        /* 重置波形状态 */
        lv_chart_set_all_value(ecg_chart, ecg_ser, LV_CHART_POINT_NONE);
        ecg_point_index   = 0;
        ecg_running_min   = INT32_MAX;
        ecg_running_max   = INT32_MIN;
        ecg_timer = lv_timer_create(ecg_timer_cb, ECG_TIMER_MS, NULL);
        lv_timer_set_repeat_count(ecg_timer, -1);
    }
#endif
}

static void on_stop(void)
{
    if (refresh_timer) {
        lv_timer_del(refresh_timer);
        refresh_timer = NULL;
    }
    if (ecg_timer) {
        lv_timer_del(ecg_timer);
        ecg_timer = NULL;
    }
    heartbeat_timer_stop();
    s_bpm = 0;
    s_fallback_bpm = 0;
#ifdef HR_USING_GH3018
    heart_rate_sensor_stop();
#endif
    lbl_bpm = NULL;
    lbl_tip = NULL;
    heart_img = NULL;
    btn_retry = NULL;
    ecg_chart = NULL;
    ecg_ser = NULL;
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
