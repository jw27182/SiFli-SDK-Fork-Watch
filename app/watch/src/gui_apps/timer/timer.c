/*********************
 *      INCLUDES
 *********************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <rtthread.h>
#include <rtdevice.h>

#include "app_mem.h"
#include "gui_app_fwk.h"
#include "littlevgl2rtt.h"
#include "lv_ex_data.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "lvsf_comp.h"
#include "vibrator_manager.h"

#define SCR_W LV_HOR_RES_MAX
#define SCR_H LV_VER_RES_MAX

#define LIST_TIMER_HEIGHT     160
#define LIST_TIMER_WIDGET_RADIUS 30
#define LIST_TIMER_BG_COLOR   0x1c1c1c
#define LIST_TIMER_ACCENT_COLOR 0x55aa6c
#define LIST_TIMER_ROW_SPACING  40

typedef struct {
    lv_obj_t *bg;
    lv_obj_t *list_container;
    lv_obj_t *timer_list;
    lv_obj_t *timer_title;
    lv_obj_t *timer_label;
    lv_obj_t *countdown_screen;
    lv_obj_t *pause_button;
    lv_obj_t *stop_button;
    lv_obj_t *current_button;
} timer_ui_t;

typedef struct {
    rt_timer_t countdown_timer;
    volatile uint32_t remaining_time;
    volatile bool is_paused;
    volatile bool dirty;
} app_timer_data_ctx_t;

static timer_ui_t ui;
static app_timer_data_ctx_t app_timer_data_ctx = {0};
static lv_timer_t *refresh_lv_timer = NULL;

static const char *timer_options[] = {
    "30 secs", "1 min", "2 mins", "3 mins", "4 mins",
    "5 mins", "10 mins", "15 mins", "20 mins", "25 mins",
    "30 mins", "1 hour", ""
};

static int16_t old_selected_timer_index = -1;
static int16_t selected_timer_index = 0;
static volatile bool _timeout = false;
static lv_obj_t *timeout_msg_box = NULL;

/* 前向声明 */
static void update_timer_label(void);
static void show_counter_listview(void);
static void show_timeout_notification(void);
static void refresh_pause_button_icon(void);

static void remove_countdown_timer(void)
{
    if (app_timer_data_ctx.countdown_timer) {
        rt_timer_stop(app_timer_data_ctx.countdown_timer);
        rt_timer_delete(app_timer_data_ctx.countdown_timer);
        app_timer_data_ctx.countdown_timer = NULL;
    }
}

static void countdown_timer_cb(void *parameter)
{
    (void)parameter;
    if (!app_timer_data_ctx.is_paused &&
        app_timer_data_ctx.remaining_time > 0) {
        app_timer_data_ctx.remaining_time--;
        app_timer_data_ctx.dirty = true;

        if (app_timer_data_ctx.remaining_time == 0) {
            _timeout = true;
        }
    }
}

/* LVGL 定时器回调：检查 dirty 标志并刷新 UI */
static void lv_refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    if (app_timer_data_ctx.dirty) {
        app_timer_data_ctx.dirty = false;
        if (app_timer_data_ctx.remaining_time == 0 && _timeout) {
            _timeout = false;
            show_counter_listview();
            show_timeout_notification();
            vibrator_send(500, 80);
        } else {
            update_timer_label();
        }
    }
}

static void update_timer_label(void)
{
    if (!lv_obj_is_valid(ui.timer_label))
        return;

    uint32_t hours = app_timer_data_ctx.remaining_time / 3600;
    uint32_t minutes = (app_timer_data_ctx.remaining_time % 3600) / 60;
    uint32_t seconds = app_timer_data_ctx.remaining_time % 60;
    char time_str[9];
    snprintf(time_str, sizeof(time_str), "%02lu:%02lu:%02lu",
             (unsigned long)hours, (unsigned long)minutes,
             (unsigned long)seconds);
    lv_label_set_text(ui.timer_label, time_str);
}

static void show_new_timer_view(const char *text)
{
    (void)text;
    if (lv_obj_is_valid(ui.list_container))
        lv_obj_add_flag(ui.list_container, LV_OBJ_FLAG_HIDDEN);
    if (lv_obj_is_valid(ui.countdown_screen))
        lv_obj_clear_flag(ui.countdown_screen, LV_OBJ_FLAG_HIDDEN);
}

static void create_countdown_timer(void)
{
    remove_countdown_timer();
    app_timer_data_ctx.countdown_timer = rt_timer_create(
        "cd_timer", countdown_timer_cb, NULL, RT_TICK_PER_SECOND,
        RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
    if (app_timer_data_ctx.countdown_timer) {
        rt_timer_start(app_timer_data_ctx.countdown_timer);
        app_timer_data_ctx.is_paused = false;
    }
}

static void tap_button(lv_obj_t *btn)
{
    if (!lv_obj_is_valid(btn))
        return;

    const char *text = (const char *)lv_obj_get_user_data(btn);
    if (strcmp(text, "30 secs") == 0)
        app_timer_data_ctx.remaining_time = 30;
    else if (strcmp(text, "1 min") == 0)
        app_timer_data_ctx.remaining_time = 60;
    else if (strcmp(text, "2 mins") == 0)
        app_timer_data_ctx.remaining_time = 120;
    else if (strcmp(text, "3 mins") == 0)
        app_timer_data_ctx.remaining_time = 180;
    else if (strcmp(text, "4 mins") == 0)
        app_timer_data_ctx.remaining_time = 240;
    else if (strcmp(text, "5 mins") == 0)
        app_timer_data_ctx.remaining_time = 300;
    else if (strcmp(text, "10 mins") == 0)
        app_timer_data_ctx.remaining_time = 600;
    else if (strcmp(text, "15 mins") == 0)
        app_timer_data_ctx.remaining_time = 900;
    else if (strcmp(text, "20 mins") == 0)
        app_timer_data_ctx.remaining_time = 1200;
    else if (strcmp(text, "25 mins") == 0)
        app_timer_data_ctx.remaining_time = 1500;
    else if (strcmp(text, "30 mins") == 0)
        app_timer_data_ctx.remaining_time = 1800;
    else if (strcmp(text, "1 hour") == 0)
        app_timer_data_ctx.remaining_time = 3600;
    else
        app_timer_data_ctx.remaining_time = 0;

    update_timer_label();
    show_new_timer_view(text);
    create_countdown_timer();
}

static void show_counter_listview(void)
{
    if (lv_obj_is_valid(ui.countdown_screen))
        lv_obj_add_flag(ui.countdown_screen, LV_OBJ_FLAG_HIDDEN);
    if (lv_obj_is_valid(ui.list_container))
        lv_obj_clear_flag(ui.list_container, LV_OBJ_FLAG_HIDDEN);
    if (lv_obj_is_valid(ui.timer_title))
        lv_label_set_text(ui.timer_title, "Timer");
}

static void refresh_timer_status(void)
{
    if (app_timer_data_ctx.countdown_timer) {
        if (app_timer_data_ctx.is_paused)
            rt_timer_stop(app_timer_data_ctx.countdown_timer);
        else
            rt_timer_start(app_timer_data_ctx.countdown_timer);
    }
}

static void refresh_pause_button_icon(void)
{
    if (lv_obj_is_valid(ui.pause_button)) {
        lv_obj_t *lbl = lv_obj_get_child(ui.pause_button, 0);
        if (lv_obj_is_valid(lbl)) {
            lv_label_set_text(lbl,
                              app_timer_data_ctx.is_paused ? LV_SYMBOL_PLAY
                                                           : LV_SYMBOL_PAUSE);
        }
    }
}

static void pause_button_event_cb(lv_event_t *e)
{
    (void)e;
    app_timer_data_ctx.is_paused = !app_timer_data_ctx.is_paused;
    refresh_timer_status();
    refresh_pause_button_icon();
}

static void stop_button_event_cb(lv_event_t *e)
{
    (void)e;
    remove_countdown_timer();
    show_counter_listview();
    app_timer_data_ctx.remaining_time = 0;
}

static void timer_list_event_cb(lv_event_t *e)
{
    if (LV_EVENT_CLICKED == lv_event_get_code(e))
        tap_button(lv_event_get_target(e));
}

static void scroll_timer_list(lv_obj_t *list)
{
    uint16_t min_offset = LV_VER_RES;
    uint8_t child_cnt = list->spec_attr->child_cnt;

    for (uint8_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = list->spec_attr->children[i];
        lv_coord_t y_center = child->coords.y1 + LIST_TIMER_HEIGHT / 2;
        lv_coord_t y_diff = LV_ABS(y_center - LV_VER_RES / 2);
        if (y_diff < min_offset) {
            min_offset = y_diff;
            selected_timer_index = i;
        }
    }

    if (old_selected_timer_index != selected_timer_index) {
        old_selected_timer_index = selected_timer_index;
        for (uint8_t i = 0; i < child_cnt; i++) {
            lv_obj_t *item = list->spec_attr->children[i];
            if (i == selected_timer_index) {
                ui.current_button = item;
            }
        }
    }
}

static void timer_list_scroll_event_cb(lv_event_t *e)
{
    lv_obj_t *list = lv_event_get_target(e);
    if (list == NULL)
        return;
    if (e->code == LV_EVENT_SCROLL)
        scroll_timer_list(list);
}

static lv_obj_t *create_timer_list(lv_obj_t *parent)
{
    ui.current_button = NULL;

    int option_count = 0;
    while (timer_options[option_count][0] != '\0')
        option_count++;

    lv_obj_t *list_container = lv_obj_create(parent);
    lv_obj_set_size(list_container, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_opa(list_container, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list_container, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(list_container, 0,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(list_container, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *list = lv_obj_create(list_container);
    lv_obj_set_size(list, LV_PCT(90), SCR_H);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_0,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_style_pad_row(list, LIST_TIMER_ROW_SPACING,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(list, LV_VER_RES / 2,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(list, timer_list_scroll_event_cb, LV_EVENT_ALL, NULL);
    ui.timer_list = list;

    /* 标题背景 */
    lv_obj_t *title_bg = lv_obj_create(list_container);
    lv_obj_set_size(title_bg, SCR_W, 60);
    lv_obj_set_style_bg_color(title_bg, lv_color_hex(0x000000),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(title_bg, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(title_bg, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title_bg, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(title_bg, LV_OBJ_FLAG_SCROLLABLE);

    ui.timer_title = lv_label_create(title_bg);
    lv_label_set_text(ui.timer_title, "Timer");
    lv_obj_set_style_text_align(ui.timer_title, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_ext_set_local_font(ui.timer_title, FONT_HUGE,
                          lv_color_hex(0xc0c0c0));
    lv_obj_align(ui.timer_title, LV_ALIGN_CENTER, 0, 0);

    /* 列表项 */
    for (int i = 0; i < option_count; i++) {
        lv_obj_t *timer_widget = lv_obj_create(list);
        lv_obj_set_size(timer_widget, LV_PCT(100), LIST_TIMER_HEIGHT);
        lv_obj_set_style_radius(timer_widget, LIST_TIMER_WIDGET_RADIUS,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(timer_widget,
                                  lv_color_hex(LIST_TIMER_BG_COLOR),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(timer_widget, LV_OPA_COVER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(timer_widget, 0,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(timer_widget, LV_OBJ_FLAG_SCROLLABLE);

        /* 闹钟图标 */
        lv_obj_t *icon = lv_label_create(timer_widget);
        lv_ext_set_local_font(icon, FONT_TITLE, lv_color_hex(0x747474));
        lv_label_set_text(icon, LV_SYMBOL_BELL);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 20, -20);

        /* 时间标签 */
        lv_obj_t *label = lv_label_create(timer_widget);
        lv_label_set_text(label, timer_options[i]);
        lv_ext_set_local_font(label, FONT_NORMAL, lv_color_hex(0xc0c0c0));
        lv_obj_align_to(label, icon, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

        /* 开始按钮 */
        lv_obj_t *start_btn = lv_obj_create(timer_widget);
        lv_obj_set_size(start_btn, 65, 65);
        lv_obj_align(start_btn, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_set_style_radius(start_btn, 25,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(start_btn,
                                  lv_color_hex(LIST_TIMER_ACCENT_COLOR),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(start_btn, 0,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(start_btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *start_icon = lv_label_create(start_btn);
        lv_ext_set_local_font(start_icon, FONT_NORMAL, lv_color_hex(0xc0c0c0));
        lv_label_set_text(start_icon, LV_SYMBOL_PLAY);
        lv_obj_center(start_icon);

        lv_obj_add_event_cb(timer_widget, timer_list_event_cb,
                            LV_EVENT_CLICKED, NULL);
        lv_obj_set_user_data(timer_widget, (void *)timer_options[i]);
    }

    /* 滚动到默认位置 */
    selected_timer_index = option_count - 1;
    old_selected_timer_index = -1;
    if (option_count > selected_timer_index) {
        lv_obj_t *default_item = lv_obj_get_child(list, selected_timer_index);
        lv_obj_scroll_to_view(default_item, LV_ANIM_OFF);
        ui.current_button = default_item;
        old_selected_timer_index = selected_timer_index;
    }

    return list_container;
}

static lv_obj_t *create_countdown_screen(lv_obj_t *parent)
{
    lv_obj_t *countdown_screen = lv_obj_create(parent);
    lv_obj_set_size(countdown_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(countdown_screen, lv_color_black(),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(countdown_screen, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(countdown_screen, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(countdown_screen, LV_OBJ_FLAG_SCROLLABLE);

    ui.timer_label = lv_label_create(countdown_screen);
    lv_ext_set_local_font(ui.timer_label, FONT_SUPER, lv_color_hex(0xc0c0c0));
    lv_obj_align(ui.timer_label, LV_ALIGN_TOP_MID, 0, 120);
    lv_label_set_text(ui.timer_label, "00:00:00");

    /* 暂停按钮 */
    ui.pause_button = lv_btn_create(countdown_screen);
    lv_obj_set_size(ui.pause_button, 100, 70);
    lv_obj_set_style_radius(ui.pause_button, 25,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui.pause_button, lv_color_hex(0x424145),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui.pause_button, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui.pause_button, LV_ALIGN_TOP_RIGHT, -30, 260);

    lv_obj_t *pause_lbl = lv_label_create(ui.pause_button);
    lv_ext_set_local_font(pause_lbl, FONT_TITLE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(pause_lbl, LV_SYMBOL_PAUSE);
    lv_obj_center(pause_lbl);

    lv_obj_add_event_cb(ui.pause_button, pause_button_event_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_set_style_bg_color(ui.pause_button, lv_color_hex(0x2a2a2a),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ui.pause_button, LV_OPA_70, LV_STATE_PRESSED);

    /* 停止按钮 */
    ui.stop_button = lv_btn_create(countdown_screen);
    lv_obj_set_size(ui.stop_button, 100, 70);
    lv_obj_set_style_radius(ui.stop_button, 25,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui.stop_button, lv_color_hex(0x424145),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui.stop_button, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui.stop_button, LV_ALIGN_TOP_LEFT, 30, 260);

    lv_obj_t *stop_lbl = lv_label_create(ui.stop_button);
    lv_ext_set_local_font(stop_lbl, FONT_NORMAL, lv_color_hex(0xc0c0c0));
    lv_label_set_text(stop_lbl, "STOP");
    lv_obj_center(stop_lbl);

    lv_obj_add_event_cb(ui.stop_button, stop_button_event_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_set_style_bg_color(ui.stop_button, lv_color_hex(0x2a2a2a),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ui.stop_button, LV_OPA_70, LV_STATE_PRESSED);

    return countdown_screen;
}

static void close_timeout_notification_cb(lv_event_t *e)
{
    (void)e;
    if (timeout_msg_box && lv_obj_is_valid(timeout_msg_box)) {
        lv_obj_t *mask = (lv_obj_t *)lv_obj_get_user_data(timeout_msg_box);
        if (mask && lv_obj_is_valid(mask))
            lv_obj_del(mask);
        else
            lv_obj_del(timeout_msg_box);
        timeout_msg_box = NULL;
    }
    _timeout = false;
    remove_countdown_timer();
    show_counter_listview();
}

static void show_timeout_notification(void)
{
    if (timeout_msg_box && lv_obj_is_valid(timeout_msg_box))
        return;

    lv_obj_t *mask = lv_obj_create(lv_scr_act());
    lv_obj_set_size(mask, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(mask, lv_color_hex(0x000000),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(mask, LV_OPA_60,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(mask, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    timeout_msg_box = lv_obj_create(mask);
    lv_obj_set_size(timeout_msg_box, LV_PCT(75), LV_PCT(60));
    lv_obj_align(timeout_msg_box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(timeout_msg_box, 40,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(timeout_msg_box, lv_color_hex(0x1c1c1c),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(timeout_msg_box, 240,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(timeout_msg_box, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *alarm_icon = lv_label_create(timeout_msg_box);
    lv_ext_set_local_font(alarm_icon, FONT_SUPER, lv_color_hex(0xc0c0c0));
    lv_label_set_text(alarm_icon, LV_SYMBOL_BELL);
    lv_obj_align(alarm_icon, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *title = lv_label_create(timeout_msg_box);
    lv_ext_set_local_font(title, FONT_HUGE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(title, "Time's Up!");
    lv_obj_align_to(title, alarm_icon, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);

    lv_obj_t *subtitle = lv_label_create(timeout_msg_box);
    lv_ext_set_local_font(subtitle, FONT_NORMAL, lv_color_hex(0x747474));
    lv_label_set_text(subtitle, "Your timer has finished");
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    lv_obj_t *close_btn = lv_btn_create(timeout_msg_box);
    lv_obj_set_size(close_btn, 140, 50);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_radius(close_btn, 25,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x424145),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(close_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *btn_label = lv_label_create(close_btn);
    lv_ext_set_local_font(btn_label, FONT_NORMAL, lv_color_hex(0xc0c0c0));
    lv_label_set_text(btn_label, "Close");
    lv_obj_center(btn_label);

    lv_obj_add_event_cb(close_btn, close_timeout_notification_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_set_user_data(timeout_msg_box, mask);
}

static void create_timer_app_ui(lv_obj_t *parent)
{
    ui.bg = lv_obj_create(parent);
    lv_obj_set_size(ui.bg, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(ui.bg, lv_color_black(),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui.bg, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui.bg, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(ui.bg, LV_OBJ_FLAG_SCROLLABLE);

    ui.list_container = create_timer_list(ui.bg);
    ui.countdown_screen = create_countdown_screen(ui.bg);
    lv_obj_add_flag(ui.countdown_screen, LV_OBJ_FLAG_HIDDEN);
}

static void on_start(void)
{
    memset(&ui, 0, sizeof(timer_ui_t));
    create_timer_app_ui(lv_scr_act());

    /* 创建 LVGL 刷新定时器 */
    refresh_lv_timer = lv_timer_create(lv_refresh_cb, 200, NULL);
    lv_timer_set_repeat_count(refresh_lv_timer, -1);

    if (app_timer_data_ctx.countdown_timer) {
        if (_timeout) {
            show_timeout_notification();
            vibrator_send(500, 80);
        } else {
            update_timer_label();
            if (lv_obj_is_valid(ui.countdown_screen))
                lv_obj_clear_flag(ui.countdown_screen, LV_OBJ_FLAG_HIDDEN);
            if (lv_obj_is_valid(ui.list_container))
                lv_obj_add_flag(ui.list_container, LV_OBJ_FLAG_HIDDEN);
            refresh_pause_button_icon();
        }
    } else {
        show_counter_listview();
    }

    lv_img_cache_invalidate_src(NULL);
}

static void on_pause(void)
{
    refresh_timer_status();
}

static void on_resume(void)
{
    refresh_timer_status();
}

static void on_stop(void)
{
    if (refresh_lv_timer) {
        lv_timer_del(refresh_lv_timer);
        refresh_lv_timer = NULL;
    }

    if (lv_obj_is_valid(ui.bg))
        lv_obj_del(ui.bg);

    memset(&ui, 0, sizeof(timer_ui_t));
    old_selected_timer_index = -1;
    selected_timer_index = 0;
    _timeout = false;
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

LV_IMG_DECLARE(img_timer);
#define APP_ID "timer"
static int app_main(intent_t i)
{
    (void)i;
    gui_app_regist_msg_handler(APP_ID, msg_handler);
    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(timer), LV_EXT_IMG_GET(img_timer),
                   APP_ID, app_main);
