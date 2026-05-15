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

#define HUMITURE_AHT20_DEV "temp_aht20"

#define SCR_W 410
#define SCR_H 502

enum {
    COMFORT_COLD,
    COMFORT_COOL,
    COMFORT_COMFORTABLE,
    COMFORT_WARM,
    COMFORT_HOT,
};

LV_IMG_DECLARE(img_humiture_dress);
LV_IMG_DECLARE(img_humiture_tips);
LV_IMG_DECLARE(img_humiture_humi);

static float temp = 0.0f, humi = 0.0f;
static lv_obj_t *label_temp = NULL;
static lv_obj_t *label_temp_unit = NULL;
static lv_obj_t *label_humi = NULL;
static lv_obj_t *dot_comfort = NULL;
static lv_obj_t *label_comfort = NULL;
static lv_obj_t *label_comfort_desc = NULL;
static lv_obj_t *label_dress = NULL;
static lv_obj_t *label_tips = NULL;
static lv_obj_t *bar_comfort = NULL;
static lv_timer_t *frush_timer = NULL;
static rt_device_t aht_sensor_dev = RT_NULL;

static int get_comfort_level(float t, float h)
{
    if (t >= 18.0f && t <= 26.0f && h >= 40.0f && h <= 70.0f)
        return COMFORT_COMFORTABLE;
    if (t >= 26.0f && h >= 60.0f)
        return COMFORT_WARM;
    if (t > 32.0f)
        return COMFORT_HOT;
    if (t >= 26.0f)
        return COMFORT_WARM;
    if (t >= 10.0f)
        return COMFORT_COOL;
    return COMFORT_COLD;
}

static lv_color_t get_comfort_color(int level)
{
    switch (level) {
        case COMFORT_COLD:
            return lv_color_hex(0x4FC3F7);
        case COMFORT_COOL:
            return lv_color_hex(0x81D4FA);
        case COMFORT_COMFORTABLE:
            return lv_color_hex(0x66BB6A);
        case COMFORT_WARM:
            return lv_color_hex(0xFFA726);
        case COMFORT_HOT:
            return lv_color_hex(0xEF5350);
        default:
            return lv_color_hex(0xFFFFFF);
    }
}

static const char *get_comfort_text(int level)
{
    switch (level) {
        case COMFORT_COLD:
            return "寒冷";
        case COMFORT_COOL:
            return "凉爽";
        case COMFORT_COMFORTABLE:
            return "舒适";
        case COMFORT_WARM:
            return "温暖";
        case COMFORT_HOT:
            return "炎热";
        default:
            return "--";
    }
}

static const char *get_comfort_desc(int level, float h)
{
    switch (level) {
        case COMFORT_COLD:
            return "气温偏低，注意保暖防寒";
        case COMFORT_COOL:
            return h < 40.0f ? "空气干燥，注意补水"
                 : h > 70.0f ? "空气潮湿，注意防潮"
                 : "天气偏凉，体感舒适";
        case COMFORT_COMFORTABLE:
            return h < 40.0f ? "温湿度适宜，略显干燥"
                 : h > 70.0f ? "温湿度适宜，略有潮湿"
                 : "体感最佳，温湿度宜人";
        case COMFORT_WARM:
            return h > 60.0f ? "闷热潮湿，注意通风"
                 : "天气偏热，注意防晒";
        case COMFORT_HOT:
            return "高温天气，谨防中暑";
        default:
            return "";
    }
}

static const char *get_dress_advice(float t)
{
    if (t < 0.0f)
        return "厚羽绒服+围巾手套";
    if (t < 5.0f)
        return "羽绒服/厚棉衣";
    if (t < 12.0f)
        return "棉衣/呢大衣";
    if (t < 18.0f)
        return "薄外套/毛衣";
    if (t < 24.0f)
        return "长袖T恤/卫衣";
    if (t < 28.0f)
        return "短袖/薄衬衫";
    if (t < 33.0f)
        return "短袖短裤/裙子";
    return "轻薄透气夏装";
}

static const char *get_life_tips(float t, float h, int level)
{
    if (level == COMFORT_COMFORTABLE && h >= 40.0f && h <= 60.0f)
        return "天气宜人，适合户外运动";
    if (t < 5.0f)
        return "注意保暖，谨防感冒";
    if (t < 12.0f)
        return "室内外温差大，及时添衣";
    if (t >= 12.0f && t < 24.0f && h < 40.0f)
        return "空气干燥，多喝温水";
    if (t >= 24.0f && h > 65.0f)
        return "闷热潮湿，保持通风";
    if (t >= 33.0f)
        return "高温预警，减少户外活动";
    if (h > 75.0f)
        return "湿度较高，注意防潮除湿";
    if (h < 30.0f)
        return "空气干燥，注意加湿护肤";
    return "今日天气尚可，保持好心情";
}

static lv_obj_t *make_icon(lv_obj_t *parent, const lv_img_dsc_t *src,
                           lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *img = lv_img_create(parent);
    if (src)
        lv_img_set_src(img, src);
    lv_obj_align(img, LV_ALIGN_LEFT_MID, x, y);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    return img;
}

static lv_obj_t *make_dot(lv_obj_t *parent, lv_coord_t size, lv_color_t color,
                          lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, size, size);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(dot, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, x, y);
    return dot;
}

static void style_card(lv_obj_t *card, lv_coord_t y)
{
    lv_obj_set_size(card, SCR_W - 48, 78);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E1E2E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
}

static void timer_callback(lv_timer_t *timer)
{
    static char buf[64];
    struct rt_sensor_data d[2];
    int comfort_level;
    lv_color_t tc;

    (void)timer;
    if (aht_sensor_dev != RT_NULL)
    {
        if (rt_device_read(aht_sensor_dev, 0, &d[0], 2) >= 2)
        {
            if (d[0].type == RT_SENSOR_CLASS_TEMP)
                temp = (float)d[0].data.temp / 10.0f;
            if (d[1].type == RT_SENSOR_CLASS_HUMI)
                humi = (float)d[1].data.humi / 10.0f;
        }
    }

    comfort_level = get_comfort_level(temp, humi);
    tc = get_comfort_color(comfort_level);

    if (aht_sensor_dev != RT_NULL)
    {
        rt_snprintf(buf, sizeof(buf), "%.1f", temp);
        if (label_temp) lv_label_set_text(label_temp, buf);

        rt_snprintf(buf, sizeof(buf), "%.1f%%", humi);
        if (label_humi) lv_label_set_text(label_humi, buf);

        if (label_temp)
            lv_obj_set_style_text_color(label_temp, tc, LV_PART_MAIN | LV_STATE_DEFAULT);
        if (label_temp_unit)
            lv_obj_set_style_text_color(label_temp_unit, tc, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else
    {
        if (label_temp) lv_label_set_text(label_temp, "--");
        if (label_humi) lv_label_set_text(label_humi, "--%%");
    }

    if (dot_comfort)
        lv_obj_set_style_bg_color(dot_comfort, tc, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (label_comfort)
    {
        lv_label_set_text(label_comfort, get_comfort_text(comfort_level));
        lv_obj_set_style_text_color(label_comfort, tc, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (label_comfort_desc)
        lv_label_set_text(label_comfort_desc, get_comfort_desc(comfort_level, humi));

    if (bar_comfort)
    {
        lv_obj_set_style_bg_color(bar_comfort, tc, LV_PART_MAIN | LV_STATE_DEFAULT);
        float ratio;
        if (temp < 0.0f) ratio = 0.05f;
        else if (temp > 40.0f) ratio = 1.0f;
        else ratio = temp / 40.0f;
        lv_coord_t w = (lv_coord_t)(ratio * (lv_coord_t)(SCR_W - 48 - 24));
        if (w < 8) w = 8;
        lv_obj_set_width(bar_comfort, w);
    }

    if (label_dress)
        lv_label_set_text(label_dress, get_dress_advice(temp));

    if (label_tips)
        lv_label_set_text(label_tips, get_life_tips(temp, humi, comfort_level));
}

static void create_ui(void)
{
    lv_obj_t *screen = lv_scr_act();
    // lv_obj_set_style_bg_color(screen, lv_color_hex(0x121218), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    label_temp = lv_label_create(screen);
    lv_ext_set_local_font(label_temp, FONT_SUPER, lv_color_hex(0xFFFFFF));
    lv_label_set_text(label_temp, "--.-");
    lv_obj_align(label_temp, LV_ALIGN_TOP_MID, -14, 18);

    label_temp_unit = lv_label_create(screen);
    lv_ext_set_local_font(label_temp_unit, FONT_TITLE, lv_color_hex(0xFFFFFF));
    lv_label_set_text(label_temp_unit, "℃");
    lv_obj_align_to(label_temp_unit, label_temp, LV_ALIGN_OUT_RIGHT_TOP, 14, 18);

    lv_obj_t *humi_row = lv_obj_create(screen);
    lv_obj_set_size(humi_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(humi_row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(humi_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(humi_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(humi_row, label_temp_unit, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, 0);
    lv_obj_clear_flag(humi_row, LV_OBJ_FLAG_SCROLLABLE);

    make_icon(humi_row, &img_humiture_humi, 0, 0);

    label_humi = lv_label_create(humi_row);
    lv_ext_set_local_font(label_humi, FONT_NORMAL, lv_color_hex(0x8899AA));
    lv_label_set_text(label_humi, "--%%");
    lv_obj_align_to(label_humi, lv_obj_get_child(humi_row, 0), LV_ALIGN_OUT_RIGHT_MID, 4, 0);

    lv_obj_t *div_top = lv_obj_create(screen);
    lv_obj_set_size(div_top, SCR_W - 48, 1);
    lv_obj_align(div_top, LV_ALIGN_TOP_MID, 0, 136);
    lv_obj_set_style_bg_color(div_top, lv_color_hex(0x2A2A3E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(div_top, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(div_top, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *comfort_row = lv_obj_create(screen);
    lv_obj_set_size(comfort_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(comfort_row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(comfort_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(comfort_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(comfort_row, LV_ALIGN_TOP_MID, 0, 148);
    lv_obj_clear_flag(comfort_row, LV_OBJ_FLAG_SCROLLABLE);

    dot_comfort = make_dot(comfort_row, 12, lv_color_hex(0x66BB6A), 0, 0);

    label_comfort = lv_label_create(comfort_row);
    lv_ext_set_local_font(label_comfort, FONT_TITLE, lv_color_hex(0x66BB6A));
    lv_label_set_text(label_comfort, "--");
    lv_obj_align_to(label_comfort, dot_comfort, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

    label_comfort_desc = lv_label_create(screen);
    lv_ext_set_local_font(label_comfort_desc, FONT_SMALL, lv_color_hex(0x8899AA));
    lv_label_set_text(label_comfort_desc, "");
    lv_obj_align(label_comfort_desc, LV_ALIGN_TOP_MID, 0, 182);

    lv_obj_t *bar_bg = lv_obj_create(screen);
    lv_obj_set_size(bar_bg, SCR_W - 48, 6);
    lv_obj_align(bar_bg, LV_ALIGN_TOP_MID, 0, 208);
    lv_obj_set_style_bg_color(bar_bg, lv_color_hex(0x2A2A3E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar_bg, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar_bg, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar_bg, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(bar_bg, LV_OBJ_FLAG_SCROLLABLE);

    bar_comfort = lv_obj_create(bar_bg);
    lv_obj_set_height(bar_comfort, 6);
    lv_obj_set_width(bar_comfort, 40);
    lv_obj_align(bar_comfort, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(bar_comfort, lv_color_hex(0x66BB6A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar_comfort, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar_comfort, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar_comfort, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(bar_comfort, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *div_mid = lv_obj_create(screen);
    lv_obj_set_size(div_mid, SCR_W - 48, 1);
    lv_obj_align(div_mid, LV_ALIGN_TOP_MID, 0, 232);
    lv_obj_set_style_bg_color(div_mid, lv_color_hex(0x2A2A3E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(div_mid, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(div_mid, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *card_dress = lv_obj_create(screen);
    style_card(card_dress, 248);

    make_icon(card_dress, &img_humiture_dress, 0, -10);

    lv_obj_t *dress_title = lv_label_create(card_dress);
    lv_ext_set_local_font(dress_title, FONT_SMALL, lv_color_hex(0x666688));
    lv_label_set_text(dress_title, "穿衣指南");
    lv_obj_align_to(dress_title, lv_obj_get_child(card_dress, 0), LV_ALIGN_OUT_RIGHT_TOP, 10, -2);

    label_dress = lv_label_create(card_dress);
    lv_ext_set_local_font(label_dress, FONT_NORMAL, lv_color_hex(0xCCCCDD));
    lv_label_set_text(label_dress, "--");
    lv_obj_align_to(label_dress, dress_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    lv_obj_t *card_tips = lv_obj_create(screen);
    style_card(card_tips, 340);

    make_icon(card_tips, &img_humiture_tips, 0, -10);

    lv_obj_t *tips_title = lv_label_create(card_tips);
    lv_ext_set_local_font(tips_title, FONT_SMALL, lv_color_hex(0x666688));
    lv_label_set_text(tips_title, "生活建议");
    lv_obj_align_to(tips_title, lv_obj_get_child(card_tips, 0), LV_ALIGN_OUT_RIGHT_TOP, 10, -2);

    label_tips = lv_label_create(card_tips);
    lv_ext_set_local_font(label_tips, FONT_NORMAL, lv_color_hex(0xCCCCDD));
    lv_label_set_text(label_tips, "--");
    lv_obj_align_to(label_tips, tips_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    frush_timer = lv_timer_create(timer_callback, 1000, NULL);
    lv_timer_set_repeat_count(frush_timer, -1);
}

static void on_start(void)
{
    if (aht_sensor_dev == RT_NULL)
    {
        aht_sensor_dev = rt_device_find(HUMITURE_AHT20_DEV);
        if (aht_sensor_dev != RT_NULL)
        {
            if (rt_device_open(aht_sensor_dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK)
                aht_sensor_dev = RT_NULL;
        }
    }

    create_ui();

    lv_img_cache_invalidate_src(NULL);
}

static void on_pause(void)
{
    if (frush_timer) {
        lv_timer_del(frush_timer);
        frush_timer = NULL;
    }
    if (aht_sensor_dev != RT_NULL)
        rt_device_close(aht_sensor_dev);
}

static void on_resume(void)
{
    if (aht_sensor_dev != RT_NULL)
        rt_device_open(aht_sensor_dev, RT_DEVICE_FLAG_RDONLY);
    if (frush_timer == NULL) {
        frush_timer = lv_timer_create(timer_callback, 1000, NULL);
        lv_timer_set_repeat_count(frush_timer, -1);
    }
}

static void on_stop(void)
{
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

static void msg_handler(gui_app_msg_type_t msg, void *param)
{
    (void)param;
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
static int app_main(intent_t i)
{
    (void)i;
    gui_app_regist_msg_handler(APP_ID, msg_handler);

    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(humiture), LV_EXT_IMG_GET(img_humiture),
                   APP_ID, app_main);
