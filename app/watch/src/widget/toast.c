#include "my_widget.h"

#include "lvsf_font.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <string.h>

// Toast layout config
#define TOAST_WIDTH          300
#define TOAST_HEIGHT         64
#define TOAST_TOP_MARGIN     18
#define TOAST_SPACING        10
#define TOAST_AUTO_HIDE_MS   5000
#define TOAST_ANIM_IN_MS     260
#define TOAST_ANIM_OUT_MS    220
#define TOAST_ANIM_MOVE_MS   180
#define TOAST_SWIPE_HIDE_THRESHOLD  24
#define TOAST_RADIUS         30
#define TOAST_BG_COLOR_TIP   0xe0e0e0
#define TOAST_BG_COLOR_WARN  0xff0000
#define TOAST_TEXT_MAX_CHARS 24
#define TOAST_TEXT_SCROLL_SPEED 100
#define TOAST_MAX_VISIBLE_COUNT 6


typedef struct ToastItem {
    lv_obj_t * panel;
    lv_obj_t * text;
    lv_timer_t * auto_hide_timer;
    bool is_hiding;
    myui_toast_type_t type;
    lv_coord_t press_start_y;
    bool press_tracking;
    bool swipe_consumed;
} ToastItem;

static ToastItem * s_toasts[TOAST_MAX_VISIBLE_COUNT];
static uint8_t s_toast_count = 0;
static bool s_toast_style_inited = false;
static lv_style_t s_style_tip;
static lv_style_t s_style_warning;

static void toast_hide_item(ToastItem * item);

static int32_t toast_target_y_by_index(uint8_t idx)
{
    return TOAST_TOP_MARGIN + (int32_t)idx * (TOAST_HEIGHT + TOAST_SPACING);
}

static int32_t toast_find_index(ToastItem * item)
{
    uint8_t i = 0;
    for(i = 0; i < s_toast_count; i++) {
        if(s_toasts[i] == item) return (int32_t)i;
    }
    return -1;
}

static uint32_t toast_text_len(const char *text)
{
    uint32_t len = 0;
    if(!text) return 0;
    while(text[len] != '\0') {
        len++;
    }
    return len;
}

static void toast_remove_at(uint8_t idx)
{
    uint8_t i = 0;
    if(idx >= s_toast_count) return;
    for(i = idx; i + 1 < s_toast_count; i++) {
        s_toasts[i] = s_toasts[i + 1];
    }
    s_toasts[s_toast_count - 1] = NULL;
    s_toast_count--;
}

static void toast_insert_back(ToastItem *item)
{
    if(s_toast_count >= TOAST_MAX_VISIBLE_COUNT) return;
    s_toasts[s_toast_count] = item;
    s_toast_count++;
}

static void toast_item_set_y(void * var, int32_t v)
{
    ToastItem * item = (ToastItem *)var;
    if(item && item->panel && lv_obj_is_valid(item->panel)) {
        lv_obj_set_y(item->panel, v);
    }
}

static void toast_relayout_animated(void)
{
    uint8_t i = 0;
    for(i = 0; i < s_toast_count; i++) {
        ToastItem * item = s_toasts[i];
        if(!item || !item->panel || !lv_obj_is_valid(item->panel) || item->is_hiding) continue;

        int32_t target_y = toast_target_y_by_index(i);
        int32_t current_y = lv_obj_get_y(item->panel);
        if(current_y == target_y) continue;

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, item);
        lv_anim_set_exec_cb(&a, toast_item_set_y);
        lv_anim_set_time(&a, TOAST_ANIM_MOVE_MS);
        lv_anim_set_values(&a, current_y, target_y);
        lv_anim_start(&a);
    }
}

static void toast_auto_hide_timer_cb(lv_timer_t * timer)
{
    ToastItem * item = (ToastItem *)timer->user_data;
    toast_hide_item(item);
}

static void toast_hide_ready_cb(lv_anim_t * anim)
{
    ToastItem * item = (ToastItem *)anim->var;
    int32_t idx = -1;
    if(!item) return;

    if(item->panel && lv_obj_is_valid(item->panel)) {
        lv_obj_del(item->panel);
    }
    item->panel = NULL;

    if(item->auto_hide_timer) {
        lv_timer_del(item->auto_hide_timer);
        item->auto_hide_timer = NULL;
    }

    idx = toast_find_index(item);
    if(idx >= 0) toast_remove_at((uint8_t)idx);

    lv_mem_free(item);
}

static void toast_hide_item(ToastItem * item)
{
    int32_t idx = -1;
    if(!item || item->is_hiding) return;

    if(!item->panel || !lv_obj_is_valid(item->panel)) {
        if(item->auto_hide_timer) {
            lv_timer_del(item->auto_hide_timer);
            item->auto_hide_timer = NULL;
        }
        idx = toast_find_index(item);
        if(idx >= 0) toast_remove_at((uint8_t)idx);
        lv_mem_free(item);
        return;
    }

    item->is_hiding = true;

    if(item->auto_hide_timer) {
        lv_timer_del(item->auto_hide_timer);
        item->auto_hide_timer = NULL;
    }

    idx = toast_find_index(item);
    if(idx >= 0) toast_remove_at((uint8_t)idx);

    toast_relayout_animated();

    int32_t current_y = lv_obj_get_y(item->panel);
    int32_t hide_y = -TOAST_HEIGHT - TOAST_TOP_MARGIN;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, item);
    lv_anim_set_exec_cb(&a, toast_item_set_y);
    lv_anim_set_ready_cb(&a, toast_hide_ready_cb);
    lv_anim_set_time(&a, TOAST_ANIM_OUT_MS);
    lv_anim_set_values(&a, current_y, hide_y);
    lv_anim_start(&a);
}

static void toast_press_swipe_cb(lv_event_t * e)
{
    ToastItem * item = (ToastItem *)lv_event_get_user_data(e);
    if(!item || item->is_hiding) return;

    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_PRESSED) {
        lv_indev_t * indev = lv_indev_get_act();
        if(!indev) return;

        lv_point_t p;
        lv_indev_get_point(indev, &p);
        item->press_start_y = p.y;
        item->press_tracking = true;
        item->swipe_consumed = false;
        return;
    }

    if(code == LV_EVENT_PRESSING) {
        if(!item->press_tracking || item->swipe_consumed || s_toast_count == 0) return;

        lv_indev_t * indev = lv_indev_get_act();
        if(!indev) return;

        lv_point_t p;
        lv_indev_get_point(indev, &p);
        lv_coord_t dy = item->press_start_y - p.y;
        if(dy >= TOAST_SWIPE_HIDE_THRESHOLD) {
            item->swipe_consumed = true;
            // Every swipe hides only the latest (top-most) toast.
            toast_hide_item(s_toasts[0]);
        }
        return;
    }

    if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        item->press_tracking = false;
        item->swipe_consumed = false;
    }
}

static void toast_init_styles_once(void)
{
    if(s_toast_style_inited) return;

    lv_style_init(&s_style_tip);
    lv_style_set_radius(&s_style_tip, TOAST_RADIUS);
    lv_style_set_bg_opa(&s_style_tip, LV_OPA_COVER);
    lv_style_set_bg_color(&s_style_tip, lv_color_hex(TOAST_BG_COLOR_TIP));
    lv_style_set_border_width(&s_style_tip, 0);
    lv_style_set_pad_left(&s_style_tip, 14);
    lv_style_set_pad_right(&s_style_tip, 14);
    lv_style_set_pad_top(&s_style_tip, 10);
    lv_style_set_pad_bottom(&s_style_tip, 10);
    lv_style_set_shadow_width(&s_style_tip, 18);
    lv_style_set_shadow_opa(&s_style_tip, LV_OPA_30);
    lv_style_set_shadow_color(&s_style_tip, lv_color_hex(0x000000));

    lv_style_init(&s_style_warning);
    lv_style_set_radius(&s_style_warning, TOAST_RADIUS);
    lv_style_set_bg_opa(&s_style_warning, LV_OPA_COVER);
    lv_style_set_bg_color(&s_style_warning, lv_color_hex(TOAST_BG_COLOR_WARN));
    lv_style_set_border_width(&s_style_warning, 0);
    lv_style_set_pad_left(&s_style_warning, 14);
    lv_style_set_pad_right(&s_style_warning, 14);
    lv_style_set_pad_top(&s_style_warning, 10);
    lv_style_set_pad_bottom(&s_style_warning, 10);
    lv_style_set_shadow_width(&s_style_warning, 18);
    lv_style_set_shadow_opa(&s_style_warning, LV_OPA_30);
    lv_style_set_shadow_color(&s_style_warning, lv_color_hex(0x000000));

    s_toast_style_inited = true;
}

void myui_toast_show(myui_toast_type_t type, const char *text)
{
    ToastItem * item = NULL;
    toast_init_styles_once();

    lv_obj_t * screen = lv_layer_top();
    if(s_toast_count >= TOAST_MAX_VISIBLE_COUNT) {
        // Keep oldest on top: remove the oldest (top) when overflow.
        toast_hide_item(s_toasts[0]);
    }

    item = (ToastItem *)lv_mem_alloc(sizeof(ToastItem));
    if(!item) return;
    lv_memset_00(item, sizeof(ToastItem));
    item->is_hiding = false;
    item->type = type;
    item->auto_hide_timer = NULL;
    item->press_start_y = 0;
    item->press_tracking = false;
    item->swipe_consumed = false;

    item->panel = lv_obj_create(screen);
    lv_obj_remove_style_all(item->panel);
    lv_obj_set_size(item->panel, TOAST_WIDTH, TOAST_HEIGHT);
    lv_obj_set_x(item->panel, (LV_HOR_RES - TOAST_WIDTH) / 2);
    lv_obj_set_y(item->panel, -TOAST_HEIGHT - TOAST_TOP_MARGIN);
    lv_obj_add_flag(item->panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(item->panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(item->panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(item->panel, toast_press_swipe_cb, LV_EVENT_PRESSED, item);
    lv_obj_add_event_cb(item->panel, toast_press_swipe_cb, LV_EVENT_PRESSING, item);
    lv_obj_add_event_cb(item->panel, toast_press_swipe_cb, LV_EVENT_RELEASED, item);
    lv_obj_add_event_cb(item->panel, toast_press_swipe_cb, LV_EVENT_PRESS_LOST, item);

    if(type == MYUI_TOAST_TYPE_WARNING) lv_obj_add_style(item->panel, &s_style_warning, 0);
    else lv_obj_add_style(item->panel, &s_style_tip, 0);

    item->text = lv_label_create(item->panel);
    lv_obj_set_width(item->text, TOAST_WIDTH - 36);
    lv_obj_clear_flag(item->text, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(item->text, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    const char * show_text = text ? text : "toast has no content";
    uint32_t text_len = toast_text_len(show_text);
    if(text_len > TOAST_TEXT_MAX_CHARS) {
        lv_label_set_long_mode(item->text, LV_LABEL_LONG_SCROLL);
        lv_obj_set_style_anim_speed(item->text, TOAST_TEXT_SCROLL_SPEED, 0);
    }
    else {
        lv_label_set_long_mode(item->text, LV_LABEL_LONG_CLIP);
    }
    lv_label_set_text(item->text, show_text);
    if (type == MYUI_TOAST_TYPE_WARNING) {
        lv_obj_set_style_text_color(item->text, lv_color_hex(0xffffff), 0);
    }
    else
    {
        lv_obj_set_style_text_color(item->text, lv_color_hex(0x212121), 0);
    }
    lv_ext_set_local_font(item->text, FONT_SUBTITLE,
                          lv_obj_get_style_text_color(item->text, 0));
    lv_obj_set_style_text_align(item->text, LV_ALIGN_CENTER, 0);
    lv_obj_center(item->text);

    toast_insert_back(item);
    toast_relayout_animated();

    // Slide in from the top to target position.
    lv_anim_t in_anim;
    lv_anim_init(&in_anim);
    lv_anim_set_var(&in_anim, item);
    lv_anim_set_exec_cb(&in_anim, toast_item_set_y);
    lv_anim_set_time(&in_anim, TOAST_ANIM_IN_MS);
    lv_anim_set_values(&in_anim, -TOAST_HEIGHT - TOAST_TOP_MARGIN,
                       toast_target_y_by_index(s_toast_count - 1));
    lv_anim_start(&in_anim);

    item->auto_hide_timer = lv_timer_create(toast_auto_hide_timer_cb, TOAST_AUTO_HIDE_MS, item);
}