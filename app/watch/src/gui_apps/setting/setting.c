/*********************
 *      INCLUDES
 *********************/

#include <rtdevice.h>
#include <rtthread.h>

#include "app_mem.h"
#include "brightness_manager.h"
#include "charge_manager.h"
#include "gnss_manager.h"
#include "gui_app_fwk.h"
#include "littlevgl2rtt.h"
#include "lv_ex_data.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "lvsf_comp.h"
#include "my_widget.h"
#include "screens.h"
#include "actions.h"
#include "images.h"

#define DBG_TAG "gui_apps.setting"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static rt_bool_t g_setting_slider_syncing = RT_FALSE;
#define SETTING_DEV_PAGE_ID "setting_dev"

static void setting_sync_utc_time_success_cb(const dm_date_time_t *utc_time,
                                             void *user_data) {
    char text[64];
    (void)user_data;

    if (!utc_time) {
        myui_toast_show(MYUI_TOAST_TYPE_TIP, "GNSS时间同步成功");
        return;
    }

    rt_snprintf(text, sizeof(text), "同步成功 %04d-%02d-%02d %02d:%02d:%02d",
                utc_time->year, utc_time->month, utc_time->day, utc_time->hour,
                utc_time->minute, utc_time->second);
    myui_toast_show(MYUI_TOAST_TYPE_TIP, text);
}

static void setting_sync_utc_time_fail_cb(rt_err_t err, void *user_data) {
    (void)user_data;

    if (err == -RT_ETIMEOUT) {
        myui_toast_show(MYUI_TOAST_TYPE_WARNING, "GNSS时间同步超时");
    } else if (err == -RT_ENOSYS) {
        myui_toast_show(MYUI_TOAST_TYPE_WARNING, "当前版本未启用GNSS");
    } else {
        myui_toast_show(MYUI_TOAST_TYPE_WARNING, "GNSS时间同步失败");
    }
}

static void setting_dev_page_msg_handler(gui_app_msg_type_t msg, void *param) {
    (void)param;

    switch (msg) {
        case GUI_APP_MSG_ONSTART:
        case GUI_APP_MSG_ONRESUME:
            create_screen_developer_mode();
            break;
        case GUI_APP_MSG_ONSTOP:
            if (objects.developer_mode && lv_obj_is_valid(objects.developer_mode)) {
                lv_obj_del(objects.developer_mode);
            }
            objects.developer_mode = NULL;
            break;
        default:
            break;
    }
}

static void setting_charge_on_changed(rt_uint16_t current_ma, void *user_data) {
    char text[40];
    (void)user_data;

    if (objects.label_charge_current && lv_obj_is_valid(objects.label_charge_current)) {
        rt_snprintf(text, sizeof(text), "预设充电电流 | %dmA", (int)current_ma);
        lv_label_set_text(objects.label_charge_current, text);
    }

    if (objects.bar_charge_current && lv_obj_is_valid(objects.bar_charge_current)) {
        lv_bar_set_value(objects.bar_charge_current, current_ma, LV_ANIM_OFF);
    }
}

static void on_setting_slider_value_changed(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value;

    if (g_setting_slider_syncing) return;
    value = lv_slider_get_value(slider);
    brightness_manager_set_manual_brightness((rt_uint8_t)value);
}

static void setting_brightness_on_changed(rt_uint8_t brightness,
                                          rt_bool_t auto_enabled,
                                          void *user_data) {
    char text[24];
    (void)user_data;

    if (objects.label_current_brightness && lv_obj_is_valid(objects.label_current_brightness)) {
        rt_snprintf(text, sizeof(text), "亮度 | %d%%", (int)brightness);
        lv_label_set_text(objects.label_current_brightness, text);
    }

    if (objects.slider_brightness && lv_obj_is_valid(objects.slider_brightness)) {
        g_setting_slider_syncing = RT_TRUE;
        lv_slider_set_value(objects.slider_brightness, brightness, LV_ANIM_OFF);
        g_setting_slider_syncing = RT_FALSE;
    }

    if (objects.switch_auto_brightness && lv_obj_is_valid(objects.switch_auto_brightness)) {
        if (auto_enabled) {
            lv_obj_add_state(objects.switch_auto_brightness, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(objects.switch_auto_brightness, LV_STATE_CHECKED);
        }
    }
}

void action_on_shutdown_clicked(lv_event_t *e)
{
    (void)e;
    if (charge_manager_set_ship_mode(RT_TRUE) != RT_EOK) {
        LOG_E("set ship mode failed");
    }
}

void action_on_sync_utc_time_clicked(lv_event_t *e)
{
    int ret;
    (void)e;

    ret = gnss_manager_sync_utc_to_rtc_async(setting_sync_utc_time_success_cb,
                                             setting_sync_utc_time_fail_cb,
                                             RT_NULL);
    if (ret == -RT_EBUSY) {
        myui_toast_show(MYUI_TOAST_TYPE_TIP, "GNSS正在同步，请稍候");
    } else if (ret != RT_EOK) {
        myui_toast_show(MYUI_TOAST_TYPE_WARNING, "启动GNSS同步失败");
    } else {
        myui_toast_show(MYUI_TOAST_TYPE_TIP, "开始同步GNSS时间");
    }
}

void action_on_auto_brightness_clicked(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    rt_bool_t checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    brightness_manager_set_auto_enabled(checked);
}
void action_on_brightness_add_clicked(lv_event_t *e)
{
    (void)e;
    brightness_manager_adjust_manual_brightness(5);
}
void action_on_brightness_subtract_clicked(lv_event_t *e)
{
    (void)e;
    brightness_manager_adjust_manual_brightness(-5);
}

void action_on_charge_current_add_clicked(lv_event_t *e)
{
    (void)e;
    charge_manager_adjust_current(CHARGE_MANAGER_CURRENT_STEP_MA);
}
void action_on_charge_current_subtract_clicked(lv_event_t *e)
{
    (void)e;
    charge_manager_adjust_current(-CHARGE_MANAGER_CURRENT_STEP_MA);
}

void action_on_developer_mode_clicked(lv_event_t * e)
{
    (void)e;
    if (gui_app_create_page(SETTING_DEV_PAGE_ID, setting_dev_page_msg_handler) < 0) {
        LOG_E("open developer page failed");
    }
}
void action_on_developer_mode_show_toast_tip(lv_event_t * e)
{
    (void)e;
    myui_toast_show(MYUI_TOAST_TYPE_TIP, "Developer mode show toast tip");
}
void action_on_developer_mode_show_toast_warning(lv_event_t * e)
{
    (void)e;
    myui_toast_show(MYUI_TOAST_TYPE_WARNING, "Developer mode show toast warning");
}

static void create_ui(void)
{
    create_screen_setting();
}

static void on_start(void)
{
    create_ui();
    brightness_manager_init();

    if (objects.slider_brightness && lv_obj_is_valid(objects.slider_brightness)) {
        lv_slider_set_range(objects.slider_brightness, BRIGHTNESS_MIN_VAL,
                            BRIGHTNESS_MAX_VAL);
        lv_obj_add_event_cb(objects.slider_brightness, on_setting_slider_value_changed,
                            LV_EVENT_VALUE_CHANGED, NULL);
    }
    if (objects.bar_charge_current &&
        lv_obj_is_valid(objects.bar_charge_current)) {
        lv_bar_set_range(objects.bar_charge_current, CHARGE_MANAGER_CURRENT_MIN_MA,
                         CHARGE_MANAGER_CURRENT_MAX_MA);
    }
    brightness_manager_register_callback(setting_brightness_on_changed, RT_NULL);
    charge_manager_register_callback(setting_charge_on_changed, RT_NULL);
    setting_brightness_on_changed(brightness_manager_get_value(),
                                  brightness_manager_is_auto_enabled(),
                                  RT_NULL);
    setting_charge_on_changed(charge_manager_get_current_ma(), RT_NULL);

    lv_img_cache_invalidate_src(NULL);
}

static void on_pause(void)
{
}

static void on_resume(void)
{
}

static void on_stop(void)
{
    brightness_manager_unregister_callback(setting_brightness_on_changed, RT_NULL);
    charge_manager_unregister_callback(setting_charge_on_changed, RT_NULL);
}

static void msg_handler(gui_app_msg_type_t msg, void *param)
{
    switch (msg)
    {
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
static int app_main(intent_t i)
{
    gui_app_regist_msg_handler(APP_ID, msg_handler);

    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(setting), LV_EXT_IMG_GET(img_setting), APP_ID,
                   app_main);
