#ifndef APP_WATCH_SRC_WIDGET_MY_WIDGET_H
#define APP_WATCH_SRC_WIDGET_MY_WIDGET_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MYUI_TOAST_TYPE_TIP = 0,
    MYUI_TOAST_TYPE_WARNING = 1
} myui_toast_type_t;

void myui_toast_show(myui_toast_type_t type, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* APP_WATCH_SRC_WIDGET_MY_WIDGET_H */