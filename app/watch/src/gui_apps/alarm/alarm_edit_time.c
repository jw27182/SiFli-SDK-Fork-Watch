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

#define SCR_W 410
#define SCR_H 502
#define BORDER_GAP_HOR 80
#define BORDER_GAP_VER 40

static lv_obj_t *roller_hour = NULL;
static lv_obj_t *roller_minute = NULL;
static int32_t edit_idx = -1;
static alarm_entry_t edit_alarm;

static void on_start(void);
static void on_resume(void);
static void on_pause(void);
static void on_stop(void);

#define SUBPAGE_NAME "almadd"
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

static void hour_roller_event_cb(lv_event_t *e)
{
    (void)e;
    if (roller_hour)
        edit_alarm.hour = lv_roller_get_selected(roller_hour);
}

static void minute_roller_event_cb(lv_event_t *e)
{
    (void)e;
    if (roller_minute)
        edit_alarm.minute = lv_roller_get_selected(roller_minute);
}

static void cancel_btn_event_cb(lv_event_t *e)
{
    (void)e;
    gui_app_goback();
}

static void set_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (roller_hour)
        edit_alarm.hour = lv_roller_get_selected(roller_hour);
    if (roller_minute)
        edit_alarm.minute = lv_roller_get_selected(roller_minute);

    alarm_storage_update(edit_idx, &edit_alarm);
    gui_app_goback();
}

/* 入口函数 —— 由 alarm_edit.c 调用 */
void app_alarm_edit_time(int32_t idx, const alarm_entry_t *entry)
{
    edit_idx = idx;
    if (entry)
        memcpy(&edit_alarm, entry, sizeof(alarm_entry_t));
    else {
        memset(&edit_alarm, 0, sizeof(edit_alarm));
        edit_alarm.hour = 8;
        edit_alarm.state = ALARM_STATE_ENABLE;
        edit_alarm.snooze = ALARM_SNOOZE_ENABLE;
    }

    gui_app_create_page(SUBPAGE_NAME, msg_handler);
}

static void on_start(void)
{
    lv_obj_t *scr = lv_scr_act();

    /* 标题 */
    lv_obj_t *title = lv_label_create(scr);
    lv_ext_set_local_font(title, FONT_TITLE, lv_color_hex(0xFFFFFF));
    lv_label_set_text(title, "Set Time");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, BORDER_GAP_VER);

    /* 小时滚轮 */
    roller_hour = lv_roller_create(scr);
    lv_roller_set_options(roller_hour,
                          "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
                          "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
                          "20\n21\n22\n23",
                          LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_hour, 3);
    lv_obj_set_width(roller_hour, 100);
    lv_obj_align(roller_hour, LV_ALIGN_TOP_LEFT, BORDER_GAP_HOR, 80);
    lv_ext_set_local_font(roller_hour, FONT_TITLE, lv_color_hex(0xFFFFFF));
    lv_obj_set_style_bg_color(roller_hour, lv_color_hex(0x1E1E2E),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(roller_hour, lv_color_hex(0x3A6D7B),
                              LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(roller_hour, hour_roller_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* 分钟滚轮 */
    roller_minute = lv_roller_create(scr);
    lv_roller_set_options(roller_minute,
                          "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
                          "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
                          "20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n"
                          "30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n"
                          "40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n"
                          "50\n51\n52\n53\n54\n55\n56\n57\n58\n59",
                          LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_minute, 3);
    lv_obj_set_width(roller_minute, 100);
    lv_obj_align(roller_minute, LV_ALIGN_TOP_RIGHT, -BORDER_GAP_HOR, 80);
    lv_ext_set_local_font(roller_minute, FONT_TITLE, lv_color_hex(0xFFFFFF));
    lv_obj_set_style_bg_color(roller_minute, lv_color_hex(0x1E1E2E),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(roller_minute, lv_color_hex(0x3A6D7B),
                              LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(roller_minute, minute_roller_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* 设置当前值 */
    lv_roller_set_selected(roller_hour, edit_alarm.hour, LV_ANIM_OFF);
    lv_roller_set_selected(roller_minute, edit_alarm.minute, LV_ANIM_OFF);

    /* Cancel 按钮 */
    lv_obj_t *cancel_btn = lv_btn_create(scr);
    lv_obj_set_size(cancel_btn, 120, 50);
    lv_obj_set_style_radius(cancel_btn, 16,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x333333),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(cancel_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, BORDER_GAP_HOR, -60);
    lv_obj_add_event_cb(cancel_btn, cancel_btn_event_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_ext_set_local_font(cancel_lbl, FONT_NORMAL, lv_color_hex(0xFFFFFF));
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);

    /* Set 按钮 */
    lv_obj_t *set_btn = lv_btn_create(scr);
    lv_obj_set_size(set_btn, 120, 50);
    lv_obj_set_style_radius(set_btn, 16,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(0x3A6D7B),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(set_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_RIGHT, -BORDER_GAP_HOR, -60);
    lv_obj_add_event_cb(set_btn, set_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *set_lbl = lv_label_create(set_btn);
    lv_ext_set_local_font(set_lbl, FONT_NORMAL, lv_color_hex(0xFFFFFF));
    lv_label_set_text(set_lbl, "Set");
    lv_obj_center(set_lbl);

    lv_img_cache_invalidate_src(NULL);
}

static void on_resume(void)
{
}

static void on_pause(void)
{
}

static void on_stop(void)
{
    roller_hour = NULL;
    roller_minute = NULL;
}

