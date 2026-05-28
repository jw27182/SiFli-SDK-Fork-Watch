#include "my_widget.h"

#include "lvsf_font.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal constants                                                 */
/* ------------------------------------------------------------------ */

#define POPUP_BOX_RADIUS      40
#define POPUP_BOX_BG          0x1c1c1c
#define POPUP_BOX_OPA         240
#define POPUP_BTN_RADIUS      22
#define POPUP_BTN_BG          0x424145
#define POPUP_BTN_W           120
#define POPUP_BTN_H           44
#define POPUP_TITLE_COLOR     0xc0c0c0
#define POPUP_SUBTITLE_COLOR  0x747474

/* Per-popup bookkeeping — one active popup at a time. */
typedef struct {
    lv_obj_t       *overlay;
    lv_event_cb_t   user_cb[MYUI_POPUP_MAX_BTNS];
    void           *user_data[MYUI_POPUP_MAX_BTNS];
} popup_ctx_t;

static popup_ctx_t g_popup;

/* ------------------------------------------------------------------ */
/*  Internal callbacks                                                 */
/* ------------------------------------------------------------------ */

static void btn_click_cb(lv_event_t *e)
{
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    /* Invoke the caller's callback first (if any). */
    if (idx < MYUI_POPUP_MAX_BTNS && g_popup.user_cb[idx])
        g_popup.user_cb[idx](e);

    /* Then close the popup. */
    if (g_popup.overlay && lv_obj_is_valid(g_popup.overlay)) {
        lv_obj_del(g_popup.overlay);
        g_popup.overlay = NULL;
    }
}

static void overlay_click_cb(lv_event_t *e)
{
    /* Block click events from propagating to underlying UI,
     * but do NOT close the popup. */
    (void)e;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

lv_obj_t *myui_popup_show(const char            *icon,
                          const char            *title,
                          const char            *subtitle,
                          const myui_popup_btn_t *btns,
                          uint8_t                btn_count)
{
    if (!title || !btns || btn_count == 0 ||
        btn_count > MYUI_POPUP_MAX_BTNS)
        return NULL;

    /* Close any existing popup first. */
    if (g_popup.overlay && lv_obj_is_valid(g_popup.overlay))
        lv_obj_del(g_popup.overlay);
    memset(&g_popup, 0, sizeof(g_popup));

    /* --- Overlay (semi-transparent, full screen) --- */
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(overlay, overlay_click_cb, LV_EVENT_CLICKED, NULL);
    g_popup.overlay = overlay;

    /* --- Dialog box (centered, flex column layout) --- */
    lv_obj_t *box = lv_obj_create(overlay);
    lv_obj_set_size(box, LV_HOR_RES_MAX - 40, LV_SIZE_CONTENT);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(box, POPUP_BOX_RADIUS,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(box, lv_color_hex(POPUP_BOX_BG),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(box, POPUP_BOX_OPA,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(box, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(box, 24, 0);
    lv_obj_set_style_pad_bottom(box, 24, 0);
    lv_obj_set_style_pad_left(box, 16, 0);
    lv_obj_set_style_pad_right(box, 16, 0);
    lv_obj_set_style_pad_row(box, 8, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, overlay_click_cb, LV_EVENT_CLICKED, NULL);

    /* --- Icon (optional) --- */
    if (icon) {
        lv_obj_t *icon_lbl = lv_label_create(box);
        lv_ext_set_local_font(icon_lbl, FONT_SUPER,
                              lv_color_hex(POPUP_TITLE_COLOR));
        lv_label_set_text(icon_lbl, icon);
    }

    /* --- Title --- */
    lv_obj_t *title_lbl = lv_label_create(box);
    lv_ext_set_local_font(title_lbl, FONT_HUGE,
                          lv_color_hex(POPUP_TITLE_COLOR));
    lv_label_set_text(title_lbl, title);

    /* --- Subtitle (optional) --- */
    if (subtitle) {
        lv_obj_t *sub_lbl = lv_label_create(box);
        lv_ext_set_local_font(sub_lbl, FONT_NORMAL,
                              lv_color_hex(POPUP_SUBTITLE_COLOR));
        lv_label_set_text(sub_lbl, subtitle);
    }

    /* --- Button row --- */
    lv_obj_t *btn_row = lv_obj_create(box);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(btn_row, 8, 0);
    lv_obj_set_style_pad_bottom(btn_row, 4, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0; i < btn_count; i++) {
        lv_obj_t *btn = lv_btn_create(btn_row);
        lv_obj_set_size(btn, POPUP_BTN_W, POPUP_BTN_H);
        lv_obj_set_style_radius(btn, POPUP_BTN_RADIUS,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, lv_color_hex(POPUP_BTN_BG),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(btn, 0,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_ext_set_local_font(lbl, FONT_NORMAL, btns[i].text_color);
        lv_obj_set_width(lbl, POPUP_BTN_W - 16); /* 8px padding each side */

        const char *btn_text = btns[i].text ? btns[i].text : "";
        /* Measure text width to decide mode */
        lv_coord_t text_w = lv_txt_get_width(btn_text, strlen(btn_text),
                                              lv_obj_get_style_text_font(lbl, 0),
                                              0, LV_TEXT_FLAG_NONE);
        if (text_w > POPUP_BTN_W - 16) {
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        } else {
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        }
        lv_label_set_text(lbl, btn_text);
        lv_obj_center(lbl);

        g_popup.user_cb[i]   = btns[i].cb;
        g_popup.user_data[i] = btns[i].user_data;
        lv_obj_add_event_cb(btn, btn_click_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }

    /* Fade-in animation on the overlay background. */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, overlay);
    lv_anim_set_time(&a, 300);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_50);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_bg_opa);
    lv_anim_start(&a);

    return overlay;
}

void myui_popup_close(void)
{
    if (g_popup.overlay && lv_obj_is_valid(g_popup.overlay)) {
        lv_obj_del(g_popup.overlay);
        g_popup.overlay = NULL;
    }
}
