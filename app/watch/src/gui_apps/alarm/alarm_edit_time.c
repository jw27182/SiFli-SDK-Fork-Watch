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

#define SCR_W LV_HOR_RES_MAX
#define SCR_H LV_VER_RES_MAX

static int32_t edit_idx = -1;
static alarm_entry_t edit_alarm;

/* 时间显示标签 */
static lv_obj_t *lbl_hour = NULL;
static lv_obj_t *lbl_minute = NULL;

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

/* 更新时间显示标签 */
static void update_time_labels(void)
{
    char buf[4];
    if (lbl_hour) {
        snprintf(buf, sizeof(buf), "%02d", edit_alarm.hour);
        lv_label_set_text(lbl_hour, buf);
    }
    if (lbl_minute) {
        snprintf(buf, sizeof(buf), "%02d", edit_alarm.minute);
        lv_label_set_text(lbl_minute, buf);
    }
}

/* 加减按钮事件 */
static void hour_up_event_cb(lv_event_t *e)
{
    (void)e;
    edit_alarm.hour = (edit_alarm.hour + 1) % 24;
    update_time_labels();
}

static void hour_down_event_cb(lv_event_t *e)
{
    (void)e;
    edit_alarm.hour = (edit_alarm.hour + 23) % 24;
    update_time_labels();
}

static void min_up_event_cb(lv_event_t *e)
{
    (void)e;
    edit_alarm.minute = (edit_alarm.minute + 1) % 60;
    update_time_labels();
}

static void min_down_event_cb(lv_event_t *e)
{
    (void)e;
    edit_alarm.minute = (edit_alarm.minute + 59) % 60;
    update_time_labels();
}

static void cancel_btn_event_cb(lv_event_t *e)
{
    (void)e;
    gui_app_goback();
}

static void set_btn_event_cb(lv_event_t *e)
{
    (void)e;
    alarm_storage_update(edit_idx, &edit_alarm);
    gui_app_goback();
}

/* 创建带 +/- 按钮的数值选择器 */
static lv_obj_t *create_num_picker(lv_obj_t *parent, const char *title_text,
                                    lv_obj_t **value_lbl,
                                    lv_event_cb_t up_cb, lv_event_cb_t down_cb)
{
    /* 列（垂直排列） */
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_size(col, 120, 300);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(col, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(col, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(col, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_main_place(col, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_cross_place(col, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(col, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 标题（在最上方） */
    lv_obj_t *title = lv_label_create(col);
    lv_ext_set_local_font(title, FONT_NORMAL, lv_color_hex(0x747474));
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 向上按钮（标题下方） */
    lv_obj_t *btn_up = lv_btn_create(col);
    lv_obj_set_size(btn_up, 100, 50);
    lv_obj_set_style_radius(btn_up, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_up, lv_color_hex(0x424145), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_up, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btn_up, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_up, up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_up, lv_color_hex(0x2a2a2a), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn_up, LV_OPA_70, LV_STATE_PRESSED);

    lv_obj_t *up_icon = lv_label_create(btn_up);
    lv_ext_set_local_font(up_icon, FONT_HUGE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(up_icon, "+");
    lv_obj_center(up_icon);

    /* 数值显示区（在中间） */
    lv_obj_t *val_box = lv_obj_create(col);
    lv_obj_set_size(val_box, 100, 80);
    lv_obj_clear_flag(val_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(val_box, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(val_box, lv_color_hex(0x1c1c1c), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(val_box, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(val_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(val_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *val = lv_label_create(val_box);
    lv_ext_set_local_font(val, FONT_BIGL, lv_color_hex(0xc0c0c0));
    lv_label_set_text(val, "00");
    lv_obj_align(val, LV_ALIGN_CENTER, 0, 0);
    *value_lbl = val;

    /* 向下按钮（在数值下方） */
    lv_obj_t *btn_down = lv_btn_create(col);
    lv_obj_set_size(btn_down, 100, 50);
    lv_obj_set_style_radius(btn_down, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_down, lv_color_hex(0x424145), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_down, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btn_down, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_down, down_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_down, lv_color_hex(0x2a2a2a), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn_down, LV_OPA_70, LV_STATE_PRESSED);

    lv_obj_t *down_icon = lv_label_create(btn_down);
    lv_ext_set_local_font(down_icon, FONT_HUGE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(down_icon, "-");
    lv_obj_center(down_icon);

    return col;
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
    lv_ext_set_local_font(title, FONT_HUGE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(title, "设置时间");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 3);

    /* 中间横向容器，放置时、分选择器和冒号 */
    lv_obj_t *pickers_row = lv_obj_create(scr);
    lv_obj_set_size(pickers_row, SCR_W - 40, 320);
    lv_obj_align(pickers_row, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_clear_flag(pickers_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(pickers_row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(pickers_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(pickers_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(pickers_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(pickers_row, LV_FLEX_FLOW_ROW, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_main_place(pickers_row, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_cross_place(pickers_row, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(pickers_row, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 小时选择器 */
    create_num_picker(pickers_row, "时", &lbl_hour, hour_up_event_cb, hour_down_event_cb);

    /* 冒号分隔符 - 与数值显示区垂直居中对齐 */
    lv_obj_t *colon_container = lv_obj_create(pickers_row);
    lv_obj_set_size(colon_container, 60, 300);
    lv_obj_clear_flag(colon_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(colon_container, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(colon_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(colon_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *colon = lv_label_create(colon_container);
    lv_ext_set_local_font(colon, FONT_HUGE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(colon, ":");
    /* 布局: 标题24 + 间距8 + btn_up50 + 间距8 + val_box80/2=40 = 130
       内容总高 24+8+50+8+80+8+50=228, 居中偏移(300-228)/2=36
       数值中心 36+130=166, 容器中心150, 差值+16 */
    lv_obj_align(colon, LV_ALIGN_CENTER, 0, 16);

    /* 分钟选择器 */
    create_num_picker(pickers_row, "分", &lbl_minute, min_up_event_cb, min_down_event_cb);

    /* Cancel 按钮 */
    lv_obj_t *cancel_btn = lv_btn_create(scr);
    lv_obj_set_size(cancel_btn, 120, 50);
    lv_obj_set_style_radius(cancel_btn, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x424145), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(cancel_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cancel_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 80, -40);
    lv_obj_add_event_cb(cancel_btn, cancel_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x2a2a2a), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(cancel_btn, LV_OPA_70, LV_STATE_PRESSED);

    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_ext_set_local_font(cancel_lbl, FONT_NORMAL, lv_color_hex(0xc0c0c0));
    lv_label_set_text(cancel_lbl, "取消");
    lv_obj_center(cancel_lbl);

    /* Set 按钮 */
    lv_obj_t *set_btn = lv_btn_create(scr);
    lv_obj_set_size(set_btn, 120, 50);
    lv_obj_set_style_radius(set_btn, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(0x55aa6c), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(set_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(set_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_RIGHT, -80, -40);
    lv_obj_add_event_cb(set_btn, set_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(0x2a2a2a), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(set_btn, LV_OPA_70, LV_STATE_PRESSED);

    lv_obj_t *set_lbl = lv_label_create(set_btn);
    lv_ext_set_local_font(set_lbl, FONT_NORMAL, lv_color_hex(0x000000));
    lv_label_set_text(set_lbl, "确定");
    lv_obj_center(set_lbl);

    /* 设置当前值 */
    update_time_labels();

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
    lbl_hour = NULL;
    lbl_minute = NULL;
}
