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
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_ext_set_local_font(back_lbl, FONT_NORMAL, lv_color_hex(0xFFFFFF));
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    lv_obj_t *title = lv_label_create(header);
    lv_ext_set_local_font(title, FONT_TITLE, lv_color_hex(0xFFFFFF));
    lv_label_set_text(title, "Edit Alarm");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* 时间按钮 */
    lv_obj_t *time_btn = lv_btn_create(list_cnt);
    lv_obj_set_size(time_btn, LV_PCT(100), 70);
    lv_obj_set_style_radius(time_btn, 20,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(time_btn, lv_color_hex(0x1E1E2E),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(time_btn, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(time_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);

    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%d:%02d",
             current_alarm.hour, current_alarm.minute);
    lv_obj_t *time_label = lv_label_create(time_btn);
    lv_ext_set_local_font(time_label, FONT_SUPER, lv_color_hex(0xFFFFFF));
    lv_label_set_text(time_label, time_str);
    lv_obj_center(time_label);
    lv_obj_add_event_cb(time_btn, time_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* 重复设置按钮 */
    lv_obj_t *repeat_btn = lv_btn_create(list_cnt);
    lv_obj_set_size(repeat_btn, LV_PCT(100), 60);
    lv_obj_set_style_radius(repeat_btn, 20,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(repeat_btn, lv_color_hex(0x1E1E2E),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(repeat_btn, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(repeat_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(repeat_btn, repeat_btn_event_cb, LV_EVENT_CLICKED,
                        NULL);

    lv_obj_t *repeat_title = lv_label_create(repeat_btn);
    lv_ext_set_local_font(repeat_title, FONT_NORMAL, lv_color_hex(0x888888));
    lv_label_set_text(repeat_title, "Repeat");
    lv_obj_align(repeat_title, LV_ALIGN_LEFT_MID, 16, -8);

    lv_obj_t *repeat_val = lv_label_create(repeat_btn);
    lv_ext_set_local_font(repeat_val, FONT_NORMAL, lv_color_hex(0xCCCCCC));
    lv_label_set_text(repeat_val, get_repeat_text(current_alarm.repeat_days));
    lv_obj_align_to(repeat_val, repeat_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    /* 贪睡按钮 */
    lv_obj_t *snooze_btn = lv_btn_create(list_cnt);
    lv_obj_set_size(snooze_btn, LV_PCT(100), 60);
    lv_obj_set_style_radius(snooze_btn, 20,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(snooze_btn, lv_color_hex(0x1E1E2E),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(snooze_btn, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(snooze_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(snooze_btn, snooze_btn_event_cb, LV_EVENT_CLICKED,
                        NULL);

    lv_obj_t *snooze_label = lv_label_create(snooze_btn);
    lv_ext_set_local_font(snooze_label, FONT_NORMAL, lv_color_hex(0xCCCCCC));
    lv_label_set_text(snooze_label, "Snooze");
    lv_obj_align(snooze_label, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *snooze_val = lv_label_create(snooze_btn);
    lv_ext_set_local_font(snooze_val, FONT_SMALL,
                          current_alarm.snooze ? lv_color_hex(0x66BB6A)
                                               : lv_color_hex(0x888888));
    lv_label_set_text(snooze_val,
                      current_alarm.snooze ? "ON" : "OFF");
    lv_obj_align(snooze_val, LV_ALIGN_RIGHT_MID, -16, 0);

    /* 删除按钮 */
    lv_obj_t *del_btn = lv_btn_create(list_cnt);
    lv_obj_set_size(del_btn, LV_PCT(100), 60);
    lv_obj_set_style_radius(del_btn, 20,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(del_btn, lv_color_hex(0x3A1E1E),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(del_btn, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(del_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(del_btn, delete_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *del_label = lv_label_create(del_btn);
    lv_ext_set_local_font(del_label, FONT_NORMAL, lv_color_hex(0xFF6666));
    lv_label_set_text(del_label, "Delete alarm");
    lv_obj_center(del_label);
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
            return; /* 存储满，无法添加 */
        }
        alarm_idx = alarm_storage_get_count() - 1;
    }
    dirty = false;

    /* 导航到编辑子页面 */
    gui_app_create_page(SUBPAGE_NAME, msg_handler);
}

static void on_start(void)
{
    list_cnt = lv_list_create(lv_scr_act());
    lv_obj_set_size(list_cnt, SCR_W - 20, SCR_H - 20);
    lv_obj_align(list_cnt, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(list_cnt, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list_cnt, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(list_cnt, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_row(list_cnt, 12,
                             LV_PART_MAIN | LV_STATE_DEFAULT);

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

