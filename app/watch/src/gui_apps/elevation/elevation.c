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
#include "sensor.h"

#define DBG_TAG "gui_apps.elevation"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

rt_device_t baro_sensor_dev = NULL;
static struct rt_sensor_data baro_data;
static lv_obj_t* label = NULL;
static lv_timer_t* frush_timer = NULL;

static void timer_callback(lv_timer_t* timer) {
    static char buf[32];
    if (baro_sensor_dev) {
        rt_device_read(baro_sensor_dev, 0, &baro_data, 1);
        rt_sprintf(buf, "barometric pressure: %d pa", baro_data.data.baro);
    }else{
        rt_sprintf(buf, "barometric pressure: N/A");
    }
    if (label) lv_label_set_text(label, buf);
}

static void create_ui(void) {
    lv_obj_t* screen = lv_scr_act();

    label = lv_label_create(lv_scr_act());
    lv_obj_center(label);

    frush_timer = lv_timer_create(timer_callback, 1000, NULL);
    lv_timer_set_repeat_count(frush_timer, -1);
}

static void on_start(void) {
    rt_err_t ret = RT_EOK;
    baro_sensor_dev = rt_device_find("baro_spl06");

    if (baro_sensor_dev == RT_NULL) {
        LOG_E("find spl06 sensor device failed!\n");
        return;
    }

    ret = rt_device_open(baro_sensor_dev, RT_DEVICE_FLAG_RDONLY);
    if (ret != RT_EOK) {
        LOG_E("open baro_sensor_dev failed! err: %d\n", ret);
        return;
    }

    create_ui();

    lv_img_cache_invalidate_src(NULL);
}

static void on_pause(void) {
    if (frush_timer) {
        lv_timer_del(frush_timer);
        frush_timer = NULL;
    }
}

static void on_resume(void) {
    if (frush_timer == NULL) {
        frush_timer = lv_timer_create(timer_callback, 1000, NULL);
        lv_timer_set_repeat_count(frush_timer, -1);
    }
}

static void on_stop(void) {
    if (frush_timer) {
        lv_timer_del(frush_timer);
        frush_timer = NULL;
    }
    if (baro_sensor_dev) {
        rt_device_close(baro_sensor_dev);
        baro_sensor_dev = NULL;
    }
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

LV_IMG_DECLARE(img_elevation);
#define APP_ID "elevation"
static int app_main(intent_t i) {
    gui_app_regist_msg_handler(APP_ID, msg_handler);

    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(elevation), LV_EXT_IMG_GET(img_elevation),
                   APP_ID, app_main);
