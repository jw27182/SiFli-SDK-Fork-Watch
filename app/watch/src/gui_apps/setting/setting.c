/*********************
 *      INCLUDES
 *********************/

#include <rtdevice.h>
#include <rtthread.h>

#include "app_mem.h"
#include "gui_app_fwk.h"
#include "littlevgl2rtt.h"
#include "lv_ex_data.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "lvsf_comp.h"

#define DBG_TAG "gui_apps.setting"

static struct rt_device_pwm *pwm2;
static const rt_uint32_t pwm_period_ns = 10000;
static int pwm_ch = 1;
static int pin_id = 1;
static int pin_state = 0;

static void empty_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        rt_pin_write(pin_id, 1);
    }else if (code == LV_EVENT_RELEASED) {
        rt_pin_write(pin_id, 0);
    }
}

static void slider_evt(lv_event_t *e)
{
    if (!pwm2) return;
    int v = lv_slider_get_value(lv_event_get_target(e));
    rt_uint32_t pulse = pwm_period_ns * v / 100;
    rt_pwm_set(pwm2, pwm_ch, pwm_period_ns, pulse);
}

static void btn_toggle_evt(lv_event_t *e)
{
    pin_state = !pin_state;
    rt_pin_write(pin_id, pin_state);
}

static void create_ui(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_size(slider, 200, 10);
    lv_obj_center(slider);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_evt, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *btn1 = lv_btn_create(scr);
    lv_obj_set_size(btn1, 100, 100);
    lv_obj_align(btn1, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_add_event_cb(btn1, btn_toggle_evt, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn2 = lv_btn_create(scr);
    lv_obj_set_size(btn2, 100, 100);
    lv_obj_align(btn2, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_obj_add_event_cb(btn2, empty_cb, LV_EVENT_ALL, NULL);
}

static void on_start(void) {
    create_ui();

    lv_img_cache_invalidate_src(NULL);
    pwm2 = (struct rt_device_pwm *)rt_device_find("pwm2");
    // if (pwm2)
    // {
    //     rt_pwm_set(pwm2, pwm_ch, pwm_period_ns, pwm_period_ns / 2);
    //     rt_pwm_enable(pwm2, pwm_ch);
    // }
    // rt_pin_mode(pin_id, PIN_MODE_OUTPUT);
    // rt_pin_write(pin_id, pin_state);
}

static void on_pause(void) {
}

static void on_resume(void) {
}

static void on_stop(void) {
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

LV_IMG_DECLARE(img_setting);
#define APP_ID "setting"
static int app_main(intent_t i) {
    gui_app_regist_msg_handler(APP_ID, msg_handler);

    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(setting), LV_EXT_IMG_GET(img_setting),
                   APP_ID, app_main);
