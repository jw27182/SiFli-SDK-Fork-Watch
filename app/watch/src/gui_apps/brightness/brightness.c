/*********************
 *      INCLUDES
 *********************/

#include <math.h>
#include <rtdevice.h>
#include <rtthread.h>

#include "app_mem.h"
#include "gui_app_fwk.h"
#include "littlevgl2rtt.h"
#include "lv_ex_data.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "lvsf_comp.h"
#include "sensor.h"

#define clamp(val, min, max) \
    ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))

#define DBG_TAG "gui_apps.brightness"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BRIGHTNESS_MIN_VAL 10
#define BRIGHTNESS_MAX_VAL 100
#define BRIGHTNESS_RANGE (BRIGHTNESS_MAX_VAL - BRIGHTNESS_MIN_VAL)

/* 自动亮度：EMA 平滑环境光、映射到目标档位、滞回防抖、按步进缓慢逼近目标 */
#define AUTO_LUX_EMA_SHIFT 4 /* alpha = 1/16，约 16*50ms 时间常数量级 */
#define AUTO_RAMP_STEP_UP 2    /* 每 tick 最多变亮步进（%） */
#define AUTO_RAMP_STEP_DOWN 1  /* 变暗更慢，减轻屏光反射引起的来回抖 */
#define AUTO_TARGET_HYST_UP 5  /* 目标变亮需超过当前滞回目标的最小差值 */
#define AUTO_TARGET_HYST_DOWN 6
#define AUTO_TIMER_PERIOD_MS 50

/*
 * 环境光 -> 背光档位：对数型曲线，锚点 50 lux -> 亮度值 50（即常见意义上的 50%）。
 * level = anchor_level + gain * ln(lux / anchor_lux)，再限制在 [BRIGHTNESS_MIN_VAL, MAX]。
 */
#define AUTO_LUX_LOG_ANCHOR_LUX 50.0f
#define AUTO_LUX_LOG_ANCHOR_LEVEL 50.0f
#define AUTO_LUX_LOG_GAIN 15.0f
#define AUTO_LUX_LOG_INPUT_MIN 1.0f

LV_IMG_DECLARE(img_brightness_icon);
LV_IMG_DECLARE(img_auto_brightness_icon);

rt_device_t g_brightness_sensor_dev = NULL, g_lcd_sensor_dev = NULL;
static lv_obj_t *g_label = NULL, *g_slider = NULL;
static lv_obj_t* g_brightness_icon = NULL;
static rt_uint8_t g_brightness_now = 100;
static rt_bool_t g_auto_brightness = RT_FALSE;
lv_obj_t *g_auto_brightness_icon = NULL, *g_auto_brightness_btn = NULL;

static rt_bool_t g_auto_lux_filter_reset = RT_FALSE;
static rt_int32_t g_auto_lux_ema = -1;
static rt_uint8_t g_auto_hyst_target = BRIGHTNESS_MAX_VAL;

static rt_uint8_t lux_to_brightness_target(rt_int32_t lux) {
    float x = (float)lux;
    float t;

    if (x < AUTO_LUX_LOG_INPUT_MIN) x = AUTO_LUX_LOG_INPUT_MIN;

    t = AUTO_LUX_LOG_ANCHOR_LEVEL +
        AUTO_LUX_LOG_GAIN * logf(x / AUTO_LUX_LOG_ANCHOR_LUX);

    return (rt_uint8_t)clamp((rt_int32_t)(t + 0.5f), BRIGHTNESS_MIN_VAL,
                             BRIGHTNESS_MAX_VAL);
}

static rt_uint8_t auto_target_apply_hysteresis(rt_uint8_t raw_target,
                                                rt_uint8_t hyst_now) {
    rt_int16_t d = (rt_int16_t)raw_target - (rt_int16_t)hyst_now;

    if (d >= (rt_int16_t)AUTO_TARGET_HYST_UP) return raw_target;
    if (d <= -(rt_int16_t)AUTO_TARGET_HYST_DOWN) return raw_target;
    return hyst_now;
}

static void auto_brightness_ramp_step(rt_uint8_t goal) {
    rt_int32_t delta;

    if (g_brightness_now == goal) return;

    if (g_brightness_now < goal) {
        delta = (rt_int32_t)goal - (rt_int32_t)g_brightness_now;
        g_brightness_now = (rt_uint8_t)((rt_int32_t)g_brightness_now +
                                        clamp(delta, 1, AUTO_RAMP_STEP_UP));
    } else {
        delta = (rt_int32_t)g_brightness_now - (rt_int32_t)goal;
        g_brightness_now = (rt_uint8_t)((rt_int32_t)g_brightness_now -
                                        clamp(delta, 1, AUTO_RAMP_STEP_DOWN));
    }
}

static void icon_scale_anim(lv_obj_t* icon, int32_t start_scale,
                            int32_t end_scale) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, icon);
    lv_anim_set_values(&a, start_scale, end_scale);
    lv_anim_set_time(&a, 200);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_img_set_zoom);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void auto_brightness_btn_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        icon_scale_anim(g_auto_brightness_icon, 256, 230);
    } else if (code == LV_EVENT_RELEASED) {
        icon_scale_anim(g_auto_brightness_icon, 230, 256);
    } else if (code == LV_EVENT_CLICKED) {
        g_auto_brightness = !g_auto_brightness;

        if (g_auto_brightness) {
            g_auto_lux_filter_reset = RT_TRUE;
            lv_obj_set_style_bg_color(g_auto_brightness_btn,
                                      lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor(g_auto_brightness_icon,
                                         lv_color_hex(0x000000),
                                         LV_STATE_DEFAULT);
        } else {
            lv_obj_set_style_bg_color(g_auto_brightness_btn,
                                      lv_color_hex(0x333333), LV_STATE_DEFAULT);
            lv_obj_set_style_img_recolor(g_auto_brightness_icon,
                                         lv_color_hex(0xFFFFFF),
                                         LV_STATE_DEFAULT);
        }

        LOG_I("auto brightness set: %s\n", g_auto_brightness ? "on" : "off");
    }
}

static void brightness_slider_event_cb(lv_event_t* e) {
    lv_obj_t* g_slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(g_slider);
    int16_t angle = (value - BRIGHTNESS_MIN_VAL) * 360 / BRIGHTNESS_RANGE;
    if (g_auto_brightness) {
        g_auto_brightness = RT_FALSE;
        lv_obj_set_style_bg_color(g_auto_brightness_btn, lv_color_hex(0x333333),
                                  LV_STATE_DEFAULT);
        lv_obj_set_style_img_recolor(g_auto_brightness_icon,
                                     lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
        LOG_I("auto brightness set: %s\n", g_auto_brightness ? "on" : "off");
    }

    g_brightness_now = value;
    if (g_brightness_icon) lv_img_set_angle(g_brightness_icon, angle * 10);

    if (g_lcd_sensor_dev)
        rt_device_control(g_lcd_sensor_dev, RTGRAPHIC_CTRL_SET_BRIGHTNESS,
                          &value);
}

static void create_auto_brightness_button(lv_obj_t* parent) {
    g_auto_brightness_btn = lv_btn_create(parent);

    lv_obj_set_size(g_auto_brightness_btn, 100, 100);
    lv_obj_align(g_auto_brightness_btn, LV_ALIGN_CENTER, -80, 160);

    lv_obj_set_style_radius(g_auto_brightness_btn, LV_RADIUS_CIRCLE,
                            LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(g_auto_brightness_btn, lv_color_hex(0x333333),
                              LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_auto_brightness_btn, LV_OPA_COVER,
                            LV_STATE_DEFAULT);

    lv_obj_set_style_transform_width(g_auto_brightness_btn, -5,
                                     LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(g_auto_brightness_btn, -5,
                                      LV_STATE_PRESSED);

    g_auto_brightness_icon = lv_img_create(g_auto_brightness_btn);
    lv_img_set_src(g_auto_brightness_icon,
                   LV_EXT_IMG_GET(img_auto_brightness_icon));
    lv_obj_set_style_img_recolor(g_auto_brightness_icon, lv_color_hex(0xFFFFFF),
                                 LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(g_auto_brightness_icon, LV_OPA_COVER,
                                     LV_STATE_DEFAULT);

    lv_img_set_zoom(g_auto_brightness_icon, 256);

    lv_obj_center(g_auto_brightness_icon);

    lv_obj_add_event_cb(g_auto_brightness_btn, auto_brightness_btn_event_cb,
                        LV_EVENT_ALL, NULL);
}

static void create_brightness_slider(lv_obj_t* parent) {
    g_slider = lv_slider_create(parent);
    lv_obj_remove_style_all(g_slider);

    lv_obj_set_size(g_slider, 100, 420);
    lv_obj_align(g_slider, LV_ALIGN_CENTER, 120, 0);

    lv_obj_set_style_outline_width(g_slider, 0, 0);

    lv_slider_set_range(g_slider, BRIGHTNESS_MIN_VAL, BRIGHTNESS_MAX_VAL);
    lv_slider_set_value(g_slider, BRIGHTNESS_MAX_VAL, LV_ANIM_ON);

    // 美化滑块样式 - 背景（轨道）
    lv_obj_set_style_bg_color(g_slider, lv_color_hex(0x333333),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_slider, LV_OPA_100,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_slider, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(g_slider, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_slider, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(g_slider, 0,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(g_slider, 0,
                                   LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(g_slider, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 美化滑块样式 - 指示条（已填充部分）
    lv_obj_set_style_bg_color(g_slider, lv_color_hex(0xFFFFFF),
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_slider, LV_OPA_100,
                            LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_slider, 50, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_slider, 0,
                                  LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(g_slider, 0,
                                   LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(g_slider, 0,
                                   LV_PART_INDICATOR | LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(g_slider, 0,
                                  LV_PART_INDICATOR | LV_STATE_DEFAULT);

    // 美化滑块样式 - 旋钮
    lv_obj_set_style_bg_color(g_slider, lv_color_hex(0xFFFFFF),
                              LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_slider, LV_OPA_0,
                            LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_slider, 50, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(g_slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(g_slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(g_slider, 0,
                                   LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(g_slider, 0,
                                   LV_PART_KNOB | LV_STATE_FOCUSED);

    lv_obj_add_event_cb(g_slider, brightness_slider_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    g_brightness_icon = lv_img_create(g_slider);
    lv_img_set_src(g_brightness_icon, LV_EXT_IMG_GET(img_brightness_icon));
    lv_obj_align_to(g_brightness_icon, g_slider, LV_ALIGN_OUT_BOTTOM_MID, 0,
                    -82);
}

static void create_ui(void) {
    lv_obj_t* screen = lv_scr_act();
    g_label = lv_label_create(screen);
    lv_obj_center(g_label);
    lv_obj_align(g_label, LV_ALIGN_TOP_MID, 0, 20);

    create_brightness_slider(screen);
    create_auto_brightness_button(screen);
}

static void on_start(void) {
    create_ui();
    lv_img_cache_invalidate_src(NULL);
}

static void brightness_detach_ui_objs(void) {
    g_label = NULL;
    g_slider = NULL;
    g_brightness_icon = NULL;
    g_auto_brightness_btn = NULL;
    g_auto_brightness_icon = NULL;
}

static void on_pause(void) {
}

static void on_resume(void) {
}

static void on_stop(void) {
    brightness_detach_ui_objs();
}

static void msg_handler(gui_app_msg_type_t msg, void* param) {
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
}

LV_IMG_DECLARE(img_brightness);
#define APP_ID "brightness"
static int app_main(intent_t i) {
    gui_app_regist_msg_handler(APP_ID, msg_handler);

    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(brightness), LV_EXT_IMG_GET(img_brightness),
                   APP_ID, app_main);

static void lv_timer_callback(lv_timer_t* timer) {
    static char buf[48];
    static struct rt_sensor_data brightness_data;
    rt_int32_t lux_raw;
    rt_int32_t ema;
    rt_uint8_t raw_target;

    (void)timer;

    rt_device_read(g_brightness_sensor_dev, 0, &brightness_data, 1);
    lux_raw = brightness_data.data.light;
    rt_sprintf(buf, "brightness: %d lux", (int)lux_raw);
    if (g_label && lv_obj_is_valid(g_label)) lv_label_set_text(g_label, buf);

    if (!g_auto_brightness) {
        g_auto_lux_ema = -1;
        return;
    }

    if (!g_lcd_sensor_dev) return;

    if (g_auto_lux_filter_reset || g_auto_lux_ema < 0) {
        g_auto_lux_ema = lux_raw;
        g_auto_hyst_target = g_brightness_now;
        g_auto_lux_filter_reset = RT_FALSE;
    } else {
        ema = g_auto_lux_ema +
              ((lux_raw - g_auto_lux_ema) >> AUTO_LUX_EMA_SHIFT);
        g_auto_lux_ema = ema;
    }

    raw_target = lux_to_brightness_target(g_auto_lux_ema);
    g_auto_hyst_target =
        auto_target_apply_hysteresis(raw_target, g_auto_hyst_target);
    auto_brightness_ramp_step(g_auto_hyst_target);

    rt_device_control(g_lcd_sensor_dev, RTGRAPHIC_CTRL_SET_BRIGHTNESS,
                      &g_brightness_now);
    if (g_slider && lv_obj_is_valid(g_slider)) {
        lv_slider_set_value(g_slider, g_brightness_now, LV_ANIM_OFF);
        if (g_brightness_icon && lv_obj_is_valid(g_brightness_icon)) {
            int32_t value = (int32_t)g_brightness_now;
            int16_t angle = (int16_t)((value - BRIGHTNESS_MIN_VAL) * 360 /
                                      BRIGHTNESS_RANGE);
            lv_img_set_angle(g_brightness_icon, angle * 10);
        }
    }
}

static int brightness_timer_init(void) {
    rt_err_t ret = RT_EOK;
    g_brightness_sensor_dev = rt_device_find("li_ltr303");

    if (g_brightness_sensor_dev == RT_NULL) {
        LOG_E("find ltr303 sensor device failed!\n");
        return -RT_ERROR;
    }

    ret = rt_device_open(g_brightness_sensor_dev, RT_DEVICE_FLAG_RDONLY);
    if (ret != RT_EOK) {
        LOG_E("open g_brightness_sensor_dev failed! err: %d\n", ret);
        return -RT_ERROR;
    }

    g_lcd_sensor_dev = rt_device_find("lcd");
    if (g_lcd_sensor_dev == RT_NULL) {
        LOG_E("find lcd device failed!\n");
        return -RT_ERROR;
    }

    lv_timer_t* brightness_timer =
        lv_timer_create(lv_timer_callback, AUTO_TIMER_PERIOD_MS, NULL);
    lv_timer_set_repeat_count(brightness_timer, -1);
    return RT_EOK;
}

INIT_APP_EXPORT(brightness_timer_init);