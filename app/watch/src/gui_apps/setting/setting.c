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
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

extern void aw32001_charge_enable(uint8_t en);
extern rt_err_t aw32001_set_charge_current(uint16_t set_chg);

lv_obj_t *label;
lv_obj_t *charge_label;

static bool pin_state = false;

static void slider_evt(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    aw32001_set_charge_current(v);
    lv_label_set_text_fmt(label, "%dmA ", v);
}

static void btn_toggle_evt(lv_event_t *e)
{
    pin_state = !pin_state;
    aw32001_charge_enable(pin_state);
    lv_label_set_text(charge_label, pin_state ? "charge enable" : "charge disable");
    LOG_I("charge %s", pin_state ? "enable" : "disable");
}

static void create_ui(void) {
    lv_obj_t *scr = lv_scr_act();
    
    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_size(slider, 200, 10);
    lv_obj_center(slider);
    lv_slider_set_range(slider, 2, 500);
    lv_slider_set_value(slider, 2, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_evt, LV_EVENT_VALUE_CHANGED, NULL);

    //‘Ÿœ‘ æ≥‰µÁ◊¥Ã¨
    label = lv_label_create(scr);
    lv_label_set_text(label, "2mA");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -100);

    charge_label = lv_label_create(scr);
    lv_label_set_text(charge_label, "charge");
    lv_obj_align(charge_label, LV_ALIGN_CENTER, 0, -200);

    lv_obj_t *btn1 = lv_btn_create(scr);
    lv_obj_set_size(btn1, 100, 100);
    lv_obj_align(btn1, LV_ALIGN_CENTER, 0, 100);
    lv_obj_add_event_cb(btn1, btn_toggle_evt, LV_EVENT_CLICKED, NULL);
}

static void on_start(void) {
    create_ui();

    lv_img_cache_invalidate_src(NULL);
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
