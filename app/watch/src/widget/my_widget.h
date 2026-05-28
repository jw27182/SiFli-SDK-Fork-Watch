#ifndef APP_WATCH_SRC_WIDGET_MY_WIDGET_H
#define APP_WATCH_SRC_WIDGET_MY_WIDGET_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ================================================================== */
/*  Toast                                                              */
/* ================================================================== */

typedef enum {
    MYUI_TOAST_TYPE_TIP = 0,
    MYUI_TOAST_TYPE_WARNING = 1
} myui_toast_type_t;

void myui_toast_show(myui_toast_type_t type, const char *text);

/* ================================================================== */
/*  Popup dialog                                                       */
/* ================================================================== */

#define MYUI_POPUP_MAX_BTNS  3

/**
 * @brief Button descriptor for myui_popup_show().
 *
 * When the button is clicked:
 *   - If cb != NULL, cb(e) is called with the original user_data.
 *   - The popup overlay is automatically deleted after cb returns.
 *   - If cb == NULL, the popup is simply closed.
 */
typedef struct {
    const char    *text;        /* Button label */
    lv_color_t     text_color;  /* Label color */
    lv_event_cb_t  cb;          /* Click callback (may be NULL) */
    void          *user_data;   /* Passed to cb as lv_event_get_user_data(e) */
} myui_popup_btn_t;

/**
 * @brief Show a modal popup dialog on lv_layer_top().
 *
 * Layout (top → bottom):
 *   [icon]          — optional, e.g. LV_SYMBOL_BELL  (FONT_SUPER)
 *   [title]         — required                        (FONT_HUGE)
 *   [subtitle]      — optional                        (FONT_NORMAL)
 *   [btn0] [btn1] … — 1 ~ MYUI_POPUP_MAX_BTNS        (FONT_NORMAL)
 *
 * Style: dark theme (0x1c1c1c, radius 40) with semi-transparent overlay,
 *        matching the existing timer / alarm UI.
 *
 * @param icon       LV_SYMBOL_xxx string, or NULL to skip.
 * @param title      Main title text (must not be NULL).
 * @param subtitle   Subtitle text, or NULL to skip.
 * @param btns       Array of button descriptors (at least 1).
 * @param btn_count  Number of buttons (1 ~ MYUI_POPUP_MAX_BTNS).
 * @return The overlay lv_obj_t* on lv_layer_top().  The caller may
 *         delete it programmatically (lv_obj_del) if needed.
 *         Returns NULL on error.
 */
lv_obj_t *myui_popup_show(const char            *icon,
                          const char            *title,
                          const char            *subtitle,
                          const myui_popup_btn_t *btns,
                          uint8_t                btn_count);

/**
 * @brief Close the currently visible popup (if any).
 */
void myui_popup_close(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_WATCH_SRC_WIDGET_MY_WIDGET_H */
