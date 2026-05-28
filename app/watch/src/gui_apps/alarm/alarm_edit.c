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
#include "my_widget.h"

#define SCR_W LV_HOR_RES_MAX
#define SCR_H LV_VER_RES_MAX

static lv_obj_t *list_cnt = NULL;
static int32_t alarm_idx = -1;
static alarm_entry_t current_alarm;
static bool dirty = false;

/* 前向声明 */
extern void app_alarm_edit_time(int32_t idx, const alarm_entry_t *entry);
extern void app_alarm_edit_repeat(int32_t idx, const alarm_entry_t *entry);
static void rebuild_edit_list(void);
static void on_start(void);
static void on_resume(void);
static void on_pause(void);
static void on_stop(void);

#define SUBPAGE_NAME "almedit"
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

static const char *get_repeat_text(uint8_t days)
{
    static char buf[64];
    static const char *day_names[] = {
        "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
    };
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

static void save_alarm(void)
{
    if (alarm_idx >= 0)
        alarm_storage_update(alarm_idx, &current_alarm);
}

/* 事件回调 */
static void back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (dirty)
        save_alarm();
    gui_app_goback();
}

static void time_btn_event_cb(lv_event_t *e)
{
    (void)e;
    save_alarm();
    app_alarm_edit_time(alarm_idx, &current_alarm);
}

static void repeat_btn_event_cb(lv_event_t *e)
{
    (void)e;
    save_alarm();
    app_alarm_edit_repeat(alarm_idx, &current_alarm);
}

static void snooze_btn_event_cb(lv_event_t *e)
{
    (void)e;
    current_alarm.snooze = current_alarm.snooze ? ALARM_SNOOZE_DISABLE
                                                : ALARM_SNOOZE_ENABLE;
    dirty = true;
    save_alarm();
    rebuild_edit_list();
}

static void delete_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (alarm_idx >= 0) {
        alarm_storage_delete(alarm_idx);
        alarm_idx = -1;
    }
    gui_app_goback();
}

static void rebuild_edit_list(void)
{
    if (!list_cnt)
        return;
    lv_obj_clean(list_cnt);

    /* 返回按钮区域 */
    lv_obj_t *header = lv_obj_create(list_cnt);
    lv_obj_set_size(header, LV_PCT(100), 50);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 50, 40);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(back_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_ext_set_local_font(back_lbl, FONT_NORMAL, lv_color_hex(0xc0c0c0));
    lv_label_set_text(back_lbl, "返回");
    lv_obj_center(back_lbl);

    lv_obj_t *title = lv_label_create(header);
    lv_ext_set_local_font(title, FONT_HUGE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(title, "编辑闹钟");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* 确定按钮（右上角） */
    lv_obj_t *ok_btn = lv_btn_create(header);
    lv_obj_set_size(ok_btn, 50, 40);
    lv_obj_set_style_bg_opa(ok_btn, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ok_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ok_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ok_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ok_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ok_lbl = lv_label_create(ok_btn);
    lv_ext_set_local_font(ok_lbl, FONT_NORMAL, lv_color_hex(0x55aa6c));
    lv_label_set_text(ok_lbl, "确定");
    lv_obj_center(ok_lbl);

    /* 时间按钮 - 两行内容，高度足够 */
    lv_obj_t *time_btn = lv_obj_create(list_cnt);
    lv_obj_set_size(time_btn, LV_PCT(100), 80);
    lv_obj_clear_flag(time_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(time_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(time_btn, 30,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(time_btn, lv_color_hex(0x1c1c1c),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(time_btn, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(time_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(time_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(time_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(time_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(time_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(time_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);

    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%d:%02d",
             current_alarm.hour, current_alarm.minute);
    lv_obj_t *time_label = lv_label_create(time_btn);
    lv_ext_set_local_font(time_label, FONT_TITLE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(time_label, time_str);
    lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(time_btn, time_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_set_style_bg_color(time_btn, lv_color_hex(0x2a2a2a),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(time_btn, LV_OPA_70, LV_STATE_PRESSED);

    /* 重复设置按钮 - 两行内容 */
    lv_obj_t *repeat_btn = lv_obj_create(list_cnt);
    lv_obj_set_size(repeat_btn, LV_PCT(100), 83);
    lv_obj_clear_flag(repeat_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(repeat_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(repeat_btn, 30,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(repeat_btn, lv_color_hex(0x1c1c1c),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(repeat_btn, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(repeat_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(repeat_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(repeat_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(repeat_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(repeat_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(repeat_btn, 19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(repeat_btn, repeat_btn_event_cb, LV_EVENT_CLICKED,
                        NULL);

    lv_obj_set_style_bg_color(repeat_btn, lv_color_hex(0x2a2a2a),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(repeat_btn, LV_OPA_70, LV_STATE_PRESSED);

    lv_obj_t *repeat_title = lv_label_create(repeat_btn);
    lv_ext_set_local_font(repeat_title, FONT_SUBTITLE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(repeat_title, "重复");
    lv_obj_align(repeat_title, LV_ALIGN_LEFT_MID, 0, -10);

    lv_obj_t *repeat_val = lv_label_create(repeat_btn);
    lv_ext_set_local_font(repeat_val, FONT_SMALL, lv_color_hex(0x747474));
    lv_label_set_text(repeat_val, get_repeat_text(current_alarm.repeat_days));
    lv_obj_align_to(repeat_val, repeat_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    /* 贪睡按钮 - 两行内容 */
    lv_obj_t *snooze_btn = lv_obj_create(list_cnt);
    lv_obj_set_size(snooze_btn, LV_PCT(100), 83);
    lv_obj_clear_flag(snooze_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(snooze_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(snooze_btn, 30,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(snooze_btn, lv_color_hex(0x1c1c1c),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(snooze_btn, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(snooze_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(snooze_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(snooze_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(snooze_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(snooze_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(snooze_btn, 19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(snooze_btn, snooze_btn_event_cb, LV_EVENT_CLICKED,
                        NULL);

    lv_obj_set_style_bg_color(snooze_btn, lv_color_hex(0x2a2a2a),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(snooze_btn, LV_OPA_70, LV_STATE_PRESSED);

    lv_obj_t *snooze_label = lv_label_create(snooze_btn);
    lv_ext_set_local_font(snooze_label, FONT_SUBTITLE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(snooze_label, "贪睡");
    lv_obj_align(snooze_label, LV_ALIGN_LEFT_MID, 0, -10);

    lv_obj_t *snooze_val = lv_label_create(snooze_btn);
    lv_ext_set_local_font(snooze_val, FONT_SMALL, lv_color_hex(0x747474));
    lv_label_set_text(snooze_val,
                      current_alarm.snooze ? "已开启" : "已关闭");
    lv_obj_align_to(snooze_val, snooze_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    /* 删除按钮 */
    lv_obj_t *del_btn = lv_obj_create(list_cnt);
    lv_obj_set_size(del_btn, LV_PCT(100), 64);
    lv_obj_clear_flag(del_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(del_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(del_btn, 30,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(del_btn, lv_color_hex(0x3A1E1E),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(del_btn, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(del_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(del_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(del_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(del_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(del_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(del_btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(del_btn, delete_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_set_style_bg_color(del_btn, lv_color_hex(0x2a2a2a),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(del_btn, LV_OPA_70, LV_STATE_PRESSED);

    lv_obj_t *del_label = lv_label_create(del_btn);
    lv_ext_set_local_font(del_label, FONT_NORMAL, lv_color_hex(0xFF6666));
    lv_label_set_text(del_label, "删除闹钟");
    lv_obj_align(del_label, LV_ALIGN_CENTER, 0, 0);
}

/* 入口函数 —— 由 alarm.c 调用，负责准备数据并导航到编辑子页面 */
void app_alarm_edit(uint32_t idx)
{
    alarm_idx = (int32_t)idx;

    if (alarm_idx >= 0) {
        alarm_storage_get(alarm_idx, &current_alarm);
    } else {
        /* 新建闹钟默认值 */
        memset(&current_alarm, 0, sizeof(current_alarm));
        current_alarm.hour = 8;
        current_alarm.minute = 0;
        current_alarm.state = ALARM_STATE_ENABLE;
        current_alarm.snooze = ALARM_SNOOZE_ENABLE;
        current_alarm.repeat_days = ALARM_REPEAT_ONE_SHOT;
        /* 先添加到存储 */
        if (alarm_storage_add(&current_alarm) != 0) {
            myui_toast_show(MYUI_TOAST_TYPE_WARNING, "存储失败，无法添加闹钟");
            return; /* 存储满或写入失败，无法添加 */
        }
        alarm_idx = alarm_storage_get_count() - 1;
    }
    dirty = false;

    /* 导航到编辑子页面 */
    gui_app_create_page(SUBPAGE_NAME, msg_handler);
}

static void on_start(void)
{
    list_cnt = lv_obj_create(lv_scr_act());
    lv_obj_set_size(list_cnt, SCR_W - 20, SCR_H - 20);
    lv_obj_align(list_cnt, LV_ALIGN_CENTER, 0, 0);
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

    rebuild_edit_list();
}

static void on_resume(void)
{
    /* 从子页面返回后重新从存储读取，避免用旧数据覆盖子页面的修改 */
    if (alarm_idx >= 0)
        alarm_storage_get(alarm_idx, &current_alarm);
    if (list_cnt)
        rebuild_edit_list();
}

static void on_pause(void)
{
}

static void on_stop(void)
{
    if (dirty)
        save_alarm();

    if (list_cnt) {
        lv_obj_del(list_cnt);
        list_cnt = NULL;
    }
}

