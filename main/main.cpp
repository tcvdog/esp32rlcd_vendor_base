
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <esp_heap_caps.h>

#include "display_bsp.h"
#include "lvgl_bsp.h"
#include "user_app.h"
#include "user_config.h"
#include "codec_bsp.h"
#include "joystick_bsp.h"
#include "button_bsp.h"
#include "ying_image.h"
#include "phrase_data.h"
#include "arithmetic.h"
#include <lvgl.h>
#include "extra/libs/gif/gifdec.h"

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern CodecPort *codecport;

LV_FONT_DECLARE(font_cn_16);
LV_FONT_DECLARE(font_cn_24);

// 照片模式: 跳过 LVGL 刷屏，直接写 RLCD
static bool s_photo_mode = false;
static bool s_sd_ok = false;
static int s_photo_idx = 0;
static int s_photo_max = 0;
static uint8_t *s_fs_data = NULL;

#define PHOTO_W 400
#define PHOTO_H 300
#define PHOTO_FILE_SIZE (PHOTO_W * PHOTO_H / 8)  /* 15000 字节 */

// SDMMC 引脚: CLK=38, CMD=21, D0=39, 1-bit
#define SDMMC_CLK   38
#define SDMMC_CMD   21
#define SDMMC_D0    39
#define SDMMC_WIDTH 1

DisplayPort RlcdPort(12,11,5,40,41,LCD_WIDTH,LCD_HEIGHT);

static void mount_sd(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 3,
        .allocation_unit_size = 16*1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    sdmmc_slot_config_t scfg = SDMMC_SLOT_CONFIG_DEFAULT();
    scfg.width = SDMMC_WIDTH;
    scfg.clk = (gpio_num_t)SDMMC_CLK;
    scfg.cmd = (gpio_num_t)SDMMC_CMD;
    scfg.d0  = (gpio_num_t)SDMMC_D0;

    sdmmc_card_t *card = NULL;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &scfg, &mcfg, &card);
    if (ret == ESP_OK && card) {
        sdmmc_card_print_info(stdout, card);
        s_sd_ok = true;
        ESP_LOGI("PIC", "SD card mounted");
    } else {
        ESP_LOGW("PIC", "SD card mount failed: %d", ret);
    }
}

/* 读取并显示全屏照片 */
static void show_photo(int idx)
{
    if (!s_sd_ok || !s_fs_data) return;

    char path[48];
    snprintf(path, sizeof(path), "/sdcard/sketches/full_%03d.sketch", idx);
    FILE *f = fopen(path, "rb");
    if (!f) { ESP_LOGE("PIC", "no photo %d", idx); return; }
    size_t r = fread(s_fs_data, 1, PHOTO_FILE_SIZE, f);
    fclose(f);
    if (r != PHOTO_FILE_SIZE) { ESP_LOGE("PIC", "short read %d", idx); return; }

    s_photo_mode = true;
    for (int y = 0; y < PHOTO_H; y++) {
        for (int x = 0; x < PHOTO_W; x++) {
            int bi = y * 50 + (x / 8);  // 50 = PHOTO_W / 8
            int bit = 7 - (x & 7);
            int pixel = (s_fs_data[bi] >> bit) & 1;
            RlcdPort.RLCD_SetPixel(x, y, pixel ? ColorBlack : ColorWhite);
        }
    }
    RlcdPort.RLCD_Display();
}

/* 扫描 SD 卡上可用照片数 */
static int scan_photos(void)
{
    if (!s_sd_ok) return 0;
    int n = 0;
    for (int i = 0; i < 256; i++) {
        char p[48];
        snprintf(p, sizeof(p), "/sdcard/sketches/full_%03d.sketch", i);
        FILE *f = fopen(p, "rb");
        if (f) { n = i + 1; fclose(f); }
        else break;
    }
    return n;
}

/* ===== 播放 canon.pcm ===== */

static void Lvgl_FlushCallback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    // 照片模式: 跳过 LVGL 刷屏
    if (s_photo_mode) { lv_disp_flush_ready(drv); return; }
    uint16_t *buf = (uint16_t *)color_map;
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            RlcdPort.RLCD_SetPixel(x, y, (*buf < 0x7fff) ? ColorBlack : ColorWhite);
            buf++;
        }
    }
    RlcdPort.RLCD_Display();
    lv_disp_flush_ready(drv);
}

/* ===== 全局变量 ===== */
static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_status = NULL;
static TaskHandle_t s_play_task = NULL;
static volatile bool s_stop_play = false;
static int s_volume = 90;  // 0-100
static lv_obj_t *s_vol_label = NULL;  // 音量标签指针

static void reset_screen(void)
{
    lv_obj_clean(s_scr);
    s_vol_label = NULL;
    s_status = lv_label_create(s_scr);
    lv_label_set_text(s_status, "");
    lv_obj_set_style_text_font(s_status, &font_cn_16, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x888888), 0);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -10);
}

/* ===== 单词学习 ===== */
#define WORDS_TOTAL      50
#define WORDS_PER_GROUP  15
#define WORD_GROUPS      4    /* 15+15+15+5 */

static const char *s_words_en[WORDS_TOTAL] = {
    "apple","banana","orange","water","bread","milk","cat","dog","bird","fish",
    "book","pen","school","teacher","student","friend","family","house","car","bus",
    "happy","sad","big","small","hot","cold","run","walk","eat","drink",
    "computer","internet","weather","mountain","river","forest","morning","evening",
    "holiday","breakfast","important","beautiful","dangerous","different","difficult",
    "wonderful","remember","understand","practice","knowledge"
};
static const char *s_words_cn[WORDS_TOTAL] = {
    "苹果","香蕉","橙子","水","面包","牛奶","猫","狗","鸟","鱼",
    "书","钢笔","学校","老师","学生","朋友","家庭","房子","汽车","公交",
    "快乐","悲伤","大的","小的","热的","冷的","跑","走","吃","喝",
    "电脑","互联网","天气","山","河流","森林","早上","晚上",
    "假期","早餐","重要的","美丽的","危险的","不同的","困难的",
    "精彩的","记住","理解","练习","知识"
};
static const char *s_words_phonetic[WORDS_TOTAL] = {
    "/ˈæp.əl/","/bəˈnɑː.nə/","/ˈɒr.ɪndʒ/","/ˈwɔː.tər/","/bred/",
    "/mɪlk/","/kæt/","/dɒɡ/","/bɜːd/","/fɪʃ/",
    "/bʊk/","/pen/","/skuːl/","/ˈtiː.tʃər/","/ˈstjuː.dənt/",
    "/frend/","/ˈfæm.əl.i/","/haʊs/","/kɑːr/","/bʌs/",
    "/ˈhæp.i/","/sæd/","/bɪɡ/","/smɔːl/","/hɒt/",
    "/kəʊld/","/rʌn/","/wɔːk/","/iːt/","/drɪŋk/",
    "/kəmˈpjuː.tər/","/ˈɪn.tə.net/","/ˈweð.ər/","/ˈmaʊn.tɪn/","/ˈrɪv.ər/",
    "/ˈfɒr.ɪst/","/ˈmɔː.nɪŋ/","/ˈiːv.nɪŋ/","/ˈhɒl.ɪ.deɪ/","/ˈbrek.fəst/",
    "/ɪmˈpɔː.tənt/","/ˈbjuː.tɪ.fəl/","/ˈdeɪn.dʒər.əs/","/ˈdɪf.ər.ənt/","/ˈdɪf.ɪ.kəlt/",
    "/ˈwʌn.də.fəl/","/rɪˈmem.bər/","/ˌʌn.dəˈstænd/","/ˈpræk.tɪs/","/ˈnɒl.ɪdʒ/"
};

// 每组范围: {起始索引, 单词数}
static const int s_group_range[WORD_GROUPS][2] = {
    {0, 15}, {15, 15}, {30, 15}, {45, 5}
};

static int s_word_idx = 0;       // 全局单词索引 (0~49)
static int s_word_group = 0;     // 当前选中的组 (0~3)
static int s_word_learned[WORDS_TOTAL] = {0}; // 单词学习次数
static int s_group_accessed[WORD_GROUPS] = {0}; // 组访问次数
static bool s_show_cn = false;
static bool s_in_group_sel = true; // true=组选择, false=单词学习

/* ===== 短语学习 ===== */
static int s_ph_idx = 0;          // 全局短语索引
static int s_ph_group = 0;        // 当前组
static int s_ph_learned[PHRASE_COUNT] = {0};
static int s_ph_group_accessed[PHRASE_GROUPS] = {0};
static bool s_ph_show_cn = false;
static bool s_ph_in_group_sel = true;

/* ===== 学习宠物 ===== */
#define PET_ACTIONS 4
#define PET_DECAY_US (60LL * 1000LL * 1000LL)
#define PET_INFO_W 112
#define PET_SPRITE_X 171
#define PET_SPRITE_Y 69
#define PET_SPRITE_W 160
#define PET_SPRITE_H 112
#define PET_ICON_X 140
#define PET_ICON_Y 270
#define PET_ICON_SIZE 24
#define PET_ICON_GAP 8
#define PET_GIF_PATH "/sdcard/cat_cropped_small.gif"
#define PET_GIF_MAX_FRAMES 40

static int s_pet_sel = 0;
static int s_pet_points = 0;      // 学习积分, 用于照顾宠物
static int s_pet_level = 1;
static int s_pet_exp = 0;
static int s_pet_full = 70;
static int s_pet_happy = 70;
static int s_pet_energy = 70;
static int s_pet_clean = 70;
static int64_t s_pet_last_decay = 0;
static char s_pet_msg[48] = "学习获得积分";

typedef struct {
    uint16_t w;
    uint16_t h;
    uint16_t x;
    uint16_t y;
    uint8_t *bits;
} PetGifFrame_t;

static PetGifFrame_t s_pet_gif_frames[PET_GIF_MAX_FRAMES];
static int s_pet_gif_frame_count = 0;
static int s_pet_gif_frame_idx = 0;
static int64_t s_pet_gif_next_us = 0;
static bool s_pet_gif_loaded = false;
static bool s_pet_gif_tried = false;

/* ===== 宠物动画系统 ===== */
#define ANIM_FRAME_US (60LL * 1000LL)  // ~16fps
#define ANIM_TOTAL_FRAMES 70

typedef enum {
    PET_ANIM_NONE = 0,
    PET_ANIM_FEED,
} PetAnim_t;

static PetAnim_t s_pet_anim = PET_ANIM_NONE;
static int s_pet_anim_frame = 0;
static int64_t s_pet_anim_next_us = 0;

/* 饭盆位置 (猫嘴下方) */
#define BOWL_X (PET_SPRITE_X + 44)
#define BOWL_Y (PET_SPRITE_Y + 95)
#define BOWL_W 28
#define BOWL_H 16
#define BOWL_FOOD_MAX 10

/* 爱心位置 (猫头上方) */
#define HEART_CX (PET_SPRITE_X + 75)
#define HEART_CY (PET_SPRITE_Y - 12)

/* 眼睛位置 (画眯眼) */
#define EYE_LX (PET_SPRITE_X + 52)
#define EYE_RX (PET_SPRITE_X + 96)
#define EYE_Y (PET_SPRITE_Y + 36)

/* 打嗝气泡起点 (嘴附近) */
#define BURP_X (PET_SPRITE_X + 70)
#define BURP_Y (PET_SPRITE_Y + 55)

/* 前向声明 - 动画函数在文件后面定义 */
static void pet_start_feed_anim(void);
static void pet_draw_feed_anim(void);

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void pet_award_points(int points)
{
    s_pet_points += points;
    s_pet_exp += points;
    while (s_pet_exp >= s_pet_level * 20) {
        s_pet_exp -= s_pet_level * 20;
        s_pet_level++;
        s_pet_happy = clamp_int(s_pet_happy + 10, 0, 100);
    }
    snprintf(s_pet_msg, sizeof(s_pet_msg), "获得%d学习积分", points);
}

static bool pet_tick(void)
{
    int64_t now = esp_timer_get_time();
    if (s_pet_last_decay == 0) {
        s_pet_last_decay = now;
        return false;
    }

    int steps = (int)((now - s_pet_last_decay) / PET_DECAY_US);
    if (steps <= 0) return false;
    if (steps > 20) steps = 20;

    s_pet_last_decay += (int64_t)steps * PET_DECAY_US;
    s_pet_full = clamp_int(s_pet_full - steps, 0, 100);
    s_pet_energy = clamp_int(s_pet_energy - steps, 0, 100);
    s_pet_clean = clamp_int(s_pet_clean - steps, 0, 100);
    if (s_pet_full < 30 || s_pet_energy < 30 || s_pet_clean < 30) {
        s_pet_happy = clamp_int(s_pet_happy - steps * 2, 0, 100);
    } else {
        s_pet_happy = clamp_int(s_pet_happy - steps, 0, 100);
    }
    return true;
}

static void pet_make_value(char *buf, size_t len, const char *name, int value)
{
    snprintf(buf, len, "%s:%3d", name, value);
}

static void pet_do_action(void)
{
    switch (s_pet_sel) {
        case 0:
            if (s_pet_points < 3) {
                snprintf(s_pet_msg, sizeof(s_pet_msg), "积分不足: 喂食需3分");
                return;
            }
            s_pet_points -= 3;
            s_pet_full = clamp_int(s_pet_full + 25, 0, 100);
            s_pet_happy = clamp_int(s_pet_happy + 5, 0, 100);
            snprintf(s_pet_msg, sizeof(s_pet_msg), "喂食中...");
            pet_start_feed_anim();
            break;
        case 1:
            if (s_pet_points < 2) {
                snprintf(s_pet_msg, sizeof(s_pet_msg), "积分不足: 玩耍需2分");
                return;
            }
            s_pet_points -= 2;
            s_pet_happy = clamp_int(s_pet_happy + 20, 0, 100);
            s_pet_energy = clamp_int(s_pet_energy - 8, 0, 100);
            s_pet_clean = clamp_int(s_pet_clean - 3, 0, 100);
            snprintf(s_pet_msg, sizeof(s_pet_msg), "玩耍成功 -2分");
            break;
        case 2:
            s_pet_energy = clamp_int(s_pet_energy + 25, 0, 100);
            s_pet_full = clamp_int(s_pet_full - 3, 0, 100);
            snprintf(s_pet_msg, sizeof(s_pet_msg), "休息恢复体力");
            break;
        case 3:
            if (s_pet_points < 1) {
                snprintf(s_pet_msg, sizeof(s_pet_msg), "积分不足: 清洁需1分");
                return;
            }
            s_pet_points -= 1;
            s_pet_clean = clamp_int(s_pet_clean + 25, 0, 100);
            s_pet_happy = clamp_int(s_pet_happy + 3, 0, 100);
            snprintf(s_pet_msg, sizeof(s_pet_msg), "清洁成功 -1分");
            break;
        default:
            break;
    }
}

static void lcd_fill_rect(int x, int y, int w, int h, uint8_t color)
{
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= LCD_HEIGHT) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= LCD_WIDTH) continue;
            RlcdPort.RLCD_SetPixel(xx, yy, color);
        }
    }
}

static void lcd_line(int x0, int y0, int x1, int y1, uint8_t color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        if (x0 >= 0 && x0 < LCD_WIDTH && y0 >= 0 && y0 < LCD_HEIGHT) {
            RlcdPort.RLCD_SetPixel(x0, y0, color);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void lcd_fill_ellipse(int cx, int cy, int rx, int ry, uint8_t color)
{
    int rx2 = rx * rx;
    int ry2 = ry * ry;
    int rxy = rx2 * ry2;
    for (int y = -ry; y <= ry; y++) {
        int yy = cy + y;
        if (yy < 0 || yy >= LCD_HEIGHT) continue;
        for (int x = -rx; x <= rx; x++) {
            int xx = cx + x;
            if (xx < 0 || xx >= LCD_WIDTH) continue;
            if (x * x * ry2 + y * y * rx2 <= rxy) {
                RlcdPort.RLCD_SetPixel(xx, yy, color);
            }
        }
    }
}

static void lcd_draw_star_color(int cx, int cy, uint8_t color)
{
    lcd_line(cx - 4, cy, cx + 4, cy, color);
    lcd_line(cx, cy - 4, cx, cy + 4, color);
    RlcdPort.RLCD_SetPixel(cx - 2, cy - 2, color);
    RlcdPort.RLCD_SetPixel(cx + 2, cy - 2, color);
    RlcdPort.RLCD_SetPixel(cx - 2, cy + 2, color);
    RlcdPort.RLCD_SetPixel(cx + 2, cy + 2, color);
}

static void lcd_draw_bowl(int x, int y)
{
    lcd_line(x, y, x + 28, y, ColorBlack);
    lcd_line(x + 3, y + 1, x + 8, y + 8, ColorBlack);
    lcd_line(x + 25, y + 1, x + 20, y + 8, ColorBlack);
    lcd_line(x + 8, y + 8, x + 20, y + 8, ColorBlack);
    RlcdPort.RLCD_SetPixel(x + 13, y - 4, ColorBlack);
    RlcdPort.RLCD_SetPixel(x + 17, y - 3, ColorBlack);
}

static void draw_pet_icon(int idx, int x, int y, bool selected)
{
    uint8_t fg = selected ? ColorWhite : ColorBlack;
    uint8_t bg = selected ? ColorBlack : ColorWhite;
    lcd_fill_rect(x, y, PET_ICON_SIZE, PET_ICON_SIZE, bg);
    lcd_line(x, y, x + PET_ICON_SIZE - 1, y, ColorBlack);
    lcd_line(x, y + PET_ICON_SIZE - 1, x + PET_ICON_SIZE - 1, y + PET_ICON_SIZE - 1, ColorBlack);
    lcd_line(x, y, x, y + PET_ICON_SIZE - 1, ColorBlack);
    lcd_line(x + PET_ICON_SIZE - 1, y, x + PET_ICON_SIZE - 1, y + PET_ICON_SIZE - 1, ColorBlack);

    int cx = x + PET_ICON_SIZE / 2;
    int cy = y + PET_ICON_SIZE / 2;
    switch (idx) {
        case 0: /* food */
            lcd_line(cx - 9, cy - 3, cx + 9, cy - 3, fg);
            lcd_line(cx - 7, cy - 2, cx - 3, cy + 6, fg);
            lcd_line(cx + 7, cy - 2, cx + 3, cy + 6, fg);
            lcd_line(cx - 3, cy + 6, cx + 3, cy + 6, fg);
            RlcdPort.RLCD_SetPixel(cx - 2, cy - 8, fg);
            RlcdPort.RLCD_SetPixel(cx + 3, cy - 7, fg);
            break;
        case 1: /* play */
            lcd_fill_ellipse(cx - 2, cy + 1, 6, 6, fg);
            lcd_line(cx - 6, cy - 2, cx + 3, cy + 5, bg);
            lcd_line(cx - 6, cy + 4, cx + 4, cy - 3, bg);
            lcd_line(cx + 4, cy + 5, cx + 11, cy + 10, fg);
            break;
        case 2: /* sleep */
            lcd_line(cx - 10, cy - 5, cx - 2, cy - 5, fg);
            lcd_line(cx - 2, cy - 5, cx - 10, cy + 2, fg);
            lcd_line(cx - 10, cy + 2, cx - 2, cy + 2, fg);
            lcd_line(cx + 2, cy + 3, cx + 10, cy + 3, fg);
            lcd_line(cx + 10, cy + 3, cx + 2, cy + 10, fg);
            lcd_line(cx + 2, cy + 10, cx + 10, cy + 10, fg);
            break;
        case 3: /* clean */
            lcd_draw_star_color(cx - 5, cy - 4, fg);
            lcd_draw_star_color(cx + 6, cy + 5, fg);
            if (selected) {
                lcd_line(cx - 9, cy + 9, cx + 9, cy - 9, fg);
            }
            break;
        default:
            break;
    }
}

static void draw_pet_icon_bar(void)
{
    int total_w = PET_ICON_SIZE * PET_ACTIONS + PET_ICON_GAP * (PET_ACTIONS - 1);
    int start_x = (LCD_WIDTH - total_w) / 2;
    lcd_fill_rect(start_x - 6, PET_ICON_Y - 6, total_w + 12, PET_ICON_SIZE + 12, ColorWhite);
    for (int i = 0; i < PET_ACTIONS; i++) {
        int x = start_x + i * (PET_ICON_SIZE + PET_ICON_GAP);
        draw_pet_icon(i, x, PET_ICON_Y, i == s_pet_sel);
    }
}

static void pet_clear_gif_frames(void)
{
    for (int i = 0; i < s_pet_gif_frame_count; i++) {
        free(s_pet_gif_frames[i].bits);
        s_pet_gif_frames[i].bits = NULL;
    }
    s_pet_gif_frame_count = 0;
    s_pet_gif_frame_idx = 0;
    s_pet_gif_loaded = false;
}

static bool pet_load_gif_from_sd(void)
{
    if (s_pet_gif_tried) return s_pet_gif_loaded;
    s_pet_gif_tried = true;
    pet_clear_gif_frames();

    FILE *fp = fopen(PET_GIF_PATH, "rb");
    if (!fp) {
        snprintf(s_pet_msg, sizeof(s_pet_msg), "缺少 /cat_cropped.gif");
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 2 * 1024 * 1024) {
        fclose(fp);
        snprintf(s_pet_msg, sizeof(s_pet_msg), "GIF文件无效");
        return false;
    }

    uint8_t *gif_data = (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gif_data) gif_data = (uint8_t *)malloc(sz);
    if (!gif_data) {
        fclose(fp);
        snprintf(s_pet_msg, sizeof(s_pet_msg), "GIF内存不足");
        return false;
    }
    size_t rd = fread(gif_data, 1, sz, fp);
    fclose(fp);
    if (rd != (size_t)sz) {
        free(gif_data);
        snprintf(s_pet_msg, sizeof(s_pet_msg), "GIF读取失败");
        return false;
    }

    gd_GIF *gif = gd_open_gif_data(gif_data);
    if (!gif) {
        free(gif_data);
        snprintf(s_pet_msg, sizeof(s_pet_msg), "GIF解码失败");
        return false;
    }

    size_t frame_bytes = (size_t)gif->width * gif->height * 3;
    uint8_t *rgb = (uint8_t *)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgb) rgb = (uint8_t *)malloc(frame_bytes);
    if (!rgb) {
        gd_close_gif(gif);
        free(gif_data);
        snprintf(s_pet_msg, sizeof(s_pet_msg), "帧缓存不足");
        return false;
    }

    int area_w = PET_SPRITE_W;
    int area_h = PET_SPRITE_H;
    while (s_pet_gif_frame_count < PET_GIF_MAX_FRAMES) {
        int r = gd_get_frame(gif);
        if (r <= 0) break;
        gd_render_frame(gif, rgb);

        PetGifFrame_t *fr = &s_pet_gif_frames[s_pet_gif_frame_count];
        fr->w = area_w;
        fr->h = area_h;
        fr->x = PET_SPRITE_X;
        fr->y = PET_SPRITE_Y;
        size_t bit_bytes = ((size_t)area_w * area_h + 7) / 8;
        fr->bits = (uint8_t *)heap_caps_calloc(1, bit_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!fr->bits) fr->bits = (uint8_t *)calloc(1, bit_bytes);
        if (!fr->bits) break;

        for (int y = 0; y < area_h; y++) {
            int sy = y * gif->height / area_h;
            for (int x = 0; x < area_w; x++) {
                int sx = x * gif->width / area_w;
                uint8_t *p = &rgb[((size_t)sy * gif->width + sx) * 3];
                int lum = ((int)p[0] * 30 + (int)p[1] * 59 + (int)p[2] * 11) / 100;
                if (lum < 150) {
                    size_t bi = ((size_t)y * area_w + x) / 8;
                    int bit = 7 - (x & 7);
                    fr->bits[bi] |= 1 << bit;
                }
            }
        }
        s_pet_gif_frame_count++;
    }

    free(rgb);
    gd_close_gif(gif);
    free(gif_data);

    if (s_pet_gif_frame_count == 0) {
        pet_clear_gif_frames();
        snprintf(s_pet_msg, sizeof(s_pet_msg), "GIF无可用帧");
        return false;
    }

    s_pet_gif_loaded = true;
    s_pet_gif_next_us = esp_timer_get_time();
    snprintf(s_pet_msg, sizeof(s_pet_msg), "已加载cat_cropped.gif");
    return true;
}

static void draw_pet_gif(void)
{
    /* 如果动画播放中, 转去画动画帧 */
    if (s_pet_anim != PET_ANIM_NONE) {
        pet_draw_feed_anim();
        return;
    }

    if (!pet_load_gif_from_sd()) {
        lcd_fill_rect(PET_SPRITE_X, PET_SPRITE_Y, PET_SPRITE_W, PET_SPRITE_H, ColorWhite);
        lcd_draw_bowl(PET_SPRITE_X + 58, PET_SPRITE_Y + 44);
        return;
    }

    int64_t now = esp_timer_get_time();
    if (now >= s_pet_gif_next_us) {
        s_pet_gif_frame_idx = (s_pet_gif_frame_idx + 1) % s_pet_gif_frame_count;
        s_pet_gif_next_us = now + 120LL * 1000LL;
    }

    PetGifFrame_t *fr = &s_pet_gif_frames[s_pet_gif_frame_idx];
    lcd_fill_rect(fr->x, fr->y, fr->w, fr->h, ColorWhite);
    for (int y = 0; y < fr->h; y++) {
        for (int x = 0; x < fr->w; x++) {
            size_t bi = ((size_t)y * fr->w + x) / 8;
            int bit = 7 - (x & 7);
            if ((fr->bits[bi] >> bit) & 1) {
                RlcdPort.RLCD_SetPixel(fr->x + x, fr->y + y, ColorBlack);
            }
        }
    }
    RlcdPort.RLCD_Display();
}

/* ===== 宠物动画绘制 ===== */

static void draw_heart(int cx, int cy, uint8_t color)
{
    lcd_fill_ellipse(cx - 4, cy, 4, 4, color);
    lcd_fill_ellipse(cx + 4, cy, 4, 4, color);
    int i;
    for (i = 0; i <= 6; i++) {
        RlcdPort.RLCD_SetPixel(cx - 6 + i, cy + 2 + i / 2, color);
        RlcdPort.RLCD_SetPixel(cx + 6 - i, cy + 2 + i / 2, color);
    }
}

static void draw_bowl_food(int x, int y, int w, int h, int food_h, uint8_t color)
{
    /* 碗的外框 */
    lcd_line(x, y, x + w, y, color);
    lcd_line(x + 2, y + 1, x + 6, y + h, color);
    lcd_line(x + w - 2, y + 1, x + w - 6, y + h, color);
    lcd_line(x + 6, y + h, x + w - 6, y + h, color);
    /* 碗内食物 */
    lcd_fill_rect(x + 5, y + h - food_h, w - 10, food_h, color);
}

static void draw_burp_bubble(int cx, int cy, int radius, uint8_t color)
{
    lcd_fill_ellipse(cx, cy, radius, radius, color);
    lcd_fill_ellipse(cx + 2, cy - radius - 2, 2, 2, color);
}

static void draw_cat_squint(void)
{
    /* 在眼睛位置画横线 = 眯眼 */
    lcd_fill_rect(EYE_LX - 4, EYE_Y - 1, 10, 3, ColorWhite);   // 清除原眼
    lcd_line(EYE_LX - 4, EYE_Y, EYE_LX + 5, EYE_Y, ColorBlack);
    lcd_fill_rect(EYE_RX - 4, EYE_Y - 1, 10, 3, ColorWhite);
    lcd_line(EYE_RX - 4, EYE_Y, EYE_RX + 5, EYE_Y, ColorBlack);
}

/* 启动喂食动画 */
static void pet_start_feed_anim(void)
{
    s_pet_anim = PET_ANIM_FEED;
    s_pet_anim_frame = 0;
    s_pet_anim_next_us = esp_timer_get_time() + ANIM_FRAME_US;
}

/* 绘制喂食动画帧 */
static void pet_draw_feed_anim(void)
{
    int f = s_pet_anim_frame;
    uint8_t black = ColorBlack;
    uint8_t white = ColorWhite;
    int cat_shift = 0;     /* 猫下移像素 */
    bool show_bowl = false;
    int food_h = 0;
    bool show_squint = false;
    bool show_burp = false;
    int burp_rise = 0;
    bool show_heart = false;
    int heart_offset = 0;
    bool anim_done = false;

    /* 阶段划分 (总共 ~65帧, ~4秒) */
    /* Phase 1 (0-8): 饭盆从底部升起 */
    if (f < 9) {
        show_bowl = true;
        food_h = BOWL_FOOD_MAX;
    }
    /* Phase 2 (9-38): 猫低头进食, 粮渐减 */
    else if (f < 39) {
        cat_shift = 4;
        show_bowl = true;
        int eat_progress = f - 9;  /* 0-29 */
        food_h = BOWL_FOOD_MAX - (eat_progress * BOWL_FOOD_MAX) / 30;
        if (food_h < 0) food_h = 0;
    }
    /* Phase 3 (39-47): 猫抬头, 眯眼 */
    else if (f < 48) {
        cat_shift = 0;
        show_squint = true;
    }
    /* Phase 4 (48-56): 打嗝气泡 */
    else if (f < 57) {
        cat_shift = 0;
        show_squint = true;
        show_burp = true;
        burp_rise = f - 48;
    }
    /* Phase 5 (57-68): 爱心浮现 */
    else if (f < 69) {
        cat_shift = 0;
        show_heart = true;
        heart_offset = f - 57;
    }
    /* Phase 6: 动画结束 */
    else {
        anim_done = true;
    }

    if (anim_done) {
        s_pet_anim = PET_ANIM_NONE;
        s_pet_anim_frame = 0;
        snprintf(s_pet_msg, sizeof(s_pet_msg), "喂食完毕 +饱食25");
        return;
    }

    /* 清除精灵区 */
    lcd_fill_rect(PET_SPRITE_X, PET_SPRITE_Y, PET_SPRITE_W, PET_SPRITE_H, white);

    /* 画猫GIF帧 (下移后可能截断底部) */
    if (s_pet_gif_loaded && s_pet_gif_frame_count > 0) {
        int gif_idx = s_pet_gif_frame_idx % s_pet_gif_frame_count;
        PetGifFrame_t *fr = &s_pet_gif_frames[gif_idx];
        int draw_h = PET_SPRITE_H - cat_shift;
        if (draw_h > 0) {
            for (int y = 0; y < draw_h; y++) {
                int src_y = y;  /* GIF帧从头开始（看起来猫下移了） */
                for (int x = 0; x < fr->w && x < PET_SPRITE_W; x++) {
                    size_t bi = ((size_t)src_y * fr->w + x) / 8;
                    int bit = 7 - (x & 7);
                    if ((fr->bits[bi] >> bit) & 1) {
                        RlcdPort.RLCD_SetPixel(fr->x + x, fr->y + cat_shift + y, black);
                    }
                }
            }
        }
    }

    /* 眯眼 */
    if (show_squint) draw_cat_squint();

    /* 饭盆 */
    if (show_bowl) {
        draw_bowl_food(BOWL_X, BOWL_Y, BOWL_W, BOWL_H, food_h, black);
    }

    /* 打嗝气泡 */
    if (show_burp) {
        int by = BURP_Y - burp_rise * 3;
        draw_burp_bubble(BURP_X, by, 5, black);
        lcd_fill_ellipse(BURP_X, by, 4, 4, white); /* 空心 */
        /* 小文字 "o" */
        RlcdPort.RLCD_SetPixel(BURP_X, by, black);
    }

    /* 爱心 */
    if (show_heart) {
        int hx = HEART_CX;
        int hy = HEART_CY - heart_offset;
        /* 心跳缩放效果 */
        int enlarge = (heart_offset % 3 == 0) ? 1 : 0;
        if (enlarge) {
            draw_heart(hx - 2, hy - 2, black);
            draw_heart(hx + 2, hy + 2, black);
        }
        draw_heart(hx, hy, black);
    }

    RlcdPort.RLCD_Display();
}

// 前向声明
static void play_sd_pcm_task_fn(void *arg);

static void show_phrase_group_sel(void) {
    reset_screen();
    lv_obj_t *ti = lv_label_create(s_scr);
    lv_label_set_text(ti, "选择短句组");
    lv_obj_set_style_text_font(ti, &font_cn_24, 0);
    lv_obj_set_style_text_color(ti, lv_color_black(), 0);
    lv_obj_align(ti, LV_ALIGN_TOP_MID, 0, 10);
    int yp[] = {-35, -10, 15, 40};
    for (int g = 0; g < PHRASE_GROUPS; g++) {
        int start = s_ph_range[g][0], cnt = s_ph_range[g][1];
        int learned = 0;
        for (int i = start; i < start + cnt; i++)
            if (s_ph_learned[i] > 0) learned++;
        char b[56];
        snprintf(b, sizeof(b), "组%d %d/%d 学%d次", g+1, learned, cnt, s_ph_group_accessed[g]);
        lv_obj_t *lb = lv_label_create(s_scr);
        lv_label_set_text(lb, b);
        lv_obj_set_style_text_font(lb, &font_cn_16, 0);
        lv_obj_align(lb, LV_ALIGN_CENTER, 0, yp[g]);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        lv_obj_set_style_text_color(lb, (g==s_ph_group)?lv_color_white():lv_color_black(), 0);
        lv_obj_set_style_bg_color(lb, (g==s_ph_group)?lv_color_black():lv_color_white(), 0);
    }
    lv_label_set_text(s_status, "▲▼选择组  确认进入  左键返回");
}

static void show_phrase(void) {
    reset_screen();
    int start = s_ph_range[s_ph_group][0], cnt = s_ph_range[s_ph_group][1];
    int local = s_ph_idx - start;
    char is[16]; snprintf(is, sizeof(is), "%d/%d", local+1, cnt);
    lv_obj_t *il = lv_label_create(s_scr);
    lv_label_set_text(il, is);
    lv_obj_set_style_text_font(il, &font_cn_16, 0);
    lv_obj_set_style_text_color(il, lv_color_hex(0x888888), 0);
    lv_obj_align(il, LV_ALIGN_TOP_RIGHT, -10, 12);

    lv_obj_t *el = lv_label_create(s_scr);
    lv_label_set_text(el, s_ph_en[s_ph_idx]);
    lv_obj_set_style_text_font(el, &font_cn_24, 0);
    lv_obj_set_style_text_color(el, lv_color_black(), 0);
    lv_obj_align(el, LV_ALIGN_CENTER, 0, -35);

    lv_obj_t *cl = lv_label_create(s_scr);
    lv_label_set_text(cl, s_ph_show_cn ? s_ph_cn[s_ph_idx] : "");
    lv_obj_set_style_text_font(cl, &font_cn_16, 0);
    lv_obj_set_style_text_color(cl, lv_color_hex(0x666666), 0);
    lv_obj_align(cl, LV_ALIGN_CENTER, 0, 12);

    char cb[32]; snprintf(cb, sizeof(cb), "学%d次", s_ph_learned[s_ph_idx]);
    lv_obj_t *cbl = lv_label_create(s_scr);
    lv_label_set_text(cbl, cb);
    lv_obj_set_style_text_font(cbl, &font_cn_16, 0);
    lv_obj_set_style_text_color(cbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(cbl, LV_ALIGN_CENTER, 0, 35);

    lv_label_set_text(s_status, "↑ ↓换句  →释义  确认发音  左键回组");

    // Play audio: /sdcard/audio/phrase_NNN.pcm
    if (s_play_task) {
        s_stop_play = true; vTaskDelay(pdMS_TO_TICKS(50));
        if (s_play_task) { vTaskDelete(s_play_task); s_play_task = NULL; }
        s_stop_play = false;
    }
    char *path = (char *)malloc(48);
    if (path) {
        snprintf(path, 48, "/sdcard/audio/phrase_%03d.pcm", s_ph_idx + 1);
        s_stop_play = false;
        xTaskCreate(play_sd_pcm_task_fn, "phplay", 4*1024, path, 1, &s_play_task);
    }
}

// 播放 SD 卡 PCM 文件的任务 (通用)
static void play_sd_pcm_task_fn(void *arg)
{
    const char *path = (const char *)arg;
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        if (Lvgl_lock(-1)) { lv_label_set_text(s_status, "找不到音频文件"); Lvgl_unlock(); }
        free((void*)path);
        vTaskDelete(NULL);
        return;
    }
    fseek(fp, 0, SEEK_END);
    uint32_t sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    codecport->CodecPort_SetInfo("es8311", 1, 24000, 2, 16);
    codecport->CodecPort_SetSpeakerVol(s_volume);

    uint8_t chunk[512];
    uint32_t pos = 0;
    while (pos < sz && !s_stop_play) {
        int c = (sz - pos > sizeof(chunk)) ? sizeof(chunk) : (sz - pos);
        c = fread(chunk, 1, c, fp);
        if (c <= 0) break;
        codecport->CodecPort_PlayWrite(chunk, c);
        pos += c;
    }
    fclose(fp);
    codecport->CodecPort_CloseSpeaker();
    s_play_task = NULL;
    free((void*)path);
    vTaskDelete(NULL);
}

static void play_word_audio(int idx)
{
    if (idx < 0 || idx >= WORDS_TOTAL) return;
    // 停止当前播放
    if (s_play_task) {
        s_stop_play = true;
        vTaskDelay(pdMS_TO_TICKS(50));
        if (s_play_task) { vTaskDelete(s_play_task); s_play_task = NULL; }
        s_stop_play = false;
    }
    // 文件名 = 单词名 + .pcm, 如 apple.pcm
    char *path = (char *)malloc(64);
    if (!path) return;
    snprintf(path, 64, "/sdcard/audio/%s.pcm", s_words_en[idx]);
    s_stop_play = false;
    xTaskCreate(play_sd_pcm_task_fn, "wordplay", 4*1024, path, 1, &s_play_task);
}

/* ===== 播放 PCM (优先 SD卡, 回退内嵌) ===== */
static void play_task_fn(void *arg)
{
    (void)arg;
    uint8_t *d = NULL;
    uint32_t sz = 0;
    bool from_sd = false;
    FILE *fp = NULL;

    // 先试 SD 卡: /sdcard/audio/canon.pcm, 再兼容旧路径
    if (s_sd_ok) {
        fp = fopen("/sdcard/audio/canon.pcm", "rb");
        if (!fp) fp = fopen("/sdcard/audio/test.pcm", "rb");
        if (!fp) fp = fopen("/sdcard/canon.pcm", "rb");
    }
    if (fp) {
        fseek(fp, 0, SEEK_END);
        sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        from_sd = true;
        if (Lvgl_lock(-1)) { lv_label_set_text(s_status, "SD卡 PCM 播放中..."); Lvgl_unlock(); }
    } else {
        // 无内嵌音频, 跳过
        if (Lvgl_lock(-1)) { lv_label_set_text(s_status, "未找到音频文件"); Lvgl_unlock(); }
        codecport->CodecPort_CloseSpeaker();
        s_play_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    codecport->CodecPort_SetInfo("es8311", 1, 24000, 2, 16);
    codecport->CodecPort_SetSpeakerVol(s_volume);

    uint8_t chunk[512];
    size_t pos = 0;
    while (pos < sz && !s_stop_play) {
        int c = (sz - pos > (int)sizeof(chunk)) ? (int)sizeof(chunk) : (sz - pos);
        if (from_sd) {
            c = fread(chunk, 1, c, fp);
            if (c <= 0) break;
            codecport->CodecPort_PlayWrite(chunk, c);
        } else {
            codecport->CodecPort_PlayWrite(d + pos, c);
        }
        pos += c;
    }

    if (fp) fclose(fp);
    codecport->CodecPort_CloseSpeaker();
    s_play_task = NULL;
    s_stop_play = false;
    if (Lvgl_lock(-1)) { lv_label_set_text(s_status, "播放完成"); Lvgl_unlock(); }
    vTaskDelete(NULL);
}

static void start_play(void)
{
    if (s_play_task) return; /* 已在播放 */
    s_stop_play = false;
    lv_label_set_text(s_status, "播放中... 按确认停止");
    xTaskCreate(play_task_fn, "play", 4*1024, NULL, 1, &s_play_task);
}

static void stop_play(void)
{
    if (s_play_task) {
        s_stop_play = true;
        vTaskDelay(pdMS_TO_TICKS(50)); /* 等任务关codec并退出 */
        if (s_play_task) {
            vTaskDelete(s_play_task);
            s_play_task = NULL;
        }
        s_stop_play = false;
    }
}

/* 播放1kHz测试音 */
static void play_tone(void)
{
    #define N 8000
    int16_t *t = (int16_t *)heap_caps_malloc(N*2*sizeof(int16_t), MALLOC_CAP_INTERNAL);
    if (!t) return;
    for (int i = 0; i < N; i++) {
        float v = sinf(2*M_PI*1000*i/16000.0f)*25000;
        t[i*2]=(int16_t)v; t[i*2+1]=(int16_t)v;
    }
    codecport->CodecPort_SetInfo("es8311",1,16000,2,16);
    codecport->CodecPort_SetSpeakerVol(100);
    codecport->CodecPort_PlayWrite(t, N*2*sizeof(int16_t));
    free(t);
}

/* ===== 数学竖式口算页面 ===== */
static const char* s_level_names[] = {"幼儿园","一年级","二年级","三年级"};
static ArithLevel_t s_math_level = ARITH_LEVEL_GRADE_1;
static bool s_math_in_progress = false;
static int s_math_last_numpad = -1;  // 上次摇杆位置去重
static int s_math_input_digits[6];
static int s_math_digit_count = 0;

/* 九宫格位置数字标签 */
static const char s_numpad_labels[9] = {'1','2','3','4','5','6','7','8','9'};

/* 显示九宫格（全 LVGL）*/
static void draw_numpad_lvgl(int highlight_digit)
{
    const int NX = 210, NY = 50;
    const int NW = 50, NH = 45, NG = 6;

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            int digit = idx + 1;
            int x = NX + col * (NW + NG);
            int y = NY + row * (NH + NG);

            lv_obj_t *cell = lv_obj_create(lv_scr_act());
            lv_obj_set_size(cell, NW, NH);
            lv_obj_set_pos(cell, x, y);
            lv_obj_set_style_border_width(cell, 2, 0);
            lv_obj_set_style_border_color(cell, lv_palette_main(LV_PALETTE_GREY), 0);
            lv_obj_set_style_radius(cell, 4, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);

            if (digit == highlight_digit) {
                lv_obj_set_style_bg_color(cell, lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
                lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
                lv_obj_set_style_shadow_width(cell, 8, 0);
                lv_obj_set_style_shadow_color(cell, lv_palette_main(LV_PALETTE_BLUE), 0);
                lv_obj_set_style_shadow_ofs_x(cell, 2, 0);
                lv_obj_set_style_shadow_ofs_y(cell, 3, 0);
            } else {
                lv_obj_set_style_bg_color(cell, lv_color_white(), 0);
                lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
                lv_obj_set_style_shadow_width(cell, 0, 0);
            }

            lv_obj_t *lb = lv_label_create(cell);
            lv_obj_set_style_text_font(lb, &font_cn_24, 0);
            char d[2] = {(char)('0' + digit), '\0'};
            lv_label_set_text(lb, d);
            lv_obj_center(lb);
            if (digit == highlight_digit)
                lv_obj_set_style_text_color(lb, lv_color_white(), 0);
            else
                lv_obj_set_style_text_color(lb, lv_palette_main(LV_PALETTE_GREY), 0);
        }
    }

    /* 底板提示 */
    lv_obj_t *z_lb = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(z_lb, &font_cn_16, 0);
    lv_label_set_text(z_lb, " SW1=0  SW3=退格  ");
    lv_obj_set_pos(z_lb, NX, NY + 3*(NH+NG) + 5);
}

/* 获取摇杆九宫格位置对应的数字 (1-9)，5 = 居中 */
static int math_get_selected_digit(void)
{
    int pos = Joystick_GetNumpadPos();
    if (pos < 0) return 5;  // 中心 = 5
    return pos + 1;  // 0->1, 1->2, ... 8->9
}

/* 显示数学 - 选等级 */
static void show_math_level_select(void)
{
    lv_obj_clean(lv_scr_act());
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(title, &font_cn_24, 0);
    lv_label_set_text(title, "竖式口算");
    lv_obj_set_pos(title, lv_disp_get_hor_res(lv_disp_get_default())/2 - 50, 10);

    for (int i = 0; i < ARITH_LEVEL_COUNT; i++) {
        lv_obj_t *lb = lv_label_create(lv_scr_act());
        lv_obj_set_style_text_font(lb, &font_cn_16, 0);
        char buf[32];
        if (i == (int)s_math_level)
            snprintf(buf, sizeof(buf), "> %s <", s_level_names[i]);
        else
            snprintf(buf, sizeof(buf), "  %s  ", s_level_names[i]);
        lv_label_set_text(lb, buf);
        lv_obj_set_pos(lb, 60, 60 + i * 35);
    }

    lv_obj_t *hint = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(hint, &font_cn_16, 0);
    lv_label_set_text(hint, "摇杆上下选  SW4确认  SW2返回");
    lv_obj_set_pos(hint, 30, 250);
}

/* 绘制右侧九宫格（直接RLCD） */
static void draw_numpad(int highlight_digit)
{
    const int OX = 210, OY = 50;
    const int W = 50, H = 45, GAP = 6;

    // 清空九宫格区域
    lcd_fill_rect(OX - 5, OY - 5, OX + 3*(W+GAP) + 5, OY + 3*(H+GAP) + 5, ColorWhite);

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            int digit = idx + 1;
            int x = OX + col * (W + GAP);
            int y = OY + row * (H + GAP);

            if (digit == highlight_digit) {
                lcd_fill_rect(x, y, x + W, y + H, ColorBlack);
                // 反色数字用白色
                // (数字通过 LVGL label 叠加，所以这里只画背景)
            } else {
                lcd_fill_rect(x, y, x + W, y + H, ColorWhite);
                lcd_line(x, y, x + W, y, ColorBlack);
                lcd_line(x, y, x, y + H, ColorBlack);
                lcd_line(x + W, y, x + W, y + H, ColorBlack);
                lcd_line(x, y + H, x + W, y + H, ColorBlack);
            }
        }
    }
    RlcdPort.RLCD_Display();
}

/* 显示数学 - 答题界面 */
static void show_math_question(void)
{
    lv_obj_clean(lv_scr_act());
    reset_screen();

    const ArithQuestion_t *q = Arith_GetCurrentQuestion();
    ArithState_t *st = Arith_GetState();
    if (!q) return;

    // 用lv_label显示竖式
    lv_obj_t *frame = lv_obj_create(lv_scr_act());
    lv_obj_set_size(frame, 190, 280);
    lv_obj_set_pos(frame, 5, 10);
    lv_obj_set_style_border_width(frame, 0, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);

    // 题目标题
    char qinfo[32];
    snprintf(qinfo, sizeof(qinfo), "第 %d/%d 题", st->question_index + 1, ARITH_Q_PER_SESSION);
    lv_obj_t *qinfo_lb = lv_label_create(frame);
    lv_obj_set_style_text_font(qinfo_lb, &font_cn_16, 0);
    lv_label_set_text(qinfo_lb, qinfo);
    lv_obj_align(qinfo_lb, LV_ALIGN_TOP_MID, 0, 5);

    if (st->answered) {
        // 显示答案提示
        char result[64];
        snprintf(result, sizeof(result), "%s! 答案: %d",
                 st->last_correct ? "正确" : "错误",
                 q->answer);
        lv_obj_t *res_lb = lv_label_create(frame);
        lv_obj_set_style_text_font(res_lb, &font_cn_16, 0);
        lv_obj_set_style_text_color(res_lb,
            st->last_correct ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED), 0);
        lv_label_set_text(res_lb, result);
        lv_obj_align(res_lb, LV_ALIGN_TOP_MID, 0, 30);

        // 继续按钮
        lv_obj_t *next_lb = lv_label_create(frame);
        lv_obj_set_style_text_font(next_lb, &font_cn_16, 0);
        lv_label_set_text(next_lb, "SW4 下一题  SW2 返回");
        lv_obj_align(next_lb, LV_ALIGN_TOP_MID, 0, 220);
    } else {
        char top[32], bot[32];
        snprintf(top, sizeof(top), "%d", q->a);
        if (q->op == '*') snprintf(bot, sizeof(bot), "x %d", q->b);
        else if (q->op == '/') snprintf(bot, sizeof(bot), "÷ %d", q->b);
        else snprintf(bot, sizeof(bot), "%c %d", q->op, q->b);

        // 竖式: 对齐右端
        lv_obj_t *a_lb = lv_label_create(frame);
        lv_obj_set_style_text_font(a_lb, &font_cn_24, 0);
        lv_label_set_text(a_lb, top);
        lv_obj_set_style_text_align(a_lb, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(a_lb, 170);
        lv_obj_align(a_lb, LV_ALIGN_TOP_MID, 0, 55);

        lv_obj_t *op_lb = lv_label_create(frame);
        lv_obj_set_style_text_font(op_lb, &font_cn_24, 0);
        lv_label_set_text(op_lb, bot);
        lv_obj_set_style_text_align(op_lb, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(op_lb, 170);
        lv_obj_align(op_lb, LV_ALIGN_TOP_MID, 0, 85);

        // 横线
        lv_obj_t *l_lb = lv_label_create(frame);
        lv_obj_set_style_text_font(l_lb, &font_cn_24, 0);
        lv_label_set_text(l_lb, "━━━");
        lv_obj_align(l_lb, LV_ALIGN_TOP_MID, 0, 108);

        // 已输入数字显示
        char input_buf[16];
        int ipos = 0;
        for (int i = 0; i < s_math_digit_count && i < 6; i++)
            ipos += snprintf(input_buf + ipos, sizeof(input_buf) - ipos, "%d", s_math_input_digits[i]);
        if (ipos == 0) { input_buf[0] = '_'; input_buf[1] = '\0'; }

        lv_obj_t *inp_lb = lv_label_create(frame);
        lv_obj_set_style_text_font(inp_lb, &font_cn_24, 0);
        lv_label_set_text(inp_lb, input_buf);
        lv_obj_set_style_text_align(inp_lb, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(inp_lb, 170);
        lv_obj_align(inp_lb, LV_ALIGN_TOP_MID, 0, 120);

        // 得分
        char score_buf[32];
        snprintf(score_buf, sizeof(score_buf), "得分: %d/%d", st->correct_count, st->question_index);
        lv_obj_t *sc_lb = lv_label_create(frame);
        lv_obj_set_style_text_font(sc_lb, &font_cn_16, 0);
        lv_label_set_text(sc_lb, score_buf);
        lv_obj_align(sc_lb, LV_ALIGN_TOP_MID, 0, 175);

        // 等级
        lv_obj_t *lv_lb = lv_label_create(frame);
        lv_obj_set_style_text_font(lv_lb, &font_cn_16, 0);
        lv_label_set_text(lv_lb, s_level_names[s_math_level]);
        lv_obj_align(lv_lb, LV_ALIGN_TOP_MID, 0, 220);

        // 提示
        lv_obj_t *h_lb = lv_label_create(frame);
        lv_obj_set_style_text_font(h_lb, &font_cn_16, 0);
        lv_label_set_text(h_lb, "摇杆选数 SW4确认");
        lv_obj_align(h_lb, LV_ALIGN_TOP_MID, 0, 245);
    }

    // --- 右侧：九宫格 ---
    int hl = (st->answered || st->session_done) ? -1 : math_get_selected_digit();
    draw_numpad_lvgl(hl);
}

/* 显示数学 - 完成总结 */
static void show_math_summary(void)
{
    lv_obj_clean(lv_scr_act());
    reset_screen();

    ArithState_t *st = Arith_GetState();

    lv_obj_t *tt = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(tt, &font_cn_24, 0);
    lv_label_set_text(tt, "本轮完成!");
    lv_obj_set_pos(tt, (400 - 100)/2, 30);

    char buf[64];
    snprintf(buf, sizeof(buf), "正确: %d / %d", st->correct_count, ARITH_Q_PER_SESSION);
    lv_obj_t *lb1 = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lb1, &font_cn_16, 0);
    lv_label_set_text(lb1, buf);
    lv_obj_set_pos(lb1, 100, 90);

    snprintf(buf, sizeof(buf), "总分: %d", st->score);
    lv_obj_t *lb2 = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lb2, &font_cn_16, 0);
    lv_label_set_text(lb2, buf);
    lv_obj_set_pos(lb2, 100, 130);

    const char *comment;
    if (st->correct_count == ARITH_Q_PER_SESSION) comment = "满分! 太棒了!";
    else if (st->correct_count >= ARITH_Q_PER_SESSION - 1) comment = "很好! 继续加油!";
    else if (st->correct_count >= ARITH_Q_PER_SESSION / 2) comment = "不错! 再练练!";
    else comment = "加油! 多练习哦!";

    lv_obj_t *lb3 = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lb3, &font_cn_16, 0);
    lv_obj_set_style_text_color(lb3, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_label_set_text(lb3, comment);
    lv_obj_set_pos(lb3, 100, 170);

    lv_obj_t *hint = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(hint, &font_cn_16, 0);
    lv_label_set_text(hint, "SW4 再来一组  SW2 返回菜单");
    lv_obj_set_pos(hint, 60, 230);

    s_math_in_progress = false;
}

/* 进入数学答题 */
static void math_start_session(void)
{
    Arith_Init(s_math_level);
    s_math_in_progress = true;
    s_math_digit_count = 0;
    s_math_last_numpad = -1;
    memset(s_math_input_digits, 0, sizeof(s_math_input_digits));
    show_math_question();
}

/* ===== 页面系统 ===== */
typedef enum { PAGE_MAIN, PAGE_WORD, PAGE_PHRASE, PAGE_MATH, PAGE_PET, PAGE_GAME, PAGE_SETTINGS,
    PAGE_SOUNDTEST, PAGE_PICTEST, PAGE_SNAKE, PAGE_TETRIS } Page_t;
static Page_t s_page = PAGE_MAIN;
static int s_sel = 0;

#define MAIN_ITEMS 6
static const char *s_main[MAIN_ITEMS] = {"单词学习", "短语学习", "数学练习", "宠物", "游戏", "设置"};

#define GAME_ITEMS 2
static const char *s_games[GAME_ITEMS] = {"贪吃蛇", "俄罗斯方块"};
static int s_g_sel = 0;

#define SET_ITEMS 2
static const char *s_set[SET_ITEMS] = {"音频测试 (canon.pcm)", "图片测试"};
static int s_set_sel = 0;

static void rebuild(void);
static void show_todo(const char *msg) { lv_label_set_text(s_status, msg); }

/* ===== 各页面 ===== */
static void show_main(void) {
    int yp[] = {50, 83, 116, 149, 182, 215};  // 左对齐, 间距33px
    reset_screen();
    lv_obj_t *ti = lv_label_create(s_scr);
    lv_label_set_text(ti, "智元掌机");
    lv_obj_set_style_text_font(ti, &font_cn_24, 0);
    lv_obj_set_style_text_color(ti, lv_color_black(), 0);
    lv_obj_align(ti, LV_ALIGN_TOP_LEFT, 15, 8);
    for (int i = 0; i < MAIN_ITEMS; i++) {
        lv_obj_t *lb = lv_label_create(s_scr);
        lv_label_set_text(lb, s_main[i]);
        lv_obj_set_style_text_font(lb, &font_cn_16, 0);
        lv_obj_align(lb, LV_ALIGN_TOP_LEFT, 20, yp[i]);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        lv_obj_set_style_text_color(lb, (i==s_sel)?lv_color_white():lv_color_black(), 0);
        lv_obj_set_style_bg_color(lb, (i==s_sel)?lv_color_black():lv_color_white(), 0);
    }

    /* 右侧鹰图 — 直接写RLCD */
    for (int y = 0; y < YING_H; y++) {
        for (int x = 0; x < YING_W; x++) {
            int bi = y * (YING_W / 8) + (x / 8);
            int bit = 7 - (x & 7);
            int pixel = (s_ying_bits[bi] >> bit) & 1;
            RlcdPort.RLCD_SetPixel(195 + x, 65 + y, pixel ? ColorBlack : ColorWhite);
        }
    }
    RlcdPort.RLCD_Display();

    lv_label_set_text(s_status, "▲▼选择  确认进入");
    s_page = PAGE_MAIN;
}

static void show_pet(void) {
    pet_tick();
    reset_screen();

    lv_obj_t *ti = lv_label_create(s_scr);
    lv_label_set_text(ti, "宠物");
    lv_obj_set_style_text_font(ti, &font_cn_16, 0);
    lv_obj_set_style_text_color(ti, lv_color_black(), 0);
    lv_obj_align(ti, LV_ALIGN_TOP_LEFT, 12, 8);

    char top[48];
    snprintf(top, sizeof(top), "Lv%d", s_pet_level);
    lv_obj_t *score = lv_label_create(s_scr);
    lv_label_set_text(score, top);
    lv_obj_set_style_text_font(score, &font_cn_16, 0);
    lv_obj_set_style_text_color(score, lv_color_black(), 0);
    lv_obj_align(score, LV_ALIGN_TOP_LEFT, 12, 34);

    char points[24];
    snprintf(points, sizeof(points), "积分:%d", s_pet_points);
    lv_obj_t *pt = lv_label_create(s_scr);
    lv_label_set_text(pt, points);
    lv_obj_set_style_text_font(pt, &font_cn_16, 0);
    lv_obj_set_style_text_color(pt, lv_color_black(), 0);
    lv_obj_align(pt, LV_ALIGN_TOP_LEFT, 12, 58);

    char buf[40];
    int y = 86;
    pet_make_value(buf, sizeof(buf), "饱食", s_pet_full);
    lv_obj_t *lb = lv_label_create(s_scr);
    lv_label_set_text(lb, buf);
    lv_obj_set_style_text_font(lb, &font_cn_16, 0);
    lv_obj_set_style_text_color(lb, lv_color_black(), 0);
    lv_obj_align(lb, LV_ALIGN_TOP_LEFT, 12, y);

    y += 20;
    pet_make_value(buf, sizeof(buf), "心情", s_pet_happy);
    lb = lv_label_create(s_scr);
    lv_label_set_text(lb, buf);
    lv_obj_set_style_text_font(lb, &font_cn_16, 0);
    lv_obj_set_style_text_color(lb, lv_color_black(), 0);
    lv_obj_align(lb, LV_ALIGN_TOP_LEFT, 12, y);

    y += 20;
    pet_make_value(buf, sizeof(buf), "清洁", s_pet_clean);
    lb = lv_label_create(s_scr);
    lv_label_set_text(lb, buf);
    lv_obj_set_style_text_font(lb, &font_cn_16, 0);
    lv_obj_set_style_text_color(lb, lv_color_black(), 0);
    lv_obj_align(lb, LV_ALIGN_TOP_LEFT, 12, y);

    y += 20;
    pet_make_value(buf, sizeof(buf), "体力", s_pet_energy);
    lb = lv_label_create(s_scr);
    lv_label_set_text(lb, buf);
    lv_obj_set_style_text_font(lb, &font_cn_16, 0);
    lv_obj_set_style_text_color(lb, lv_color_black(), 0);
    lv_obj_align(lb, LV_ALIGN_TOP_LEFT, 12, y);

    lv_obj_t *msg = lv_label_create(s_scr);
    lv_label_set_text(msg, s_pet_msg);
    lv_obj_set_style_text_font(msg, &font_cn_16, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0x666666), 0);
    lv_obj_set_width(msg, 106);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(msg, LV_ALIGN_TOP_LEFT, 6, 270);

    lv_label_set_text(s_status, "↑↓/→选动作  确认执行  SW2返回");
}

static void show_game(void) {
    int yp[] = {-42,-17,8,33};
    reset_screen();
    lv_obj_t *ti = lv_label_create(s_scr);
    lv_label_set_text(ti, "游戏");
    lv_obj_set_style_text_font(ti, &font_cn_24, 0);
    lv_obj_set_style_text_color(ti, lv_color_black(), 0);
    lv_obj_align(ti, LV_ALIGN_TOP_MID, 0, 10);
    for (int i = 0; i < GAME_ITEMS; i++) {
        lv_obj_t *lb = lv_label_create(s_scr);
        lv_label_set_text(lb, s_games[i]);
        lv_obj_set_style_text_font(lb, &font_cn_16, 0);
        lv_obj_align(lb, LV_ALIGN_CENTER, 0, yp[i]);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        lv_obj_set_style_text_color(lb, (i==s_g_sel)?lv_color_white():lv_color_black(), 0);
        lv_obj_set_style_bg_color(lb, (i==s_g_sel)?lv_color_black():lv_color_white(), 0);
    }
    lv_label_set_text(s_status, "▲▼选择  确认进入  左键返回");
}

static void show_settings(void) {
    int yp[] = {-20, 5};
    reset_screen();
    lv_obj_t *ti = lv_label_create(s_scr);
    lv_label_set_text(ti, "设置");
    lv_obj_set_style_text_font(ti, &font_cn_24, 0);
    lv_obj_set_style_text_color(ti, lv_color_black(), 0);
    lv_obj_align(ti, LV_ALIGN_TOP_MID, 0, 10);
    for (int i = 0; i < SET_ITEMS; i++) {
        lv_obj_t *lb = lv_label_create(s_scr);
        lv_label_set_text(lb, s_set[i]);
        lv_obj_set_style_text_font(lb, &font_cn_16, 0);
        lv_obj_align(lb, LV_ALIGN_CENTER, 0, yp[i]);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        lv_obj_set_style_text_color(lb, (i==s_set_sel)?lv_color_white():lv_color_black(), 0);
        lv_obj_set_style_bg_color(lb, (i==s_set_sel)?lv_color_black():lv_color_white(), 0);
    }
    lv_label_set_text(s_status, "▲▼选择  确认进入  左键返回");
}

static void show_soundtest(void) {
    reset_screen();
    lv_obj_t *ti = lv_label_create(s_scr);
    lv_label_set_text(ti, "音频测试");
    lv_obj_set_style_text_font(ti, &font_cn_24, 0);
    lv_obj_set_style_text_color(ti, lv_color_black(), 0);
    lv_obj_align(ti, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_t *info = lv_label_create(s_scr);
    lv_label_set_text(info, "按确认播放 canon.pcm\n↑ ↓ 音量  长按返回");
    lv_obj_set_style_text_color(info, lv_color_black(), 0);
    lv_obj_set_style_text_font(info, &font_cn_16, 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -15);
    /* 音量指示 */
    char vbuf[32];
    snprintf(vbuf, sizeof(vbuf), "音量: %d", s_volume);
    s_vol_label = lv_label_create(s_scr);
    lv_label_set_text(s_vol_label, vbuf);
    lv_obj_set_style_text_color(s_vol_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_vol_label, &font_cn_16, 0);
    lv_obj_align(s_vol_label, LV_ALIGN_CENTER, 0, 20);
    lv_label_set_text(s_status, "确认播放/停止  ↑↓音量 长按返回");
}

static void show_pictest(void) {
    reset_screen();
    lv_obj_t *ti = lv_label_create(s_scr);
    lv_label_set_text(ti, "图片测试");
    lv_obj_set_style_text_font(ti, &font_cn_24, 0);
    lv_obj_set_style_text_color(ti, lv_color_black(), 0);
    lv_obj_align(ti, LV_ALIGN_TOP_MID, 0, 15);

    if (s_sd_ok && s_photo_max > 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "照片 1/%d (共 %d 张)\n↑ ↓ 翻页  长按返回", s_photo_max, s_photo_max);
        lv_obj_t *info = lv_label_create(s_scr);
        lv_label_set_text(info, buf);
        lv_obj_set_style_text_color(info, lv_color_black(), 0);
        lv_obj_set_style_text_font(info, &font_cn_16, 0);
        lv_obj_align(info, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(s_status, "↑ ↓ 翻页  长按返回");
    } else {
        lv_obj_t *info = lv_label_create(s_scr);
        lv_label_set_text(info, !s_sd_ok ? "SD卡未挂载" : "未找到照片文件\\n请将 full_NNN.sketch\\n放入 /sdcard/sketches/");
        lv_obj_set_style_text_color(info, lv_color_black(), 0);
        lv_obj_align(info, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(s_status, "长按返回");
    }

    // 如果已挂载且有照片，立即显示第一张
    if (s_sd_ok && s_photo_max > 0) {
        s_photo_idx = 0;
        show_photo(0);
    }
}

/* ===== 单词组选择页面 ===== */
static void show_group_sel(void) {
    reset_screen();
    lv_obj_t *ti = lv_label_create(s_scr);
    lv_label_set_text(ti, "选择单词组");
    lv_obj_set_style_text_font(ti, &font_cn_24, 0);
    lv_obj_set_style_text_color(ti, lv_color_black(), 0);
    lv_obj_align(ti, LV_ALIGN_TOP_MID, 0, 10);

    int yp[] = {-35, -10, 15, 40};
    for (int g = 0; g < WORD_GROUPS; g++) {
        int start = s_group_range[g][0];
        int cnt = s_group_range[g][1];
        int learned = 0;
        for (int i = start; i < start + cnt; i++)
            if (s_word_learned[i] > 0) learned++;

        char buf[56];
        snprintf(buf, sizeof(buf), "组%d %d/%d 学%d次", g+1, learned, cnt, s_group_accessed[g]);
        lv_obj_t *lb = lv_label_create(s_scr);
        lv_label_set_text(lb, buf);
        lv_obj_set_style_text_font(lb, &font_cn_16, 0);
        lv_obj_align(lb, LV_ALIGN_CENTER, 0, yp[g]);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        lv_obj_set_style_text_color(lb, (g==s_word_group)?lv_color_white():lv_color_black(), 0);
        lv_obj_set_style_bg_color(lb, (g==s_word_group)?lv_color_black():lv_color_white(), 0);
    }
    lv_label_set_text(s_status, "▲▼选择组  确认进入  左键返回");
}

/* ===== 单词学习页面 ===== */
static void show_word(void) {
    reset_screen();
    int start = s_group_range[s_word_group][0];
    int cnt = s_group_range[s_word_group][1];
    int local_idx = s_word_idx - start;

    /* 顶部: 单词编号 如 "3/15" */
    char idx_str[16];
    snprintf(idx_str, sizeof(idx_str), "%d/%d", local_idx + 1, cnt);
    lv_obj_t *idx_lb = lv_label_create(s_scr);
    lv_label_set_text(idx_lb, idx_str);
    lv_obj_set_style_text_font(idx_lb, &font_cn_16, 0);
    lv_obj_set_style_text_color(idx_lb, lv_color_hex(0x888888), 0);
    lv_obj_align(idx_lb, LV_ALIGN_TOP_RIGHT, -10, 12);

    /* 英文单词 (大字, 中央偏上) */
    lv_obj_t *en_lb = lv_label_create(s_scr);
    lv_label_set_text(en_lb, s_words_en[s_word_idx]);
    lv_obj_set_style_text_font(en_lb, &font_cn_24, 0);
    lv_obj_set_style_text_color(en_lb, lv_color_black(), 0);
    lv_obj_align(en_lb, LV_ALIGN_CENTER, 0, -35);

    /* 音标 */
    lv_obj_t *ph_lb = lv_label_create(s_scr);
    lv_label_set_text(ph_lb, s_words_phonetic[s_word_idx]);
    lv_obj_set_style_text_font(ph_lb, &font_cn_16, 0);
    lv_obj_set_style_text_color(ph_lb, lv_color_hex(0x888888), 0);
    lv_obj_align(ph_lb, LV_ALIGN_CENTER, 0, -12);

    /* 中文释义 (→ 切换) */
    lv_obj_t *cn_lb = lv_label_create(s_scr);
    lv_label_set_text(cn_lb, s_show_cn ? s_words_cn[s_word_idx] : "");
    lv_obj_set_style_text_font(cn_lb, &font_cn_16, 0);
    lv_obj_set_style_text_color(cn_lb, lv_color_hex(0x666666), 0);
    lv_obj_align(cn_lb, LV_ALIGN_CENTER, 0, 12);

    /* 学习计数 (灰色小字, 在中文下方) */
    char cnt_buf[32];
    snprintf(cnt_buf, sizeof(cnt_buf), "学%d次", s_word_learned[s_word_idx]);
    lv_obj_t *cnt_lb = lv_label_create(s_scr);
    lv_label_set_text(cnt_lb, cnt_buf);
    lv_obj_set_style_text_font(cnt_lb, &font_cn_16, 0);
    lv_obj_set_style_text_color(cnt_lb, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(cnt_lb, LV_ALIGN_CENTER, 0, 35);

    lv_label_set_text(s_status, "↑ ↓换词  →释义  确认发音  左键回组");

    play_word_audio(s_word_idx);
}

static void rebuild(void) {
    switch (s_page) {
        case PAGE_MAIN: show_main(); break;
        case PAGE_WORD:
            if (s_in_group_sel) show_group_sel();
            else show_word();
            break;
        case PAGE_PHRASE:
            if (s_ph_in_group_sel) show_phrase_group_sel();
            else show_phrase();
            break;
        case PAGE_MATH:
            if (!s_math_in_progress)
                show_math_level_select();
            else
                show_math_question();
            break;
        case PAGE_PET: show_pet(); break;
        case PAGE_GAME: show_game(); break;
        case PAGE_SETTINGS: show_settings(); break;
        case PAGE_SOUNDTEST: show_soundtest(); break;
        case PAGE_PICTEST: show_pictest(); break;
        default: show_main(); break;
    }
}

/* ===== 主入口 ===== */
extern "C" void app_main(void)
{
    UserApp_AppInit();
    Joystick_Init();
    RlcdPort.RLCD_Init();
    Lvgl_PortInit(400,300,Lvgl_FlushCallback);

    /* 挂载 SD 卡 */
    mount_sd();
    s_fs_data = (uint8_t *)heap_caps_malloc(PHOTO_FILE_SIZE, MALLOC_CAP_INTERNAL);
    if (s_fs_data) {
        s_photo_max = scan_photos();
        ESP_LOGI("PIC", "Found %d photos on SD", s_photo_max);
    } else {
        ESP_LOGE("PIC", "No mem for photo buffer!");
    }

    /* WiFi 连接 */
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wicfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wicfg);
    wifi_config_t wcfg = {};
    strcpy((char*)wcfg.sta.ssid, "tcvdog");
    strcpy((char*)wcfg.sta.password, "gucheng1215");
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    esp_wifi_start();
    esp_wifi_connect();
    ESP_LOGI("WIFI", "Connecting to tcvdog...");

    if (Lvgl_lock(-1)) {
        s_scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_scr, lv_color_white(), 0);

        /* 底部状态栏 */
        s_status = lv_label_create(s_scr);
        lv_label_set_text(s_status, "▲▼选择  确认进入");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0x888888), 0);
        lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -10);

        show_main();
        lv_scr_load(s_scr);
        Lvgl_unlock();
    }
    UserApp_TaskInit();

    /* 主循环 */
    bool pressed = false;
    while (1) {
        JoystickAction_t joy = Joystick_GetAction();

        /* 4按钮映射: SW1=上, SW2=返回, SW3=下, SW4=确认 */
        ButtonState_t btns = Buttons_GetState();
        if (btns.sw1) joy = JOY_ACTION_UP;
        if (btns.sw3) joy = JOY_ACTION_DOWN;
        if (btns.sw4) joy = JOY_ACTION_PRESS;
        if (btns.sw2) joy = JOY_ACTION_LEFT;  // 返回=左键
        bool redraw = false;
        if (pet_tick() && s_page == PAGE_PET) redraw = true;

        if (s_page == PAGE_MAIN) {
            if (joy == JOY_ACTION_UP) { s_sel = (s_sel-1+MAIN_ITEMS)%MAIN_ITEMS; redraw=true; }
            else if (joy == JOY_ACTION_DOWN) { s_sel = (s_sel+1)%MAIN_ITEMS; redraw=true; }
            else if (joy == JOY_ACTION_PRESS && !pressed) {
                pressed = true;
                if (s_sel == 0) { s_page = PAGE_WORD; s_word_group = 0; s_in_group_sel = true; s_word_idx = 0; s_show_cn = false; redraw=true; }
                else if (s_sel == 1) { s_page = PAGE_PHRASE; s_ph_group = 0; s_ph_in_group_sel = true; s_ph_idx = 0; s_ph_show_cn = false; redraw=true; }
                else if (s_sel == 2) { s_page = PAGE_MATH; s_math_in_progress = false; redraw=true; }
                else if (s_sel == 3) { s_page = PAGE_PET; s_pet_sel = 0; redraw=true; }
                else if (s_sel == 4) { s_page = PAGE_GAME; s_g_sel = 0; redraw=true; }
                else if (s_sel == 5) { s_page = PAGE_SETTINGS; s_set_sel = 0; redraw=true; }
            }
        }
        else if (s_page == PAGE_PET) {
            /* 动画播放中忽略操作 */
            bool anim_active = (s_pet_anim != PET_ANIM_NONE);
            /* SW2(GP18)直接检查, 不经过摇杆映射 */
            if (btns.sw2 && !anim_active) { s_page = PAGE_MAIN; redraw=true; }
            else if (anim_active) { /* 动画期间不接受其他操作 */ }
            else if (joy == JOY_ACTION_LEFT || joy == JOY_ACTION_UP) {
                s_pet_sel = (s_pet_sel - 1 + PET_ACTIONS) % PET_ACTIONS; redraw=true;
            } else if (joy == JOY_ACTION_DOWN || joy == JOY_ACTION_RIGHT) {
                s_pet_sel = (s_pet_sel + 1) % PET_ACTIONS; redraw=true;
            } else if (joy == JOY_ACTION_PRESS && !pressed) {
                pressed = true;
                pet_do_action();
                redraw = true;
            }
        }
        else if (s_page == PAGE_GAME) {
            if (joy == JOY_ACTION_LEFT) { s_page = PAGE_MAIN; redraw=true; }
            else if (joy == JOY_ACTION_UP) { s_g_sel = (s_g_sel-1+GAME_ITEMS)%GAME_ITEMS; redraw=true; }
            else if (joy == JOY_ACTION_DOWN) { s_g_sel = (s_g_sel+1)%GAME_ITEMS; redraw=true; }
            else if (joy == JOY_ACTION_PRESS && !pressed) {
                pressed = true;
                if (s_g_sel == 0) { show_todo("贪吃蛇 (TODO)"); }
                else if (s_g_sel == 1) { show_todo("俄罗斯方块 (TODO)"); }
            }
        }
        else if (s_page == PAGE_SETTINGS) {
            if (joy == JOY_ACTION_LEFT) { s_page = PAGE_MAIN; redraw=true; }
            else if (joy == JOY_ACTION_UP) { s_set_sel = (s_set_sel-1+SET_ITEMS)%SET_ITEMS; redraw=true; }
            else if (joy == JOY_ACTION_DOWN) { s_set_sel = (s_set_sel+1)%SET_ITEMS; redraw=true; }
            else if (joy == JOY_ACTION_PRESS && !pressed) {
                pressed = true;
                if (s_set_sel == 0) {
                    s_page = PAGE_SOUNDTEST;
                    if (Lvgl_lock(-1)) { show_soundtest(); Lvgl_unlock(); }
                    start_play();
                } else if (s_set_sel == 1) { s_page = PAGE_PICTEST; redraw=true; }
            }
        }
        else if (s_page == PAGE_WORD) {
            if (s_in_group_sel) {
                /* ---- 组选择模式 ---- */
                if (joy == JOY_ACTION_LEFT || joy == JOY_ACTION_LONG_PRESS) {
                    s_page = PAGE_MAIN; redraw=true;
                } else if (joy == JOY_ACTION_UP && s_word_group > 0) {
                    s_word_group--; redraw=true;
                } else if (joy == JOY_ACTION_DOWN && s_word_group < WORD_GROUPS - 1) {
                    s_word_group++; redraw=true;
                } else if (joy == JOY_ACTION_PRESS && !pressed) {
                    pressed = true;
                    s_in_group_sel = false;
                    s_word_idx = s_group_range[s_word_group][0];
                    s_show_cn = false;
                    s_group_accessed[s_word_group]++;  // 组访问计数+1
                    if (Lvgl_lock(-1)) { show_word(); Lvgl_unlock(); }
                }
            } else {
                /* ---- 单词学习模式 ---- */
                int start = s_group_range[s_word_group][0];
                int cnt = s_group_range[s_word_group][1];
                if (joy == JOY_ACTION_LEFT || joy == JOY_ACTION_LONG_PRESS) {
                    s_in_group_sel = true; redraw=true;
                } else if (joy == JOY_ACTION_PRESS && !pressed) {
                    pressed = true;
                    s_word_learned[s_word_idx]++;  // 学习计数+1
                    pet_award_points(1);
                    play_word_audio(s_word_idx);
                } else if (joy == JOY_ACTION_RIGHT) {
                    s_show_cn = !s_show_cn;
                    if (Lvgl_lock(-1)) { show_word(); Lvgl_unlock(); }
                } else if (joy == JOY_ACTION_UP && s_word_idx > start) {
                    s_word_idx--;
                    if (Lvgl_lock(-1)) { show_word(); Lvgl_unlock(); }
                } else if (joy == JOY_ACTION_DOWN && s_word_idx < start + cnt - 1) {
                    s_word_idx++;
                    if (Lvgl_lock(-1)) { show_word(); Lvgl_unlock(); }
                }
            }
        }
        else if (s_page == PAGE_PHRASE) {
            if (s_ph_in_group_sel) {
                if (joy == JOY_ACTION_LEFT || joy == JOY_ACTION_LONG_PRESS) {
                    s_page = PAGE_MAIN; redraw=true;
                } else if (joy == JOY_ACTION_UP && s_ph_group > 0) {
                    s_ph_group--; redraw=true;
                } else if (joy == JOY_ACTION_DOWN && s_ph_group < PHRASE_GROUPS - 1) {
                    s_ph_group++; redraw=true;
                } else if (joy == JOY_ACTION_PRESS && !pressed) {
                    pressed = true;
                    s_ph_in_group_sel = false;
                    s_ph_idx = s_ph_range[s_ph_group][0];
                    s_ph_show_cn = false;
                    s_ph_group_accessed[s_ph_group]++;
                    if (Lvgl_lock(-1)) { show_phrase(); Lvgl_unlock(); }
                }
            } else {
                int start = s_ph_range[s_ph_group][0];
                int cnt = s_ph_range[s_ph_group][1];
                if (joy == JOY_ACTION_LEFT || joy == JOY_ACTION_LONG_PRESS) {
                    s_ph_in_group_sel = true; redraw=true;
                } else if (joy == JOY_ACTION_PRESS && !pressed) {
                    pressed = true;
                    s_ph_learned[s_ph_idx]++;
                    pet_award_points(2);
                    s_stop_play = false;
                    // Re-trigger audio
                    char *path = (char *)malloc(48);
                    if (path) {
                        snprintf(path, 48, "/sdcard/audio/phrase_%03d.pcm", s_ph_idx + 1);
                        xTaskCreate(play_sd_pcm_task_fn, "phplay", 4*1024, path, 1, &s_play_task);
                    }
                } else if (joy == JOY_ACTION_RIGHT) {
                    s_ph_show_cn = !s_ph_show_cn;
                    if (Lvgl_lock(-1)) { show_phrase(); Lvgl_unlock(); }
                } else if (joy == JOY_ACTION_UP && s_ph_idx > start) {
                    s_ph_idx--;
                    if (Lvgl_lock(-1)) { show_phrase(); Lvgl_unlock(); }
                } else if (joy == JOY_ACTION_DOWN && s_ph_idx < start + cnt - 1) {
                    s_ph_idx++;
                    if (Lvgl_lock(-1)) { show_phrase(); Lvgl_unlock(); }
                }
            }
        }
        else if (s_page == PAGE_SOUNDTEST) {
            if (joy == JOY_ACTION_LONG_PRESS || joy == JOY_ACTION_LEFT) {
                stop_play(); s_page = PAGE_SETTINGS; redraw=true;
            } else if (joy == JOY_ACTION_PRESS && !pressed) {
                pressed = true;
                if (s_play_task) stop_play();
                else start_play();
            } else if (joy == JOY_ACTION_UP && s_volume < 100) {
                s_volume += 5; if (s_volume > 100) s_volume = 100;
                codecport->CodecPort_SetSpeakerVol(s_volume);
                if (s_vol_label && Lvgl_lock(-1)) {
                    char _v[16]; snprintf(_v,16,"音量: %d",s_volume);
                    lv_label_set_text(s_vol_label, _v); Lvgl_unlock();
                }
            } else if (joy == JOY_ACTION_DOWN && s_volume > 0) {
                s_volume -= 5; if (s_volume < 0) s_volume = 0;
                codecport->CodecPort_SetSpeakerVol(s_volume);
                if (s_vol_label && Lvgl_lock(-1)) {
                    char _v[16]; snprintf(_v,16,"音量: %d",s_volume);
                    lv_label_set_text(s_vol_label, _v); Lvgl_unlock();
                }
            }
        }
        else if (s_page == PAGE_PICTEST) {
            if (joy == JOY_ACTION_LONG_PRESS || joy == JOY_ACTION_LEFT) {
                s_photo_mode = false;
                s_page = PAGE_SETTINGS; redraw=true;
            } else if (joy == JOY_ACTION_UP && s_photo_idx < s_photo_max - 1) {
                s_photo_idx++;
                show_photo(s_photo_idx);
            } else if (joy == JOY_ACTION_DOWN && s_photo_idx > 0) {
                s_photo_idx--;
                show_photo(s_photo_idx);
            }
        }
        else if (s_page == PAGE_MATH) {
            if (!s_math_in_progress) {
                /* ---- 等级选择 ---- */
                if (btns.sw2) { s_page = PAGE_MAIN; redraw=true; }
                else if (joy == JOY_ACTION_UP && s_math_level > 0) {
                    s_math_level = (ArithLevel_t)((int)s_math_level - 1);
                    if (Lvgl_lock(-1)) { show_math_level_select(); Lvgl_unlock(); }
                } else if (joy == JOY_ACTION_DOWN && s_math_level < ARITH_LEVEL_COUNT - 1) {
                    s_math_level = (ArithLevel_t)((int)s_math_level + 1);
                    if (Lvgl_lock(-1)) { show_math_level_select(); Lvgl_unlock(); }
                } else if (joy == JOY_ACTION_PRESS && !pressed) {
                    pressed = true;
                    math_start_session();
                }
            } else {
                /* ---- 答题模式 ---- */
                ArithState_t *st = Arith_GetState();
                if (!st) continue;

                /* SW2 = 退出 */
                if (btns.sw2) {
                    if (st->answered || st->session_done) {
                        s_math_in_progress = false;
                        s_page = PAGE_MAIN; redraw=true;
                    } else {
                        s_math_in_progress = false;
                        s_page = PAGE_MAIN; redraw=true;
                    }
                } else if (st->session_done) {
                    /* 总结页 */
                    if (joy == JOY_ACTION_PRESS && !pressed) {
                        pressed = true;
                        math_start_session();  // 再来一组
                    } else if (btns.sw2) {
                        s_math_in_progress = false;
                        s_page = PAGE_MAIN; redraw=true;
                    }
                } else if (st->answered) {
                    /* 已作答 - 显示结果 */
                    if (joy == JOY_ACTION_PRESS && !pressed) {
                        pressed = true;
                        if (!Arith_Next()) {
                            // 本轮结束
                            if (Lvgl_lock(-1)) { show_math_summary(); Lvgl_unlock(); }
                        } else {
                            s_math_digit_count = 0;
                            memset(s_math_input_digits, 0, sizeof(s_math_input_digits));
                            if (Lvgl_lock(-1)) { show_math_question(); Lvgl_unlock(); }
                        }
                    }
                } else {
                    /* 未作答 - 输入模式 */
                    if (btns.sw3 && s_math_digit_count > 0) {
                        /* SW3 = 退格 */
                        s_math_digit_count--;
                        s_math_input_digits[s_math_digit_count] = 0;
                        if (Lvgl_lock(-1)) { show_math_question(); Lvgl_unlock(); }
                    } else if (btns.sw1 && s_math_digit_count < 6) {
                        /* SW1 = 输入0 */
                        s_math_input_digits[s_math_digit_count++] = 0;
                        if (Lvgl_lock(-1)) { show_math_question(); Lvgl_unlock(); }
                    } else if (joy == JOY_ACTION_PRESS && !pressed) {
                        pressed = true;
                        int pos = Joystick_GetNumpadPos();
                        if (pos < 0 && s_math_digit_count > 0) {
                            /* 摇杆居中 + SW4 = 提交答案 */
                            int input_val = 0;
                            for (int i = 0; i < s_math_digit_count; i++)
                                input_val = input_val * 10 + s_math_input_digits[i];
                            const ArithQuestion_t *q = Arith_GetCurrentQuestion();
                            if (q) {
                                st->answered = true;
                                st->last_correct = (input_val == q->answer);
                                if (st->last_correct) {
                                    st->correct_count++;
                                    st->streak++;
                                    st->score += 10 + (st->streak > 1 ? (st->streak-1)*2 : 0);
                                } else {
                                    st->streak = 0;
                                }
                                if (Lvgl_lock(-1)) { show_math_question(); Lvgl_unlock(); }
                            }
                        } else if (s_math_digit_count < 6) {
                            /* 摇杆有方向 + SW4 = 输入该数字 */
                            int digit = pos < 0 ? 5 : pos + 1;
                            s_math_input_digits[s_math_digit_count++] = digit;
                            if (Lvgl_lock(-1)) { show_math_question(); Lvgl_unlock(); }
                        }
                    }
                }
            }
        }
        else {
            if (joy == JOY_ACTION_LONG_PRESS || joy == JOY_ACTION_LEFT) {
                s_page = PAGE_MAIN; redraw=true;
            }
        }
        if (joy == JOY_ACTION_NONE) pressed = false;

        if (redraw && Lvgl_lock(-1)) { rebuild(); Lvgl_unlock(); }
        if (Lvgl_lock(-1)) { lv_timer_handler(); Lvgl_unlock(); }

        /* 推进动画帧 */
        if (s_page == PAGE_PET && s_pet_anim != PET_ANIM_NONE) {
            int64_t now = esp_timer_get_time();
            if (now >= s_pet_anim_next_us) {
                s_pet_anim_frame++;
                s_pet_anim_next_us = now + ANIM_FRAME_US;
            }
        }

        if (s_page == PAGE_PET) {
            draw_pet_gif();
            draw_pet_icon_bar();
        }

        /* 主菜单重绘鹰图（LVGL刷新后会覆盖RLCD直接写入）*/
        if (s_page == PAGE_MAIN) {
            for (int y = 0; y < YING_H; y++) {
                for (int x = 0; x < YING_W; x++) {
                    int bi = y * (YING_W / 8) + (x / 8);
                    int bit = 7 - (x & 7);
                    int pixel = (s_ying_bits[bi] >> bit) & 1;
                    RlcdPort.RLCD_SetPixel(195 + x, 65 + y, pixel ? ColorBlack : ColorWhite);
                }
            }
            RlcdPort.RLCD_Display();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
