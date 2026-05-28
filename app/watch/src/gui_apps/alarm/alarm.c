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
#include "alarm_manager.h"
#include "vibrator_manager.h"
#include "my_widget.h"

#define LOG_TAG "alarm.ui"
#define LOG_LVL LOG_LVL_DBG
#include <ulog.h>

/* 重复天数名称 */
static const char *day_names[] = {
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

#define SCR_W LV_HOR_RES_MAX
#define SCR_H LV_VER_RES_MAX

static lv_obj_t *list_cnt = NULL;
static uint8_t alarm_num = 0;

/* 前向声明 */
extern void app_alarm_edit(uint32_t alarm_idx);
static void list_btn_event_cb(lv_event_t *e);
static void add_btn_event_cb(lv_event_t *e);
static void alarm_switch_event_cb(lv_event_t *e);

static const char *get_repeat_text(uint8_t days)
{
    static char buf[64];
    if (days == ALARM_REPEAT_ONE_SHOT)
        return "仅一次";
    if (days == ALARM_REPEAT_EVERYDAY)
        return "每天";
    if (days == ALARM_REPEAT_WORKDAY)
        return "工作日";
    if (days == ALARM_REPEAT_WEEKEND)
        return "周末";

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

        /* 列表项容器 */
        lv_obj_t *list_btn = lv_obj_create(list_cnt);
        lv_obj_set_size(list_btn, LV_PCT(100), 83);
        lv_obj_clear_flag(list_btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(list_btn, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(list_btn, lv_color_hex(0x1c1c1c),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(list_btn, LV_OPA_COVER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(list_btn, 0,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(list_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_left(list_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(list_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(list_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(list_btn, 19, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_user_data(list_btn, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(list_btn, list_btn_event_cb, LV_EVENT_CLICKED, NULL);

        /* 左侧文本容器 */
        lv_obj_t *text_box = lv_obj_create(list_btn);
        lv_obj_set_size(text_box, LV_PCT(60), LV_SIZE_CONTENT);
        lv_obj_clear_flag(text_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(text_box, LV_OPA_TRANSP,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(text_box, 0,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(text_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(text_box, LV_ALIGN_LEFT_MID, 0, 0);

        /* 时间文本 */
        lv_obj_t *time_label = lv_label_create(text_box);
        lv_ext_set_local_font(time_label, FONT_SUBTITLE, lv_color_hex(0xc0c0c0));
        lv_label_set_text(time_label, time_str);
        lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 0, -10);

        /* 重复文本 */
        lv_obj_t *repeat_label = lv_label_create(text_box);
        lv_ext_set_local_font(repeat_label, FONT_SMALL, lv_color_hex(0x747474));
        lv_label_set_text(repeat_label, get_repeat_text(db.alarms[i].repeat_days));
        lv_obj_align_to(repeat_label, time_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

        /* 右侧开关 - 参考设置页自动亮度样式 */
        lv_obj_t *sw = lv_switch_create(list_btn);
        lv_obj_set_size(sw, 50, 25);
        lv_obj_set_style_align(sw, LV_ALIGN_RIGHT_MID, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(sw, lv_color_hex(0x55aa6c), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_height(sw, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_width(sw, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(sw, -3, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(sw, -3, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_left(sw, -3, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(sw, -3, LV_PART_KNOB | LV_STATE_DEFAULT);
        lv_obj_set_user_data(sw, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(sw, alarm_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

        /* 设置开关状态 */
        if (db.alarms[i].state == ALARM_STATE_ENABLE)
            lv_obj_add_state(sw, LV_STATE_CHECKED);
    }

    /* 添加按钮 */
    if (alarm_num < ALARM_MAX_COUNT) {
        lv_obj_t *add_btn = lv_obj_create(list_cnt);
        lv_obj_set_size(add_btn, LV_PCT(100), 64);
        lv_obj_clear_flag(add_btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(add_btn, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(add_btn, lv_color_hex(0x1c1c1c),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(add_btn, LV_OPA_COVER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(add_btn, 0,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_left(add_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(add_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(add_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(add_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(add_btn, add_btn_event_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *add_label = lv_label_create(add_btn);
        lv_ext_set_local_font(add_label, FONT_SUBTITLE, lv_color_hex(0x55aa6c));
        lv_label_set_text(add_label, "添加闹钟");
        lv_obj_align(add_label, LV_ALIGN_CENTER, 0, 0);
    }
}

static void list_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        lv_obj_t *obj = lv_event_get_target(e);
        uint32_t idx = (uint32_t)(uintptr_t)lv_obj_get_user_data(obj);
        vibrator_send(50, 50);
        app_alarm_edit(idx);
    }
}

static void alarm_switch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_VALUE_CHANGED)
        return;

    lv_obj_t *sw = lv_event_get_target(e);
    uint32_t idx = (uint32_t)(uintptr_t)lv_obj_get_user_data(sw);

    alarm_entry_t entry;
    if (alarm_storage_get(idx, &entry) != 0)
        return;

    entry.state = lv_obj_has_state(sw, LV_STATE_CHECKED) ? ALARM_STATE_ENABLE : ALARM_STATE_DISABLE;
    alarm_storage_update(idx, &entry);

    LOG_I("Alarm %d switch %s", idx, entry.state == ALARM_STATE_ENABLE ? "ON" : "OFF");
}

static void add_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        vibrator_send(50, 50);
        if (alarm_num >= ALARM_MAX_COUNT) {
            myui_toast_show(MYUI_TOAST_TYPE_WARNING, "闹钟数量已达上限");
            return;
        }
        app_alarm_edit(-1); /* -1 表示新建 */
    }
}

static void create_ui(void)
{
    lv_obj_t *screen = lv_scr_act();

    /* 标题 - 对齐setting页样式，使用绝对定位确保居中 */
    lv_obj_t *title = lv_label_create(screen);
    lv_ext_set_local_font(title, FONT_HUGE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(title, "闹钟");
    lv_obj_set_pos(title, 163, 3);

    /* 列表容器 - 使用flex布局对齐setting页内容区域样式 */
    list_cnt = lv_obj_create(screen);
    lv_obj_set_size(list_cnt, SCR_W - 20, SCR_H - 60);
    lv_obj_align(list_cnt, LV_ALIGN_TOP_MID, 0, 55);
    lv_obj_set_scroll_dir(list_cnt, LV_DIR_VER);
    lv_obj_set_style_flex_flow(list_cnt, LV_FLEX_FLOW_COLUMN,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(list_cnt, lv_color_hex(0x000000),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(list_cnt, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list_cnt, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(list_cnt, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(list_cnt, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(list_cnt, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(list_cnt, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(list_cnt, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(list_cnt, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(list_cnt, LV_LAYOUT_FLEX,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(list_cnt, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(list_cnt, 4, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_height(list_cnt, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    /* 关键：容器自身不可点击，避免拦截子按钮的CLICKED事件 */
    lv_obj_clear_flag(list_cnt, LV_OBJ_FLAG_CLICKABLE);

    rebuild_list();
}

static bool mgr_inited = false;

static void on_start(void)
{
    alarm_storage_init();
    if (!mgr_inited) {
        alarm_manager_init();
        mgr_inited = true;
    }
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
