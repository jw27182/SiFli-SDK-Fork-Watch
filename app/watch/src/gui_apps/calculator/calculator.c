/*********************
 *      INCLUDES
 *********************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <rtthread.h>
#include <rtdevice.h>

#include "app_mem.h"
#include "gui_app_fwk.h"
#include "littlevgl2rtt.h"
#include "lv_ex_data.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "lvsf_comp.h"

#define SCR_W 410
#define SCR_H 502
#define MAX_NUM 10

static lv_obj_t *ta = NULL;
static lv_obj_t *my_btnm = NULL;
static char result[MAX_NUM];
static char error = 0;
static double before = 0;
static char operate = '\0';
static char num[MAX_NUM];

static double save_num(char *num)
{
    return atof(num);
}

static char judge_num(double num)
{
    char n[100];
    sprintf(n, "%g", num);
    if (strlen(n) < MAX_NUM)
        return 0;
    else
        return 1;
}

static void calculate(double before, double now, char operate, char *num)
{
    double result = now;
    switch (operate) {
    case '+':
        result = before + now;
        break;
    case '-':
        result = before - now;
        break;
    case '*':
        result = before * now;
        break;
    case '/':
        if (now == 0.0) {
            strcpy(num, "EOR");
            return;
        }
        result = before / now;
        break;
    default:
        break;
    }
    snprintf(num, MAX_NUM, "%g", result);
}

static void clear_result(void)
{
    memset(result, 0, MAX_NUM);
    memset(num, 0, MAX_NUM);
    before = 0;
    error = 0;
    operate = '\0';
}

static void handle_button_text(const char *txt)
{
    volatile double now = 0;

    if (error == 0) {
        switch (txt[0]) {
        case '=':
            if (judge_num(before) == 0) {
                now = save_num(num);
                calculate(before, now, operate, num);
                operate = '\0';
                before = save_num(num);
            } else {
                error = 1;
                lv_textarea_set_text(ta, "EOR");
            }
            break;
        case 'D':
            if (strlen(num) > 0)
                num[strlen(num) - 1] = '\0';
            break;
        case '+':
        case '-':
        case 'x':
        case '/':
            now = save_num(num);
            if (operate != '\0') {
                calculate(before, now, operate, num);
                before = save_num(num);
            } else {
                before = now;
            }
            operate = (txt[0] == 'x') ? '*' : txt[0];
            memset(num, 0, MAX_NUM);
            break;
        case ' ':
            break;
        default:
            if (strlen(num) < MAX_NUM - 1)
                num[strlen(num)] = txt[0];
            break;
        }
    }
    if (txt[0] == 'C') {
        clear_result();
        now = 0;
    }
    lv_textarea_set_text(ta, num);
}

static void clear_handler(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        handle_button_text("C");
}

static void delete_handler(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        handle_button_text("D");
}

static void equal_handler(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        handle_button_text("=");
}

static void event_handler(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        uint16_t id = lv_btnmatrix_get_selected_btn(obj);
        const char *txt = lv_btnmatrix_get_btn_text(obj, id);
        handle_button_text(txt);
    }
}

static void ta_event_cb(lv_event_t *event)
{
    (void)event;
}

static lv_obj_t *create_textarea(lv_obj_t *screen)
{
    lv_obj_t *ta = lv_textarea_create(screen);
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
    lv_textarea_set_accepted_chars(ta, "0123456789+-.*/EOR");
    lv_textarea_set_max_length(ta, MAX_NUM);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_text(ta, "");

    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ta, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_ext_set_local_font(ta, FONT_TITLE, lv_color_hex(0xFFFFFF));
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ta, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ta, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_width(ta, lv_pct(53));
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 50);

    /* 清除按钮 */
    lv_obj_t *clear_btn = lv_btn_create(screen);
    lv_obj_set_size(clear_btn, 50, 50);
    lv_obj_set_style_bg_opa(clear_btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(clear_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(clear_btn, ta, LV_ALIGN_OUT_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(clear_btn, clear_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t *clear_label = lv_label_create(clear_btn);
    lv_ext_set_local_font(clear_label, FONT_TITLE, lv_color_hex(0xC17E7C));
    lv_label_set_text(clear_label, "C");
    lv_obj_center(clear_label);

    /* 删除按钮 */
    lv_obj_t *float_btn = lv_btn_create(screen);
    lv_obj_set_size(float_btn, 50, 50);
    lv_obj_add_flag(float_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align_to(float_btn, ta, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(float_btn, delete_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_radius(float_btn, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(float_btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(float_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *del_label = lv_label_create(float_btn);
    lv_ext_set_local_font(del_label, FONT_NORMAL, lv_color_hex(0xFFFFFF));
    lv_label_set_text(del_label, LV_SYMBOL_BACKSPACE);
    lv_obj_center(del_label);

    return ta;
}

static const lv_style_const_prop_t BTNM_BG_STYLE_PROPS[] = {
    LV_STYLE_CONST_PAD_COLUMN(4),
    LV_STYLE_CONST_PAD_ROW(4),
    LV_STYLE_CONST_BG_COLOR(LV_COLOR_MAKE(0x00, 0x00, 0x00)),
    LV_STYLE_CONST_BG_OPA(LV_OPA_TRANSP),
    LV_STYLE_CONST_RADIUS(120),
    LV_STYLE_PROP_INV,
};

static const lv_style_const_prop_t BTNM_KEY_STYLE_PROPS[] = {
    LV_STYLE_CONST_RADIUS(15),
    LV_STYLE_CONST_BG_COLOR(LV_COLOR_MAKE(0x33, 0x33, 0x33)),
    LV_STYLE_CONST_TEXT_COLOR(LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)),
    LV_STYLE_CONST_BG_OPA(LV_OPA_TRANSP),
    LV_STYLE_PROP_INV,
};

static const lv_style_const_prop_t BTNM_KEY_PRESSED_STYLE_PROPS[] = {
    LV_STYLE_CONST_BG_COLOR(LV_COLOR_MAKE(0x4A, 0x4A, 0x4A)),
    LV_STYLE_CONST_BG_OPA(LV_OPA_50),
    LV_STYLE_PROP_INV,
};

LV_STYLE_CONST_INIT(BTNM_BG_STYLE, BTNM_BG_STYLE_PROPS);
LV_STYLE_CONST_INIT(BTNM_KEY_STYLE, BTNM_KEY_STYLE_PROPS);
LV_STYLE_CONST_INIT(BTNM_KEY_PRESSED_STYLE, BTNM_KEY_PRESSED_STYLE_PROPS);

static const char *btnm_map[] = {
    "7", "8", "9", "+", "\n",
    "4", "5", "6", "-", "\n",
    "1", "2", "3", "x", "\n",
    " ", "0", ".", "/", ""
};

static lv_obj_t *create_btnmatrix(lv_obj_t *screen)
{
    lv_obj_t *btnm = lv_btnmatrix_create(screen);
    lv_btnmatrix_set_map(btnm, btnm_map);

    lv_obj_add_style(btnm, (lv_style_t *)&BTNM_BG_STYLE, LV_PART_MAIN);
    lv_obj_add_style(btnm, (lv_style_t *)&BTNM_KEY_STYLE, LV_PART_ITEMS);
    lv_obj_add_style(btnm, (lv_style_t *)&BTNM_KEY_PRESSED_STYLE,
                     LV_PART_ITEMS | LV_STATE_PRESSED);

    lv_obj_set_style_text_font(btnm, LV_EXT_FONT_GET(FONT_NORMAL),
                               LV_PART_ITEMS);

    lv_obj_set_style_text_color(btnm, lv_color_hex(0x3A6D7B),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(btnm, lv_color_hex(0x000000),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_TRANSP,
                            LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_btnmatrix_set_btn_ctrl(btnm, 3, LV_BTNMATRIX_CTRL_CHECKED);
    lv_btnmatrix_set_btn_ctrl(btnm, 7, LV_BTNMATRIX_CTRL_CHECKED);
    lv_btnmatrix_set_btn_ctrl(btnm, 11, LV_BTNMATRIX_CTRL_CHECKED);
    lv_btnmatrix_set_btn_ctrl(btnm, 15, LV_BTNMATRIX_CTRL_CHECKED);

    lv_obj_add_event_cb(btnm, event_handler, LV_EVENT_ALL, NULL);

    lv_obj_set_size(btnm, 340, 360);
    lv_obj_align(btnm, LV_ALIGN_CENTER, -30, 50);

    return btnm;
}

static lv_point_t _separator_line_points[] = {{0, 0}, {360, 0}};

static void create_calculator_screen(lv_obj_t *scr)
{
    ta = create_textarea(scr);

    lv_obj_t *line = lv_line_create(scr);
    lv_line_set_points(line, _separator_line_points, 2);
    lv_obj_set_style_line_color(line, lv_color_hex(0x404040),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(line, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(line, ta, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    my_btnm = create_btnmatrix(scr);

    /* 等号按钮 */
    lv_obj_t *equal_btn = lv_btn_create(scr);
    lv_obj_set_size(equal_btn, 60, 120);
    lv_obj_set_style_bg_color(equal_btn, lv_color_hex(0x3A6D7B),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(equal_btn, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(equal_btn, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(equal_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(equal_btn, LV_ALIGN_RIGHT_MID, -30, 0);
    lv_obj_add_event_cb(equal_btn, equal_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t *equal_label = lv_label_create(equal_btn);
    lv_ext_set_local_font(equal_label, FONT_TITLE, lv_color_hex(0xFFFFFF));
    lv_label_set_text(equal_label, "=");
    lv_obj_center(equal_label);
}

static void on_start(void)
{
    lv_obj_t *scr = lv_scr_act();
    clear_result();
    create_calculator_screen(scr);
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

LV_IMG_DECLARE(img_calculator);
#define APP_ID "calculator"
static int app_main(intent_t i)
{
    (void)i;
    gui_app_regist_msg_handler(APP_ID, msg_handler);
    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(calculator), LV_EXT_IMG_GET(img_calculator),
                   APP_ID, app_main);
