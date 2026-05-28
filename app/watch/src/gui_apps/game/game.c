/*********************
 *      INCLUDES
 *********************/
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

#define SCR_W 410
#define SCR_H 502

/* 游戏常量 - 适配 410x502 屏幕 */
#define SCREEN_WIDTH  SCR_W
#define SCREEN_HEIGHT SCR_H
#define GROUND_Y      400
#define DINO_WIDTH    64
#define DINO_HEIGHT   64
#define DINO_X        60
#define DINO_GROUND_Y (GROUND_Y - DINO_HEIGHT)
#define OBSTACLE_WIDTH     25
#define OBSTACLE_MIN_HEIGHT 40
#define OBSTACLE_MAX_HEIGHT 60
#define OBSTACLE_SPEED     6
#define ENEMY_SIZE    40
#define ENEMY_MIN_Y   180
#define ENEMY_MAX_Y   320
#define JUMP_VELOCITY     -18
#define VIRTUAL_GRAVITY   1
#define GAME_TICK_MS      30
#define OBSTACLE_SPAWN_DISTANCE 400
#define MAX_OBSTACLES     3
#define MAX_PROJECTILES   5
#define PROJECTILE_SPEED  12
#define PROJECTILE_SIZE   24

LV_IMG_DECLARE(img_dinosaur);
LV_IMG_DECLARE(img_enemy);

typedef enum {
    GAME_STATE_READY,
    GAME_STATE_RUNNING,
    GAME_STATE_OVER
} game_state_t;

typedef struct {
    lv_obj_t *obj;
    int y;
    int velocity;
    bool is_jumping;
    int jump_count;
} dino_t;

typedef enum {
    OBSTACLE_TYPE_GROUND,
    OBSTACLE_TYPE_ENEMY
} obstacle_type_t;

typedef struct {
    lv_obj_t *obj;
    int x;
    int y;
    int height;
    bool active;
    obstacle_type_t type;
} obstacle_t;

typedef struct {
    lv_obj_t *obj;
    int x;
    int y;
    bool active;
} projectile_t;

static game_state_t game_state = GAME_STATE_READY;
static dino_t dino;
static obstacle_t obstacles[MAX_OBSTACLES];
static projectile_t projectiles[MAX_PROJECTILES];
static int score = 0;
static int kills = 0;
static lv_obj_t *score_label = NULL;
static lv_obj_t *kills_label = NULL;
static lv_obj_t *game_over_label = NULL;
static lv_obj_t *restart_label = NULL;
static lv_obj_t *ground_line = NULL;
static lv_obj_t *game_container = NULL;
static lv_timer_t *game_timer = NULL;
static int obstacle_spawn_timer = 0;

static void game_tick(lv_timer_t *timer);
static void reset_game(void);
static void start_game(void);
static void game_over(void);

static bool check_collision(void)
{
    int dino_left = DINO_X;
    int dino_right = DINO_X + DINO_WIDTH;
    int dino_top = dino.y;
    int dino_bottom = dino.y + DINO_HEIGHT;

    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {
            int obs_left, obs_right, obs_top, obs_bottom;

            if (obstacles[i].type == OBSTACLE_TYPE_GROUND) {
                obs_left = obstacles[i].x;
                obs_right = obstacles[i].x + OBSTACLE_WIDTH;
                obs_top = GROUND_Y - obstacles[i].height;
                obs_bottom = GROUND_Y;
            } else {
                obs_left = obstacles[i].x;
                obs_right = obstacles[i].x + ENEMY_SIZE;
                obs_top = obstacles[i].y;
                obs_bottom = obstacles[i].y + ENEMY_SIZE;
            }

            if (dino_right > obs_left && dino_left < obs_right &&
                dino_bottom > obs_top && dino_top < obs_bottom)
                return true;
        }
    }
    return false;
}

static void create_dino(lv_obj_t *parent)
{
    dino.obj = lv_img_create(parent);
    lv_img_set_src(dino.obj, LV_EXT_IMG_GET(img_dinosaur));
    lv_obj_set_size(dino.obj, DINO_WIDTH, DINO_HEIGHT);
    lv_obj_clear_flag(dino.obj, LV_OBJ_FLAG_SCROLLABLE);

    dino.y = DINO_GROUND_Y;
    dino.velocity = 0;
    dino.is_jumping = false;
    dino.jump_count = 0;

    lv_obj_set_pos(dino.obj, DINO_X, dino.y);
}

static void create_projectile(void)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) {
            if (projectiles[i].obj == NULL) {
                projectiles[i].obj = lv_obj_create(game_container);
                lv_obj_set_size(projectiles[i].obj, PROJECTILE_SIZE,
                                PROJECTILE_SIZE);
                lv_obj_set_style_bg_color(projectiles[i].obj,
                                          lv_color_hex(0xFF5555),
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_width(projectiles[i].obj, 0,
                                              LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_radius(projectiles[i].obj,
                                        PROJECTILE_SIZE / 2,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_clear_flag(projectiles[i].obj, LV_OBJ_FLAG_SCROLLABLE);
            }

            projectiles[i].x = DINO_X + DINO_WIDTH;
            projectiles[i].y = dino.y + DINO_HEIGHT / 6 - PROJECTILE_SIZE / 2;
            projectiles[i].active = true;

            lv_obj_set_pos(projectiles[i].obj, projectiles[i].x,
                           projectiles[i].y);
            lv_obj_clear_flag(projectiles[i].obj, LV_OBJ_FLAG_HIDDEN);
            break;
        }
    }
}

static void create_obstacle(int index, obstacle_type_t type)
{
    if (obstacles[index].obj != NULL && obstacles[index].type != type) {
        lv_obj_del(obstacles[index].obj);
        obstacles[index].obj = NULL;
    }

    obstacles[index].type = type;
    obstacles[index].x = SCREEN_WIDTH;
    obstacles[index].active = true;

    if (type == OBSTACLE_TYPE_GROUND) {
        if (obstacles[index].obj == NULL) {
            obstacles[index].obj = lv_obj_create(game_container);
            lv_obj_set_style_border_width(obstacles[index].obj, 0,
                                          LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obstacles[index].obj,
                                      lv_color_hex(0xCCCCCC),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obstacles[index].obj, 3,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_clear_flag(obstacles[index].obj, LV_OBJ_FLAG_SCROLLABLE);
        }

        obstacles[index].height =
            OBSTACLE_MIN_HEIGHT +
            (rand() % (OBSTACLE_MAX_HEIGHT - OBSTACLE_MIN_HEIGHT));
        obstacles[index].y = GROUND_Y - obstacles[index].height;

        lv_obj_set_size(obstacles[index].obj, OBSTACLE_WIDTH,
                        obstacles[index].height);
    } else {
        if (obstacles[index].obj == NULL) {
            obstacles[index].obj = lv_img_create(game_container);
            lv_img_set_src(obstacles[index].obj, LV_EXT_IMG_GET(img_enemy));
            lv_obj_set_size(obstacles[index].obj, ENEMY_SIZE, ENEMY_SIZE);
            lv_obj_clear_flag(obstacles[index].obj, LV_OBJ_FLAG_SCROLLABLE);
        }

        obstacles[index].y =
            ENEMY_MIN_Y + (rand() % (ENEMY_MAX_Y - ENEMY_MIN_Y));
        obstacles[index].height = ENEMY_SIZE;
    }

    lv_obj_set_pos(obstacles[index].obj, obstacles[index].x,
                   obstacles[index].y);
    lv_obj_clear_flag(obstacles[index].obj, LV_OBJ_FLAG_HIDDEN);
}

static void spawn_obstacle(void)
{
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obstacles[i].active) {
            obstacle_type_t type =
                (rand() % 10 < 6) ? OBSTACLE_TYPE_GROUND : OBSTACLE_TYPE_ENEMY;
            create_obstacle(i, type);
            obstacle_spawn_timer = 0;
            break;
        }
    }
}

static void update_projectiles(void)
{
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (projectiles[i].active) {
            projectiles[i].x += PROJECTILE_SPEED;

            bool hit = false;
            for (int j = 0; j < MAX_OBSTACLES; j++) {
                if (obstacles[j].active &&
                    obstacles[j].type == OBSTACLE_TYPE_ENEMY) {
                    int obs_left = obstacles[j].x;
                    int obs_right = obstacles[j].x + ENEMY_SIZE;
                    int obs_top = obstacles[j].y;
                    int obs_bottom = obstacles[j].y + ENEMY_SIZE;

                    int proj_left = projectiles[i].x;
                    int proj_right = projectiles[i].x + PROJECTILE_SIZE;
                    int proj_top = projectiles[i].y;
                    int proj_bottom = projectiles[i].y + PROJECTILE_SIZE;

                    if (proj_right > obs_left && proj_left < obs_right &&
                        proj_bottom > obs_top && proj_top < obs_bottom) {
                        obstacles[j].active = false;
                        lv_obj_add_flag(obstacles[j].obj, LV_OBJ_FLAG_HIDDEN);
                        hit = true;
                        kills++;
                        score += 5;
                        lv_label_set_text_fmt(kills_label, "击杀: %d", kills);
                        lv_label_set_text_fmt(score_label, "%d", score);
                        break;
                    }
                }
            }

            if (hit || projectiles[i].x > SCREEN_WIDTH) {
                projectiles[i].active = false;
                lv_obj_add_flag(projectiles[i].obj, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_set_x(projectiles[i].obj, projectiles[i].x);
            }
        }
    }
}

static void update_obstacles(void)
{
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {
            obstacles[i].x -= OBSTACLE_SPEED;

            int max_width = (obstacles[i].type == OBSTACLE_TYPE_GROUND)
                                ? OBSTACLE_WIDTH
                                : ENEMY_SIZE;
            if (obstacles[i].x < -max_width) {
                obstacles[i].active = false;
                lv_obj_add_flag(obstacles[i].obj, LV_OBJ_FLAG_HIDDEN);
                score++;
                lv_label_set_text_fmt(score_label, "%d", score);
            } else {
                lv_obj_set_pos(obstacles[i].obj, obstacles[i].x,
                               obstacles[i].y);
            }
        }
    }

    obstacle_spawn_timer += OBSTACLE_SPEED;
    if (obstacle_spawn_timer >= OBSTACLE_SPAWN_DISTANCE)
        spawn_obstacle();
}

static void update_dino(void)
{
    if (dino.is_jumping || dino.y < DINO_GROUND_Y) {
        dino.velocity += VIRTUAL_GRAVITY;
        dino.y += dino.velocity;

        if (dino.y >= DINO_GROUND_Y) {
            dino.y = DINO_GROUND_Y;
            dino.velocity = 0;
            dino.is_jumping = false;
            dino.jump_count = 0;
        }

        lv_obj_set_y(dino.obj, dino.y);
    }
}

static void dino_jump(void)
{
    if (dino.jump_count < 2) {
        dino.is_jumping = true;
        dino.velocity = JUMP_VELOCITY;
        dino.jump_count++;

        if (dino.jump_count == 2) {
            lv_obj_set_style_transform_zoom(dino.obj, 280, 0);
            lv_obj_set_style_transform_zoom(dino.obj, 256,
                                            LV_STATE_DEFAULT);
        }
    }
}

static void dino_fire(void)
{
    create_projectile();
}

static void game_tick(lv_timer_t *timer)
{
    (void)timer;
    if (game_state == GAME_STATE_RUNNING) {
        update_dino();
        update_obstacles();
        update_projectiles();

        if (check_collision()) {
            vibrator_send(300, 70);
            game_over();
        }
    }
}

static void reset_game(void)
{
    score = 0;
    kills = 0;
    obstacle_spawn_timer = 0;

    dino.y = DINO_GROUND_Y;
    dino.velocity = 0;
    dino.is_jumping = false;
    dino.jump_count = 0;
    lv_obj_set_pos(dino.obj, DINO_X, dino.y);

    for (int i = 0; i < MAX_OBSTACLES; i++) {
        obstacles[i].active = false;
        if (obstacles[i].obj != NULL)
            lv_obj_add_flag(obstacles[i].obj, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectiles[i].active = false;
        if (projectiles[i].obj != NULL)
            lv_obj_add_flag(projectiles[i].obj, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text_fmt(score_label, "%d", score);
    lv_label_set_text_fmt(kills_label, "击杀: %d", kills);
}

static void start_game(void)
{
    reset_game();
    game_state = GAME_STATE_RUNNING;

    if (game_over_label)
        lv_obj_add_flag(game_over_label, LV_OBJ_FLAG_HIDDEN);
    if (restart_label)
        lv_obj_add_flag(restart_label, LV_OBJ_FLAG_HIDDEN);
}

static void game_over(void)
{
    game_state = GAME_STATE_OVER;

    if (game_over_label)
        lv_obj_clear_flag(game_over_label, LV_OBJ_FLAG_HIDDEN);
    if (restart_label)
        lv_obj_clear_flag(restart_label, LV_OBJ_FLAG_HIDDEN);
}

static void screen_event_cb(lv_event_t *e)
{
    lv_event_code_t event = lv_event_get_code(e);

    if (event == LV_EVENT_CLICKED) {
        if (game_state == GAME_STATE_READY || game_state == GAME_STATE_OVER) {
            start_game();
        } else if (game_state == GAME_STATE_RUNNING) {
            dino_jump();
        }
    }
}

static void fire_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (game_state == GAME_STATE_RUNNING) {
            dino_fire();
        }
    }
}

static void create_game_ui(lv_obj_t *parent)
{
    game_container = lv_obj_create(parent);
    lv_obj_set_size(game_container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(game_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(game_container, lv_color_hex(0x1A1A1A),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(game_container, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(game_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(game_container, screen_event_cb, LV_EVENT_CLICKED,
                        NULL);

    ground_line = lv_obj_create(game_container);
    lv_obj_set_size(ground_line, SCREEN_WIDTH, 3);
    lv_obj_set_pos(ground_line, 0, GROUND_Y);
    lv_obj_set_style_bg_color(ground_line, lv_color_hex(0xAAAAAA),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ground_line, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(ground_line, LV_OBJ_FLAG_SCROLLABLE);

    /* 分数和击杀数容器 */
    lv_obj_t *score_container = lv_obj_create(game_container);
    lv_obj_set_size(score_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(score_container, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_opa(score_container, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(score_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(score_container, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(score_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(score_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(score_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    score_label = lv_label_create(score_container);
    lv_label_set_text(score_label, "0");
    lv_ext_set_local_font(score_label, FONT_TITLE, lv_color_hex(0xFFFFFF));

    lv_obj_t *separator = lv_label_create(score_container);
    lv_label_set_text(separator, " | ");
    lv_ext_set_local_font(separator, FONT_NORMAL, lv_color_hex(0xAAAAAA));

    kills_label = lv_label_create(score_container);
    lv_label_set_text(kills_label, "击杀: 0");
    lv_ext_set_local_font(kills_label, FONT_NORMAL, lv_color_hex(0xFF5555));

    game_over_label = lv_label_create(game_container);
    lv_label_set_text(game_over_label, "游戏结束");
    lv_obj_align(game_over_label, LV_ALIGN_CENTER, 0, -40);
    lv_ext_set_local_font(game_over_label, FONT_TITLE, lv_color_hex(0xFF5555));
    lv_obj_add_flag(game_over_label, LV_OBJ_FLAG_HIDDEN);

    restart_label = lv_label_create(game_container);
    lv_label_set_text(restart_label, "点击重新开始");
    lv_obj_align(restart_label, LV_ALIGN_CENTER, 0, 10);
    lv_ext_set_local_font(restart_label, FONT_NORMAL, lv_color_hex(0xAAAAAA));
    lv_obj_add_flag(restart_label, LV_OBJ_FLAG_HIDDEN);

    create_dino(game_container);

    for (int i = 0; i < MAX_OBSTACLES; i++) {
        obstacles[i].obj = NULL;
        obstacles[i].active = false;
    }

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectiles[i].obj = NULL;
        projectiles[i].active = false;
    }

    /* 发射按钮 */
    lv_obj_t *fire_btn = lv_btn_create(parent);
    lv_obj_set_size(fire_btn, 250, 80);
    lv_obj_set_style_bg_color(fire_btn, lv_color_hex(0x424145),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(fire_btn, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(fire_btn, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(fire_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(fire_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(fire_btn, fire_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *fire_label = lv_label_create(fire_btn);
    lv_ext_set_local_font(fire_label, FONT_TITLE, lv_color_hex(0xc0c0c0));
    lv_label_set_text(fire_label, "开火");
    lv_obj_center(fire_label);

    game_timer = lv_timer_create(game_tick, GAME_TICK_MS, NULL);
}

static void on_start(void)
{
    create_game_ui(lv_scr_act());
    game_state = GAME_STATE_READY;
    lv_img_cache_invalidate_src(NULL);
}

static void on_resume(void)
{
}

static void on_pause(void)
{
    if (game_state == GAME_STATE_RUNNING)
        game_state = GAME_STATE_READY;
}

static void on_stop(void)
{
    if (game_timer) {
        lv_timer_del(game_timer);
        game_timer = NULL;
    }

    /* game_container 及其子对象（score_label等）随屏幕销毁，
     * 清空所有指针避免悬空引用 */
    score_label = NULL;
    kills_label = NULL;
    game_over_label = NULL;
    restart_label = NULL;
    ground_line = NULL;
    game_container = NULL;

    for (int i = 0; i < MAX_OBSTACLES; i++) {
        obstacles[i].obj = NULL;
        obstacles[i].active = false;
    }

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectiles[i].obj = NULL;
        projectiles[i].active = false;
    }
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

LV_IMG_DECLARE(img_game);
#define APP_ID "game"
static int app_main(intent_t i)
{
    (void)i;
    gui_app_regist_msg_handler(APP_ID, msg_handler);
    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(game), LV_EXT_IMG_GET(img_game),
                   APP_ID, app_main);
