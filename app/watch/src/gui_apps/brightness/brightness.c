/*********************
 *      INCLUDES
 *********************/

#include <rtthread.h>

#include "app_mem.h"
#include "brightness_manager.h"
#include "gui_app_fwk.h"
#include "littlevgl2rtt.h"
#include "lv_ex_data.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "lvsf_comp.h"

#define DBG_TAG "gui_apps.brightness"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BRIGHTNESS_RANGE (BRIGHTNESS_MAX_VAL - BRIGHTNESS_MIN_VAL)

LV_IMG_DECLARE(img_brightness_icon);
LV_IMG_DECLARE(img_auto_brightness_icon);

static lv_obj_t *g_label = NULL, *g_slider = NULL;
static lv_obj_t* g_brightness_icon = NULL;
static lv_obj_t *g_auto_brightness_icon = NULL, *g_auto_brightness_btn = NULL;
static rt_bool_t g_slider_syncing = RT_FALSE;

static void update_status_label(void) {
    static char buf[56];
    rt_int32_t lux;
    rt_uint8_t brightness;

    if (!g_label || !lv_obj_is_valid(g_label)) return;
    lux = brightness_manager_get_lux();
    brightness = brightness_manager_get_value();
    rt_snprintf(buf, sizeof(buf), "brightness: %d%% | ambient: %d lux",
                (int)brightness, (int)lux);
    lv_label_set_text(g_label, buf);
}

static void set_auto_button_style(rt_bool_t auto_enabled) {
    if (!g_auto_brightness_btn || !lv_obj_is_valid(g_auto_brightness_btn)) return;
    if (!g_auto_brightness_icon || !lv_obj_is_valid(g_auto_brightness_icon)) return;

    if (auto_enabled) {
        lv_obj_set_style_bg_color(g_auto_brightness_btn, lv_color_hex(0xFFFFFF),
                                  LV_STATE_DEFAULT);
        lv_obj_set_style_img_recolor(g_auto_brightness_icon, lv_color_hex(0x000000),
                                     LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_bg_color(g_auto_brightness_btn, lv_color_hex(0x333333),
                                  LV_STATE_DEFAULT);
        lv_obj_set_style_img_recolor(g_auto_brightness_icon, lv_color_hex(0xFFFFFF),
                                     LV_STATE_DEFAULT);
    }
}

static void brightness_on_changed(rt_uint8_t brightness, rt_bool_t auto_enabled,
                                  void* user_data) {
    int32_t value = (int32_t)brightness;
    int16_t angle =
        (int16_t)((value - BRIGHTNESS_MIN_VAL) * 360 / BRIGHTNESS_RANGE);
    (void)user_data;

    set_auto_button_style(auto_enabled);

    if (g_slider && lv_obj_is_valid(g_slider)) {
        g_slider_syncing = RT_TRUE;
        lv_slider_set_value(g_slider, value, LV_ANIM_OFF);
        g_slider_syncing = RT_FALSE;
    }
    if (g_brightness_icon && lv_obj_is_valid(g_brightness_icon)) {
        lv_img_set_angle(g_brightness_icon, angle * 10);
    }
    update_status_label();
}

static void brightness_on_lux_changed(rt_int32_t lux, void* user_data) {
    (void)lux;
    (void)user_data;
    update_status_label();
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
        rt_bool_t next = !brightness_manager_is_auto_enabled();
        brightness_manager_set_auto_enabled(next);
    }
}

static void brightness_slider_event_cb(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t value;

    if (g_slider_syncing) return;
    value = lv_slider_get_value(slider);
    brightness_manager_set_manual_brightness((rt_uint8_t)value);
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
    lv_slider_set_value(g_slider, brightness_manager_get_value(), LV_ANIM_ON);

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
    if (brightness_manager_init() != RT_EOK) {
        LOG_E("brightness manager init failed\n");
        return;
    }

    create_ui();
    brightness_manager_register_callback(brightness_on_changed, RT_NULL);
    brightness_manager_register_lux_callback(brightness_on_lux_changed, RT_NULL);
    brightness_on_changed(brightness_manager_get_value(),
                          brightness_manager_is_auto_enabled(), RT_NULL);
    brightness_on_lux_changed(brightness_manager_get_lux(), RT_NULL);
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
    brightness_manager_unregister_callback(brightness_on_changed, RT_NULL);
    brightness_manager_unregister_lux_callback(brightness_on_lux_changed,
                                               RT_NULL);
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