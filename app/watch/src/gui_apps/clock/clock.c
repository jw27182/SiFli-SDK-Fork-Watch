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
#include "screens.h"
#include "images.h"
#include "string.h"
#include "time_manager.h"
#include "um_gps_if.h"
#include "battery_calculator.h"

#define DBG_TAG "gui_apps.clock"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define TIME_SCALE 200 // 200ms检查一次RTC

extern battery_calculator_t battery_calc;
static lv_timer_t *frush_timer = NULL;
static rt_device_t battery_device;
static char buf[16];
static dm_date_time_t dt_last = {0};
static const char *week_day_str[] = {"SUN", "MON", "TUE", "WED",
                                     "THU", "FRI", "SAT"};

/* ================= 星期计算 ================= */

int get_weekday(int year, int month, int day)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3)
        year -= 1;
    return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}

/* ================= 秒针动画 ================= */

static void sec_anim_cb(void *img, int32_t v)
{
    lv_img_set_angle((lv_obj_t *)img, v);
}

static void start_second_anim(uint8_t sec)
{
    lv_anim_t a;
    lv_anim_init(&a);

    uint16_t start = sec * 60;
    uint16_t end = (sec + 1) * 60;

    lv_anim_set_var(&a, objects.img_corona_second);
    lv_anim_set_exec_cb(&a, sec_anim_cb);
    lv_anim_set_time(&a, 1000); // 1秒
    lv_anim_set_values(&a, start, end);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_repeat_count(&a, 0);

    lv_anim_start(&a);
}

static void frush_time()
{
    dm_date_time_t dt_now = {0};

    dm_get_date_time(&dt_now);

    /* 秒变化 */
    if (dt_now.second != dt_last.second)
    {
        start_second_anim(dt_now.second);

        dt_last.second = dt_now.second;
    }

    if (dt_now.second % 10 == 0)
        lv_img_set_angle(objects.img_corona_minute,
                         dt_now.minute * 60 + dt_now.second);

    /* 分钟变化 */
    if (dt_now.minute != dt_last.minute)
    {
        rt_sprintf(buf, "%02d", dt_now.minute);
        lv_label_set_text(objects.label_minute, buf);

        dt_last.minute = dt_now.minute;
    }

    /* 小时变化 */
    if (dt_now.hour != dt_last.hour)
    {
        rt_sprintf(buf, "%02d", dt_now.hour);
        lv_label_set_text(objects.label_hour, buf);

        dt_last.hour = dt_now.hour;
    }

    /* 日期变化 */
    if (dt_now.day != dt_last.day || dt_now.month != dt_last.month ||
        dt_now.year != dt_last.year)
    {
        lv_label_set_text(
            objects.label_week,
            week_day_str[get_weekday(dt_now.year, dt_now.month, dt_now.day)]);

        rt_sprintf(buf, "%02d/%02d", dt_now.month, dt_now.day);

        lv_label_set_text(objects.label_date, buf);

        dt_last.day = dt_now.day;
        dt_last.month = dt_now.month;
        dt_last.year = dt_now.year;
    }
}

static void frush_battery_status()
{
    static rt_adc_cmd_read_arg_t read_arg;
    read_arg.channel = 7;
    rt_adc_enable((rt_adc_device_t)battery_device, read_arg.channel);
    rt_uint32_t voltage =
        rt_adc_read((rt_adc_device_t)battery_device, read_arg.channel);
    rt_adc_disable((rt_adc_device_t)battery_device, read_arg.channel);
    uint8_t percentage = battery_calculator_get_percent(&battery_calc, voltage);
    lv_arc_set_value(objects.arc_battery_val, percentage);
    LOG_I("get battery adc value:%d, percentage:%d, Current status:%s", voltage,
          percentage, "Discharging");

    lv_label_set_text_fmt(objects.label_battery_val, "%d%%",percentage);

    if (battery_get_charging_status() == BATTERY_CHARGER_STATUS_CHARGING)
        lv_img_set_src(objects.img_battery_icon, &img_img_battery_charge);
    else
        lv_img_set_src(objects.img_battery_icon, &img_img_battery_icon);
}

static void frush_gps_status()
{
}

/* ================= 主刷新逻辑 ================= */

static void frush_clock_ui_cb(struct _lv_timer_t *t)
{
    frush_time();
    frush_battery_status();
    frush_gps_status();
}

/* ================= UI初始化 ================= */

static void start_frush_timer(void)
{
    memset(&dt_last, 0, sizeof(dt_last));
    frush_timer = lv_timer_create(frush_clock_ui_cb, TIME_SCALE, NULL);

    lv_timer_set_repeat_count(frush_timer, -1);

    /* 测试设置时间（可删除） */
    // dm_date_time_t dt_set = {2026, 2, 26, 17, 56, 0};
    // rt_err_t ret = dm_set_date_time(dt_set);
    // RT_ASSERT(ret == RT_EOK);
}

/* ================= 生命周期 ================= */
extern const lv_img_dsc_t img_img_corona_second;
static void on_start(void)
{
    static GpsData gps_data;
    if (um_gps_get_data(NULL, NULL, NULL, &gps_data) == 0)
    {
        dm_date_time_t dt_set = {gps_data.location_data.timestamp.tm_year,
                                 gps_data.location_data.timestamp.tm_mon,
                                 gps_data.location_data.timestamp.tm_mday,
                                 gps_data.location_data.timestamp.tm_hour,
                                 gps_data.location_data.timestamp.tm_min,
                                 gps_data.location_data.timestamp.tm_sec};
        rt_err_t ret = dm_set_date_time(dt_set);
        RT_ASSERT(ret == RT_EOK);
    }
    battery_device = rt_device_find("bat1");

    create_screen_clock();
    start_frush_timer();
    lv_arc_set_value(objects.arc_gps_status, gps_data.sv_data.num_svs * 5);

    lv_img_set_src(objects.img_corona_second,
                   app_cache_copy_alloc(&img_img_corona_second, ROTATE_MEM));
    lv_img_cache_invalidate_src(NULL);
}

static void on_pause(void)
{
    if (frush_timer)
    {
        lv_timer_del(frush_timer);
        frush_timer = NULL;
    }
}

static void on_resume(void)
{
    if (frush_timer == NULL)
    {
        start_frush_timer();
    }
}

static void on_stop(void)
{
    if (frush_timer)
    {
        lv_timer_del(frush_timer);
        frush_timer = NULL;
    }

    lv_img_dsc_t *cur =
        (lv_img_dsc_t *)lv_img_get_src(objects.img_corona_second);
    if (cur && cur != &img_img_corona_second)
    {
        app_cache_copy_free(cur);
        lv_img_set_src(objects.img_corona_second, &img_img_corona_second);
    }
}

/* ================= 消息处理 ================= */

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

LV_IMG_DECLARE(img_clock);

#define APP_ID "clock"

static int app_main(intent_t i)
{
    gui_app_regist_msg_handler(APP_ID, msg_handler);
    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(clock), LV_EXT_IMG_GET(img_clock), APP_ID,
                   app_main);
