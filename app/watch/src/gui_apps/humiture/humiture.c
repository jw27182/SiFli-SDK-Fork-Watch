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

/* rt_hw_sensor_register(name "aht20", type TEMP) -> "temp_" + name; may truncate to RT_NAME_MAX */
#define HUMITURE_AHT20_DEV "temp_aht20"

static float temp = 0.0f, humi = 0.0f;
static lv_obj_t* label = NULL;
static lv_timer_t* frush_timer = NULL;
static rt_device_t aht_sensor_dev = RT_NULL;

static void timer_callback(lv_timer_t* timer) {
    static char buf[48];
    struct rt_sensor_data d[2];

    (void)timer;
    if (aht_sensor_dev != RT_NULL)
    {
        if (rt_device_read(aht_sensor_dev, 0, &d[0], 2) >= 2)
        {
            if (d[0].type == RT_SENSOR_CLASS_TEMP)
            {
                temp = (float)d[0].data.temp / 10.0f;
            }
            if (d[1].type == RT_SENSOR_CLASS_HUMI)
            {
                humi = (float)d[1].data.humi / 10.0f;
            }
        }
        rt_snprintf(buf, sizeof(buf), "温度：%.1fC  湿度：%.1f%%", temp, humi);
    }
    else
    {
        rt_snprintf(buf, sizeof(buf), "未找到传感器");
    }
    if (label) lv_label_set_text(label, buf);
}

static void create_ui(void) {
    lv_obj_t* screen = lv_scr_act();

    label = lv_label_create(lv_scr_act());
    lv_ext_set_local_font(label, FONT_NORMAL, lv_color_hex(0xffffff));
    lv_obj_center(label);

    frush_timer = lv_timer_create(timer_callback, 1000, NULL);
    lv_timer_set_repeat_count(frush_timer, -1);
}

static void on_start(void) {
    if (aht_sensor_dev == RT_NULL)
    {
        aht_sensor_dev = rt_device_find(HUMITURE_AHT20_DEV);
        if (aht_sensor_dev != RT_NULL)
        {
            if (rt_device_open(aht_sensor_dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK)
            {
                aht_sensor_dev = RT_NULL;
            }
        }
    }

    create_ui();

    lv_img_cache_invalidate_src(NULL);
}

static void on_pause(void) {
    if (frush_timer) {
        lv_timer_del(frush_timer);
        frush_timer = NULL;
    }
    if (aht_sensor_dev != RT_NULL)
    {
        rt_device_close(aht_sensor_dev);
    }
}

static void on_resume(void) {
    if (aht_sensor_dev != RT_NULL)
    {
        rt_device_open(aht_sensor_dev, RT_DEVICE_FLAG_RDONLY);
    }
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
    if (aht_sensor_dev != RT_NULL)
    {
        rt_device_close(aht_sensor_dev);
        aht_sensor_dev = RT_NULL;
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

LV_IMG_DECLARE(img_humiture);
#define APP_ID "humiture"
static int app_main(intent_t i) {
    gui_app_regist_msg_handler(APP_ID, msg_handler);

    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(humiture), LV_EXT_IMG_GET(img_humiture),
                   APP_ID, app_main);
