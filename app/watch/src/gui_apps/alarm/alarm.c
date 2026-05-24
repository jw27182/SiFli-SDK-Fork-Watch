/*********************
 *      INCLUDES
 *********************/
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include "app_mem.h"
#include "gui_app_fwk.h"
#include "littlevgl2rtt.h"
#include "lv_ex_data.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "lvsf_comp.h"
#include "alarm_storage.h"
#include "vibrator_manager.h"

#define SCR_W 410
#define SCR_H 502

/* 重复天数名称 */
static const char *day_names[] = {
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

static lv_obj_t *list_cnt = NULL;
static lv_style_t style_btn;
static uint8_t alarm_num = 0;

/* 前向声明 */
extern void app_alarm_edit(uint32_t alarm_idx);
static void list_btn_event_cb(lv_event_t *e);
static void add_btn_event_cb(lv_event_t *e);

static const char *get_repeat_text(uint8_t days)
{
    static char buf[64];
    if (days == ALARM_REPEAT_ONE_SHOT)
        return "Once";
    if (days == ALARM_REPEAT_EVERYDAY)
        return "Everyday";
    if (days == ALARM_REPEAT_WORKDAY)
        return "Workday";
    if (days == ALARM_REPEAT_WEEKEND)
        return "Weekend";

    buf[0] = '\0';
    for (int i = 0; i < 7; i++) {
        if (days & (1 << i)) {
            if (buf[0] != '\0')
                strcat(buf, " ");
            strcat(buf, day_names[i]);
        }
    }
    return buf;
}

static void rebuild_list(void)
{
    /* 清空列表 */
    if (list_cnt) {
        lv_obj_clean(list_cnt);
    }

    alarm_db_t db;
    alarm_storage_load(&db);
    alarm_num = db.count;

    for (uint8_t i = 0; i < db.count; i++) {
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%d:%02d",
                 db.alarms[i].hour, db.alarms[i].minute);

        lv_obj_t *list_btn = lv_btn_create(list_cnt);
        lv_obj_set_size(list_btn, LV_PCT(100), 70);
        lv_obj_add_style(list_btn, &style_btn, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_user_data(list_btn, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(list_btn, list_btn_event_cb, LV_EVENT_ALL, NULL);

        /* 选中状态 = 启用 */
        if (db.alarms[i].state == ALARM_STATE_ENABLE)
            lv_obj_add_state(list_btn, LV_STATE_CHECKED);

        /* 时间文本 */
        lv_obj_t *time_label = lv_label_create(list_btn);
        lv_ext_set_local_font(time_label, FONT_TITLE, lv_color_hex(0xFFFFFF));
        lv_label_set_text(time_label, time_str);
        lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 16, 0);

        /* 重复文本 */
        lv_obj_t *repeat_label = lv_label_create(list_btn);
        lv_ext_set_local_font(repeat_label, FONT_SMALL, lv_color_hex(0x888888));
        lv_label_set_text(repeat_label, get_repeat_text(db.alarms[i].repeat_days));
        lv_obj_align_to(repeat_label, time_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    }

    /* 添加按钮 */
    if (alarm_num < ALARM_MAX_COUNT) {
        lv_obj_t *add_btn = lv_btn_create(list_cnt);
        lv_obj_set_size(add_btn, LV_PCT(100), 70);
        lv_obj_add_style(add_btn, &style_btn, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(add_btn, add_btn_event_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *add_label = lv_label_create(add_btn);
        lv_ext_set_local_font(add_label, FONT_TITLE, lv_color_hex(0x80A0FF));
        lv_label_set_text(add_label, LV_SYMBOL_PLUS "  Add");
        lv_obj_center(add_label);
    }
}

static void list_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        uint32_t idx = (uint32_t)(uintptr_t)lv_obj_get_user_data(obj);
        vibrator_send(50, 50);
        app_alarm_edit(idx);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        uint32_t idx = (uint32_t)(uintptr_t)lv_obj_get_user_data(obj);
        alarm_entry_t entry;
        if (alarm_storage_get(idx, &entry) == 0) {
            entry.state = (lv_obj_get_state(obj) & LV_STATE_CHECKED)
                              ? ALARM_STATE_ENABLE
                              : ALARM_STATE_DISABLE;
            alarm_storage_update(idx, &entry);
        }
    }
}

static void add_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        vibrator_send(50, 50);
        app_alarm_edit(-1); /* -1 表示新建 */
    }
}

static void create_ui(void)
{
    lv_obj_t *screen = lv_scr_act();

    /* 标题 */
    lv_obj_t *title = lv_label_create(screen);
    lv_ext_set_local_font(title, FONT_TITLE, lv_color_hex(0xFFFFFF));
    lv_label_set_text(title, "Alarm");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* 列表容器 */
    list_cnt = lv_list_create(screen);
    lv_obj_set_size(list_cnt, SCR_W - 40, SCR_H - 80);
    lv_obj_align(list_cnt, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(list_cnt, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list_cnt, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(list_cnt, LV_SCROLLBAR_MODE_OFF);

    rebuild_list();
}

static void on_start(void)
{
    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 50);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&style_btn, LV_OPA_10);
    lv_style_set_shadow_width(&style_btn, 0);

    alarm_storage_init();
    create_ui();
    lv_img_cache_invalidate_src(NULL);
}

static void on_resume(void)
{
    /* 从编辑页返回后刷新列表 */
    if (list_cnt)
        rebuild_list();
}

static void on_pause(void)
{
}

static void on_stop(void)
{
    if (list_cnt) {
        lv_obj_del(list_cnt);
        list_cnt = NULL;
    }
    lv_style_reset(&style_btn);
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

LV_IMG_DECLARE(img_alarm);
#define APP_ID "alarm"
static int app_main(intent_t i)
{
    (void)i;
    gui_app_regist_msg_handler(APP_ID, msg_handler);
    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(alarm), LV_EXT_IMG_GET(img_alarm),
                   APP_ID, app_main);
