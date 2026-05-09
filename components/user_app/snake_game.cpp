/**
 * @file snake_game.cpp
 * @brief 贪吃蛇游戏 — LVGL canvas + 标签
 */
#include "snake_game.h"
#include "sketch_player.h"
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <math.h>

static const char *TAG = "Snake";

LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_12);

/* ===== 画布 (480x272, INDEXED_1BIT) ===== */
#define FB_W 480
#define FB_H 272
#define FB_BUF_SIZE (FB_W * FB_H / 8)
static uint8_t s_fb_buf[FB_BUF_SIZE];  /* BSS 静态分配 */

static lv_obj_t *s_canvas = NULL;
static lv_obj_t *s_score_label = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_screen = NULL;

/* 游戏区域偏移 (顶留30px给标签) */
#define AREA_TOP 30

typedef struct { int x, y; } Point;

static struct {
    Point body[SNAKE_MAX_LEN];
    int length;
    SnakeDir_t dir, next_dir;
    Point food;
    int score;
    bool game_over, running;
    unsigned long last_tick;
    int tick_ms;
} g;

/* ===== 帧缓冲助手 ===== */

static inline void fb_clear(void)
{
    memset(s_fb_buf, 0, FB_BUF_SIZE);
    /* memset 会覆盖调色板(前8字节), 重新设置 */
    if (s_canvas) {
        lv_canvas_set_palette(s_canvas, 0, lv_color_white());
        lv_canvas_set_palette(s_canvas, 1, lv_color_black());
    }
}

static inline void fb_px(int x, int y, bool black)
{
    if (x < 0 || x >= FB_W || y < 0 || y >= FB_H) return;
    int idx = y * (FB_W / 8) + x / 8;
    uint8_t bit = 1 << (7 - (x % 8));
    if (black) s_fb_buf[idx] |= bit;
    else       s_fb_buf[idx] &= ~bit;
}

static void fb_cell(int cx, int cy, bool black)
{
    int px = cx * SNAKE_CELL;
    int py = cy * SNAKE_CELL + AREA_TOP;
    for (int dy = 0; dy < SNAKE_CELL; dy++)
        for (int dx = 0; dx < SNAKE_CELL; dx++)
            fb_px(px + dx, py + dy, black);
}

static void fb_border(void)
{
    int x1 = 0, y1 = AREA_TOP;
    int x2 = SNAKE_COLS * SNAKE_CELL - 1;
    int y2 = SNAKE_ROWS * SNAKE_CELL + AREA_TOP - 1;
    for (int x = x1; x <= x2; x++) { fb_px(x, y1, true); fb_px(x, y2, true); }
    for (int y = y1; y <= y2; y++) { fb_px(x1, y, true); fb_px(x2, y, true); }
}

/* ===== 食物 ===== */
static void spawn_food(void)
{
    bool occupied[SNAKE_COLS * SNAKE_ROWS] = {false};
    for (int i = 0; i < g.length; i++)
        occupied[g.body[i].y * SNAKE_COLS + g.body[i].x] = true;
    int free = 0;
    for (int i = 0; i < SNAKE_COLS * SNAKE_ROWS; i++) if (!occupied[i]) free++;
    if (free == 0) { g.game_over = true; return; }
    int t = rand() % free, idx = 0;
    for (int i = 0; i < SNAKE_COLS * SNAKE_ROWS; i++)
        if (!occupied[i] && idx++ == t) { g.food.x = i % SNAKE_COLS; g.food.y = i / SNAKE_COLS; return; }
}

/* ===== 渲染帧 ===== */
static void render_frame(void)
{
    if (!s_canvas) return;
    fb_clear();
    fb_border();
    fb_cell(g.food.x, g.food.y, true);
    for (int i = 0; i < g.length; i++) fb_cell(g.body[i].x, g.body[i].y, true);

    /* 蛇头眼睛 */
    int hx = g.body[0].x * SNAKE_CELL + 2;
    int hy = g.body[0].y * SNAKE_CELL + AREA_TOP + 2;
    fb_px(hx + 2, hy + 2, false);
    fb_px(hx + 6, hy + 2, false);

    /* 更新分数标签 */
    if (s_score_label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Snake  %d", g.score);
        lv_label_set_text(s_score_label, buf);
    }
    if (s_status_label) {
        lv_label_set_text(s_status_label, g.game_over ? "GAME OVER!  按确认重新开始" : "摇杆方向  长按退出");
    }

    /* 标记 canvas 脏区让 LVGL 刷新 */
    lv_obj_invalidate(s_canvas);
}

/* ===== API ===== */

void Snake_Init(void)
{
    memset(&g, 0, sizeof(g));
    /* 清理 BSS 已经清零 */
}

void Snake_CreateScreen(void)
{
    if (!s_screen) {
        s_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_screen, lv_color_white(), 0);

        /* canvas */
        s_canvas = lv_canvas_create(s_screen);
        lv_canvas_set_buffer(s_canvas, s_fb_buf, FB_W, FB_H, LV_IMG_CF_INDEXED_1BIT);
        lv_canvas_set_palette(s_canvas, 0, lv_color_white());
        lv_canvas_set_palette(s_canvas, 1, lv_color_black());
        lv_obj_set_pos(s_canvas, 0, 0);
        lv_obj_set_size(s_canvas, FB_W, FB_H);

        /* 分数标签 */
        s_score_label = lv_label_create(s_screen);
        lv_obj_set_style_text_color(s_score_label, lv_color_black(), 0);
        lv_obj_set_style_text_font(s_score_label, &lv_font_montserrat_16, 0);  /* 内置英文字体 */
        lv_obj_set_pos(s_score_label, 10, 5);

        /* 状态标签 */
        s_status_label = lv_label_create(s_screen);
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(s_status_label, 200, 8);
    }
}

void Snake_Start(void)
{
    g.length = 3;
    g.dir = g.next_dir = SNAKE_DIR_RIGHT;
    g.score = 0;
    g.game_over = false;
    g.running = true;
    g.last_tick = esp_timer_get_time() / 1000;
    g.tick_ms = SNAKE_TICK_MS;

    g.body[0] = {SNAKE_COLS / 2,     SNAKE_ROWS / 2};
    g.body[1] = {SNAKE_COLS / 2 - 1, SNAKE_ROWS / 2};
    g.body[2] = {SNAKE_COLS / 2 - 2, SNAKE_ROWS / 2};

    srand(esp_timer_get_time());
    spawn_food();

    /* 切换到游戏屏幕 */
    if (s_screen) lv_scr_load(s_screen);
    render_frame();
}

void Snake_Stop(void)
{
    g.running = false;
}

void Snake_SetDir(SnakeDir_t dir)
{
    if (!g.running || g.game_over) return;
    switch (dir) {
        case SNAKE_DIR_UP:    if (g.dir == SNAKE_DIR_DOWN)  return; break;
        case SNAKE_DIR_DOWN:  if (g.dir == SNAKE_DIR_UP)    return; break;
        case SNAKE_DIR_LEFT:  if (g.dir == SNAKE_DIR_RIGHT) return; break;
        case SNAKE_DIR_RIGHT: if (g.dir == SNAKE_DIR_LEFT)  return; break;
    }
    g.next_dir = dir;
}

void Snake_Tick(void)
{
    if (!g.running || g.game_over) return;
    unsigned long now = esp_timer_get_time() / 1000;
    if (now - g.last_tick < (unsigned long)g.tick_ms) return;
    g.last_tick = now;

    g.dir = g.next_dir;

    int nx = g.body[0].x, ny = g.body[0].y;
    switch (g.dir) {
        case SNAKE_DIR_UP:    ny--; break;
        case SNAKE_DIR_DOWN:  ny++; break;
        case SNAKE_DIR_LEFT:  nx--; break;
        case SNAKE_DIR_RIGHT: nx++; break;
    }

    if (nx < 0 || nx >= SNAKE_COLS || ny < 0 || ny >= SNAKE_ROWS) {
        ESP_LOGI(TAG, "Wall hit at (%d,%d)", nx, ny);
        g.game_over = true; render_frame(); return;
    }

    bool eat = (nx == g.food.x && ny == g.food.y);
    int check = g.length - (eat ? 0 : 1);
    for (int i = 0; i < check; i++)
        if (nx == g.body[i].x && ny == g.body[i].y) {
            ESP_LOGI(TAG, "Self hit at (%d,%d) len=%d", nx, ny, g.length);
            g.game_over = true; render_frame(); return;
        }

    if (eat) {
        g.length++;
        g.score += 10;
        if (g.tick_ms > 50) g.tick_ms -= 5;
        spawn_food();
        ESP_LOGI(TAG, "Eat! len=%d score=%d speed=%dms", g.length, g.score, g.tick_ms);
    }
    /* 移动身体 */
    for (int i = g.length - 1; i > 0; i--) g.body[i] = g.body[i - 1];
    g.body[0].x = nx;
    g.body[0].y = ny;

    render_frame();
}

int  Snake_GetScore(void)     { return g.score; }
bool Snake_IsGameOver(void)  { return g.game_over; }
bool Snake_IsRunning(void)   { return g.running; }
void Snake_Reset(void)       { Snake_Start(); }
