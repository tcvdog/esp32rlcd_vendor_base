
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>

#include "display_bsp.h"
#include "lvgl_bsp.h"
#include "user_app.h"
#include "user_config.h"
#include "codec_bsp.h"
#include "joystick_bsp.h"
#include "phrase_data.h"
#include <lvgl.h>

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

// 前向声明
static void play_sd_pcm_task_fn(void *arg);

static void show_phrase_group_sel(void) {
    lv_obj_clean(s_scr);
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
    lv_obj_clean(s_scr);
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

    // 先试 SD 卡: /sdcard/audio/test.pcm
    if (s_sd_ok) {
        fp = fopen("/sdcard/audio/test.pcm", "rb");
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

/* ===== 页面系统 ===== */
typedef enum { PAGE_MAIN, PAGE_WORD, PAGE_PHRASE, PAGE_MATH, PAGE_GAME, PAGE_SETTINGS,
    PAGE_SOUNDTEST, PAGE_PICTEST, PAGE_SNAKE, PAGE_TETRIS } Page_t;
static Page_t s_page = PAGE_MAIN;
static int s_sel = 0;

#define MAIN_ITEMS 5
static const char *s_main[MAIN_ITEMS] = {"单词学习", "短语学习", "数学练习", "游戏", "设置"};

#define GAME_ITEMS 4
static const char *s_games[GAME_ITEMS] = {"音频测试", "图片测试", "贪吃蛇", "俄罗斯方块"};
static int s_g_sel = 0;

static void rebuild(void);
static void show_todo(const char *msg) { lv_label_set_text(s_status, msg); }

/* ===== 各页面 ===== */
static void show_main(void) {
    int yp[] = {-55,-25,5,35,65};
    lv_obj_clean(s_scr);
    lv_obj_t *ti = lv_label_create(s_scr);
    lv_label_set_text(ti, "智元掌机");
    lv_obj_set_style_text_font(ti, &font_cn_24, 0);
    lv_obj_set_style_text_color(ti, lv_color_black(), 0);
    lv_obj_align(ti, LV_ALIGN_TOP_MID, 0, 10);
    for (int i = 0; i < MAIN_ITEMS; i++) {
        lv_obj_t *lb = lv_label_create(s_scr);
        lv_label_set_text(lb, s_main[i]);
        lv_obj_set_style_text_font(lb, &font_cn_16, 0);
        lv_obj_align(lb, LV_ALIGN_CENTER, 0, yp[i]);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        lv_obj_set_style_text_color(lb, (i==s_sel)?lv_color_white():lv_color_black(), 0);
        lv_obj_set_style_bg_color(lb, (i==s_sel)?lv_color_black():lv_color_white(), 0);
    }
    lv_label_set_text(s_status, "▲▼选择  确认进入");
    s_page = PAGE_MAIN;
}

static void show_game(void) {
    int yp[] = {-42,-17,8,33};
    lv_obj_clean(s_scr);
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

static void show_soundtest(void) {
    lv_obj_clean(s_scr);
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
    lv_obj_clean(s_scr);
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
    lv_obj_clean(s_scr);
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
    lv_obj_clean(s_scr);
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
        case PAGE_GAME: show_game(); break;
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
        bool redraw = false;

        if (s_page == PAGE_MAIN) {
            if (joy == JOY_ACTION_UP) { s_sel = (s_sel-1+MAIN_ITEMS)%MAIN_ITEMS; redraw=true; }
            else if (joy == JOY_ACTION_DOWN) { s_sel = (s_sel+1)%MAIN_ITEMS; redraw=true; }
            else if (joy == JOY_ACTION_PRESS && !pressed) {
                pressed = true;
                if (s_sel == 0) { s_page = PAGE_WORD; s_word_group = 0; s_in_group_sel = true; s_word_idx = 0; s_show_cn = false; redraw=true; }
                else if (s_sel == 1) { s_page = PAGE_PHRASE; s_ph_group = 0; s_ph_in_group_sel = true; s_ph_idx = 0; s_ph_show_cn = false; redraw=true; }
                else if (s_sel == 3) { s_page = PAGE_GAME; s_g_sel = 0; redraw=true; }
                else if (s_sel == 2) { show_todo("数学练习 (TODO)"); }
                else if (s_sel == 4) { show_todo("设置 (TODO)"); }
            }
        }
        else if (s_page == PAGE_GAME) {
            if (joy == JOY_ACTION_LEFT) { s_page = PAGE_MAIN; redraw=true; }
            else if (joy == JOY_ACTION_UP) { s_g_sel = (s_g_sel-1+GAME_ITEMS)%GAME_ITEMS; redraw=true; }
            else if (joy == JOY_ACTION_DOWN) { s_g_sel = (s_g_sel+1)%GAME_ITEMS; redraw=true; }
            else if (joy == JOY_ACTION_PRESS && !pressed) {
                pressed = true;
                if (s_g_sel == 0) { /* 音频测试 */
                    s_page = PAGE_SOUNDTEST;
                    if (Lvgl_lock(-1)) { show_soundtest(); Lvgl_unlock(); }
                    start_play();
                } else if (s_g_sel == 1) { s_page = PAGE_PICTEST; redraw=true; }
                else if (s_g_sel == 2) { show_todo("贪吃蛇 (TODO)"); }
                else if (s_g_sel == 3) { show_todo("俄罗斯方块 (TODO)"); }
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
                stop_play(); s_page = PAGE_GAME; redraw=true;
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
                s_page = PAGE_GAME; redraw=true;
            } else if (joy == JOY_ACTION_UP && s_photo_idx < s_photo_max - 1) {
                s_photo_idx++;
                show_photo(s_photo_idx);
            } else if (joy == JOY_ACTION_DOWN && s_photo_idx > 0) {
                s_photo_idx--;
                show_photo(s_photo_idx);
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
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
