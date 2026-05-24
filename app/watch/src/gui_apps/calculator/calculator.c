/*********************
 *      INCLUDES
 *********************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <rtthread.h>
#include <rtdevice.h>

#include "app_mem.h"
#include "gui_app_fwk.h"
#include "littlevgl2rtt.h"
#include "lv_ex_data.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"
#include "lvsf_comp.h"
#include "my_widget.h"
#include "vibrator_manager.h"
#include "bignum_calc.h"

#define SCR_W LV_HOR_RES_MAX
#define SCR_H LV_VER_RES_MAX
#define MAX_INPUT_LEN 40       /* 输入缓冲区最大长度 */

/* 全局UI对象 */
static lv_obj_t *display_label = NULL;
static lv_obj_t *btnm = NULL;

/* 当前选中的运算符按钮ID，-1表示无 */
static int16_t selected_op_btn_id = -1;

/* 计算器状态 */
typedef struct {
    bignum_val_t current_value;     /* 当前显示的值 */
    bignum_val_t stored_value;      /* 存储的值（用于连续运算） */
    char display_str[64];           /* 显示字符串 */
    char input_str[MAX_INPUT_LEN];  /* 输入缓冲区 */
    char pending_op;                /* 待执行的运算符 */
    bool has_input;                 /* 是否有新输入 */
    bool error_state;               /* 错误状态 */
    bool just_calculated;           /* 刚刚完成计算 */
} calc_state_t;

static calc_state_t calc;

/* 根据文本长度自动选择合适的字体大小 */
static void adjust_font_by_length(const char *text)
{
    int len = strlen(text);
    uint16_t font_size;
    if (len <= 8) {
        font_size = FONT_HUGE;
    } else if (len <= 12) {
        font_size = FONT_BIGL;
    } else if (len <= 16) {
        font_size = FONT_TITLE;
    } else {
        font_size = FONT_SUBTITLE;
    }
    lv_ext_set_local_font(display_label, font_size, lv_color_hex(0xe8e8e8));
}

/* 更新显示 */
static void update_display(void)
{
    if (!display_label || !lv_obj_is_valid(display_label)) return;

    if (calc.error_state) {
        lv_label_set_text(display_label, "Error");
        adjust_font_by_length("Error");
        return;
    }

    const char *text_to_show = NULL;

    if (calc.has_input && calc.input_str[0] != '\0') {
        text_to_show = calc.input_str;
        /* 检查输入长度是否接近上限 */
        if (strlen(calc.input_str) >= MAX_INPUT_LEN - 5) {
            myui_toast_show(MYUI_TOAST_TYPE_TIP, "输入接近最大长度");
        }
    } else {
        /* 格式化并显示计算结果 */
        bignum_err_t err = bignum_val_to_str(&calc.current_value,
                                              calc.display_str,
                                              sizeof(calc.display_str));
        if (err == BIGNUM_OK) {
            text_to_show = calc.display_str;
        } else {
            lv_label_set_text(display_label, "Error");
            adjust_font_by_length("Error");
            calc.error_state = true;
            return;
        }
    }

    if (text_to_show) {
        adjust_font_by_length(text_to_show);
        lv_label_set_text(display_label, text_to_show);
    }
}

/* 执行计算 */
static void perform_calculation(void)
{
    if (calc.pending_op == '\0' || calc.error_state) return;

    bignum_val_t operand;
    bignum_val_init(&operand);

    if (calc.has_input && calc.input_str[0] != '\0') {
        bignum_err_t err = bignum_val_from_str(&operand, calc.input_str);
        if (err != BIGNUM_OK) {
            myui_toast_show(MYUI_TOAST_TYPE_TIP, "输入无效");
            bignum_val_free(&operand);
            calc.error_state = true;
            return;
        }
    } else {
        bignum_val_copy(&operand, &calc.current_value);
    }

    bignum_val_t result;
    bignum_val_init(&result);
    bignum_err_t err = BIGNUM_OK;

    switch (calc.pending_op) {
    case '+':
        err = bignum_val_add(&result, &calc.stored_value, &operand);
        break;
    case '-':
        err = bignum_val_sub(&result, &calc.stored_value, &operand);
        break;
    case '*':
        err = bignum_val_mul(&result, &calc.stored_value, &operand);
        break;
    case '/':
        if (bignum_val_is_zero(&operand)) {
            myui_toast_show(MYUI_TOAST_TYPE_TIP, "不能除以零");
            bignum_val_free(&operand);
            bignum_val_free(&result);
            calc.error_state = true;
            return;
        }
        err = bignum_val_div(&result, &calc.stored_value, &operand);
        break;
    }

    bignum_val_free(&operand);

    if (err != BIGNUM_OK) {
        myui_toast_show(MYUI_TOAST_TYPE_TIP, "计算溢出");
        bignum_val_free(&result);
        calc.error_state = true;
        return;
    }

    bignum_val_free(&calc.current_value);
    bignum_val_copy(&calc.current_value, &result);
    bignum_val_free(&result);

    calc.has_input = false;
    calc.input_str[0] = '\0';
    calc.just_calculated = true;
}

/* 处理数字输入 */
static void handle_digit(const char *digit)
{
    if (calc.error_state) return;

    if (calc.just_calculated) {
        /* 刚完成计算，开始新的输入 */
        calc.input_str[0] = '\0';
        calc.just_calculated = false;
    }

    size_t len = strlen(calc.input_str);
    if (len < sizeof(calc.input_str) - 1) {
        calc.input_str[len] = digit[0];
        calc.input_str[len + 1] = '\0';
        calc.has_input = true;
    } else {
        myui_toast_show(MYUI_TOAST_TYPE_TIP, "输入长度超限");
    }
}

/* 处理小数点 */
static void handle_decimal(void)
{
    if (calc.error_state) return;

    if (calc.just_calculated) {
        strcpy(calc.input_str, "0.");
        calc.just_calculated = false;
        calc.has_input = true;
        return;
    }

    if (!calc.has_input || calc.input_str[0] == '\0') {
        strcpy(calc.input_str, "0.");
        calc.has_input = true;
    } else if (strchr(calc.input_str, '.') == NULL) {
        size_t len = strlen(calc.input_str);
        if (len < sizeof(calc.input_str) - 1) {
            calc.input_str[len] = '.';
            calc.input_str[len + 1] = '\0';
        }
    } else {
        myui_toast_show(MYUI_TOAST_TYPE_TIP, "已有小数点");
    }
}

/* 取消所有运算符按钮的高亮 */
static void uncheck_all_op_btns(void)
{
    if (!btnm || !lv_obj_is_valid(btnm)) return;

    for (int i = 3; i <= 15; i += 4) {
        lv_btnmatrix_clear_btn_ctrl(btnm, i, LV_BTNMATRIX_CTRL_CHECKED);
    }
    selected_op_btn_id = -1;
}

/* 高亮指定运算符按钮 */
static void check_op_btn(int id)
{
    if (!btnm || !lv_obj_is_valid(btnm)) return;

    uncheck_all_op_btns();
    if (id >= 0 && id <= 15) {
        lv_btnmatrix_set_btn_ctrl(btnm, id, LV_BTNMATRIX_CTRL_CHECKED);
        selected_op_btn_id = id;
    }
}

/* 处理运算符 */
static void handle_operator(char op, int btn_id)
{
    if (calc.error_state) return;

    if (calc.pending_op != '\0' && calc.has_input) {
        /* 连续运算：先算出前一步结果 */
        perform_calculation();
        if (calc.error_state) return;
        /* 用计算结果作为下一步的左操作数 */
        bignum_val_copy(&calc.stored_value, &calc.current_value);
    } else if (calc.has_input) {
        /* 第一次按运算符：将输入的数字存为左操作数 */
        bignum_val_from_str(&calc.stored_value, calc.input_str);
    } else {
        /* 刚算完结果后按运算符：用当前结果作为左操作数 */
        bignum_val_copy(&calc.stored_value, &calc.current_value);
    }

    calc.pending_op = op;
    calc.has_input = false;
    calc.input_str[0] = '\0';
    calc.just_calculated = false;

    /* 高亮当前运算符按钮 */
    check_op_btn(btn_id);
}

/* 处理等号 */
static void handle_equal(void)
{
    if (calc.error_state) return;

    if (calc.pending_op != '\0') {
        perform_calculation();
        calc.pending_op = '\0';
    }
}

/* 处理清除 */
static void handle_clear(void)
{
    bignum_val_set_zero(&calc.current_value);
    bignum_val_set_zero(&calc.stored_value);
    calc.display_str[0] = '\0';
    calc.input_str[0] = '\0';
    calc.pending_op = '\0';
    calc.has_input = false;
    calc.error_state = false;
    calc.just_calculated = false;
    uncheck_all_op_btns();
}

/* 处理退格 */
static void handle_backspace(void)
{
    if (calc.error_state) return;

    if (calc.has_input && calc.input_str[0] != '\0') {
        size_t len = strlen(calc.input_str);
        if (len > 0) {
            calc.input_str[len - 1] = '\0';
            if (calc.input_str[0] == '\0') {
                calc.has_input = false;
            }
        }
    } else {
        myui_toast_show(MYUI_TOAST_TYPE_TIP, "没有可删除的内容");
    }
}

/* 处理正负号 */
static void handle_sign(void)
{
    if (calc.error_state) return;

    if (calc.has_input && calc.input_str[0] != '\0') {
        if (calc.input_str[0] == '-') {
            memmove(calc.input_str, calc.input_str + 1, strlen(calc.input_str));
        } else {
            size_t len = strlen(calc.input_str);
            if (len < sizeof(calc.input_str) - 1) {
                memmove(calc.input_str + 1, calc.input_str, len + 1);
                calc.input_str[0] = '-';
            }
        }
    } else if (bignum_val_is_zero(&calc.current_value)) {
        myui_toast_show(MYUI_TOAST_TYPE_TIP, "当前值为0");
    } else {
        bignum_val_neg(&calc.current_value, &calc.current_value);
    }
}

/* 处理百分号 */
static void handle_percent(void)
{
    if (calc.error_state) return;

    if (calc.has_input && calc.input_str[0] != '\0') {
        bignum_val_t val;
        bignum_val_init(&val);
        bignum_err_t err = bignum_val_from_str(&val, calc.input_str);
        if (err == BIGNUM_OK) {
            bignum_err_t perr = bignum_val_percent(&val, &val);
            if (perr == BIGNUM_OK) {
                bignum_val_to_str(&val, calc.input_str, sizeof(calc.input_str));
            }
        }
        bignum_val_free(&val);
    } else if (bignum_val_is_zero(&calc.current_value)) {
        myui_toast_show(MYUI_TOAST_TYPE_TIP, "当前值为0");
    } else {
        bignum_val_percent(&calc.current_value, &calc.current_value);
    }
}

/* 按钮事件处理 */
static void btn_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    /* 按键震动反馈：短时间、低强度 */
    vibrator_send(50, 30);

    uint16_t id = lv_btnmatrix_get_selected_btn(obj);
    const char *txt = lv_btnmatrix_get_btn_text(obj, id);

    if (!txt || txt[0] == '\0') return;

    if (strcmp(txt, "+/-") == 0) {
        handle_sign();
        uncheck_all_op_btns();
    } else if (strlen(txt) == 1) {
        switch (txt[0]) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            handle_digit(txt);
            uncheck_all_op_btns();
            break;
        case '.':
            handle_decimal();
            uncheck_all_op_btns();
            break;
        case '+':
            handle_operator('+', id);
            break;
        case '-':
            handle_operator('-', id);
            break;
        case 'x':
            handle_operator('*', id);
            break;
        case '/':
            handle_operator('/', id);
            break;
        case '=':
            handle_equal();
            uncheck_all_op_btns();
            break;
        case 'C':
            handle_clear();
            uncheck_all_op_btns();
            break;
        case 'D':
            handle_backspace();
            break;
        case '%':
            handle_percent();
            break;
        }
    }

    update_display();
}

/* 创建显示区域 */
static void create_display(lv_obj_t *parent)
{
    /* 显示标签 - 直接放在屏幕上，无背景色 */
    display_label = lv_label_create(parent);
    lv_obj_set_style_text_color(display_label, lv_color_hex(0xe8e8e8), LV_PART_MAIN);
    lv_ext_set_local_font(display_label, FONT_HUGE, lv_color_hex(0xe8e8e8));
    lv_label_set_text(display_label, "0");
    lv_obj_set_style_text_align(display_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_long_mode(display_label, LV_LABEL_LONG_SCROLL); /* 超长来回滚动显示 */
    lv_obj_set_width(display_label, SCR_W - 60);
    lv_obj_align(display_label, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_pad_top(display_label, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(display_label, 0, LV_PART_MAIN);
    lv_obj_clear_flag(display_label, LV_OBJ_FLAG_SCROLLABLE);
}

/* 创建按钮矩阵 */
static void create_buttons(lv_obj_t *parent)
{
    /* 按钮映射：5行4列 */
    static const char *btn_map[] = {
        "C", "D", "%", "/", "\n",
        "7", "8", "9", "x", "\n",
        "4", "5", "6", "-", "\n",
        "1", "2", "3", "+", "\n",
        "+/-", "0", ".", "=",
        ""
    };

    btnm = lv_btnmatrix_create(parent);
    lv_btnmatrix_set_map(btnm, btn_map);
    lv_obj_set_size(btnm, SCR_W - 40, SCR_H - 130);
    lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* 背景样式 */
    lv_obj_set_style_bg_color(btnm, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btnm, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btnm, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(btnm, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btnm, 5, LV_PART_MAIN);

    /* 按键默认样式 - 对齐setting页风格 */
    lv_obj_set_style_bg_color(btnm, lv_color_hex(0x1c1c1c), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(btnm, 15, LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnm, lv_color_hex(0xe8e8e8), LV_PART_ITEMS);
    lv_obj_set_style_border_width(btnm, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(btnm, lv_color_hex(0x303030), LV_PART_ITEMS);
    lv_obj_set_style_border_opa(btnm, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(btnm, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_font(btnm, LV_EXT_FONT_GET(FONT_SUBTITLE), LV_PART_ITEMS);

    /* 按键按下样式 */
    lv_obj_set_style_bg_color(btnm, lv_color_hex(0x2a2a2a), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);

    /* 运算符列样式（第4列：/、x、-、+）：设置CHECKABLE并使用CHECKED样式实现高亮 */
    for (int i = 3; i <= 15; i += 4) {
        lv_btnmatrix_set_btn_ctrl(btnm, i, LV_BTNMATRIX_CTRL_CHECKABLE);
    }
    /* 运算符选中时高亮样式 */
    lv_obj_set_style_bg_color(btnm, lv_color_hex(0x2d5a3d), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btnm, lv_color_hex(0x55aa6c), LV_PART_ITEMS | LV_STATE_CHECKED);

    /* 等号按钮特殊样式 - 使用深绿色背景 */
    lv_obj_set_style_bg_color(btnm, lv_color_hex(0x1a4a2d), LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnm, lv_color_hex(0x55aa6c), LV_PART_ITEMS);

    /* C和D按钮样式 */
    lv_obj_set_style_bg_color(btnm, lv_color_hex(0x424145), LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnm, lv_color_hex(0xc0c0c0), LV_PART_ITEMS);

    /* 添加事件回调 */
    lv_obj_add_event_cb(btnm, btn_event_handler, LV_EVENT_CLICKED, NULL);
}

/* 创建计算器界面 */
static void create_calculator_screen(lv_obj_t *scr)
{
    /* 设置黑色背景 */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    /* 创建显示区域 */
    create_display(scr);

    /* 创建按钮矩阵 */
    create_buttons(scr);
}

static void on_start(void)
{
    lv_obj_t *scr = lv_scr_act();

    /* 初始化大数状态 */
    bignum_val_init(&calc.current_value);
    bignum_val_init(&calc.stored_value);

    handle_clear();
    create_calculator_screen(scr);
    update_display();
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
    /* 释放大数资源 */
    bignum_val_free(&calc.current_value);
    bignum_val_free(&calc.stored_value);

    /* 清理UI资源 */
    display_label = NULL;
    btnm = NULL;
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
