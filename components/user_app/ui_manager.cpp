/*
 * ui_manager.cpp — 单词掌机 UI 管理层
 * 主菜单 + 单词学习卡片 + 短语学习卡片 + 贪吃蛇 + 俄罗斯方块
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <esp_timer.h>
#include "lvgl.h"
#include "ui_manager.h"
#include "joystick_bsp.h"
#include "tts_player.h"
#include "vocab_loader.h"
#include "sketch_player.h"
#include "sd_audio_player.h"
#include "audio_bsp.h"
#include "arithmetic.h"
#include "snake_game.h"

/* RlcdPort 定义在 lvgl_demo.cpp，用于直接写屏 */
#include "display_bsp.h"
extern DisplayPort RlcdPort;

/* 全屏照片: 直接写入 RLCD，不设 LVGL canvas（DRAM 不足） */
static lv_obj_t *s_photo_canvas = NULL;

LV_FONT_DECLARE(font_cn_16);
LV_FONT_DECLARE(font_cn_24);

static lv_obj_t* ui_create_tetris_game(void); // 前向声明

static const char *TAG_UI = "UI";

/* =================================================================
 * 页面管理 — 所有 screen 对象
 * ================================================================= */
static AppPage_t s_current_page = PAGE_MAIN_MENU;
static lv_obj_t *s_scr_main_menu   = NULL;
static lv_obj_t *s_scr_word_learn  = NULL;
static lv_obj_t *s_scr_phrase_learn = NULL;
static lv_obj_t *s_scr_game_menu   = NULL;
static lv_obj_t *s_scr_settings    = NULL;
static lv_obj_t *s_scr_tetris      = NULL;
static lv_obj_t *s_scr_socrates_method = NULL;
static lv_obj_t *s_scr_photo_view  = NULL;
static lv_obj_t *s_scr_arithmetic  = NULL;    /* 算术 — 选等级 */
static lv_obj_t *s_scr_arithmetic_game = NULL; /* 算术 — 答题 */
static lv_obj_t *s_scr_sound_test = NULL;    /* 声音测试 */
/* 照片幻灯片索引 */
static int s_photo_idx = 0;

void UI_SwitchPage(AppPage_t page)
{
    /* 切出单词/照片页面时清除素描叠加 */
    if (s_current_page == PAGE_WORD_LEARN ||
        s_current_page == PAGE_PHOTO_VIEW) {
        Sketch_ClearActive();
    }
    s_current_page = page;
    switch (page) {
        case PAGE_MAIN_MENU:   lv_scr_load(s_scr_main_menu);   break;
        case PAGE_WORD_LEARN:  lv_scr_load(s_scr_word_learn);  break;
        case PAGE_PHRASE_LEARN:lv_scr_load(s_scr_phrase_learn);break;
        case PAGE_GAME_MENU:   lv_scr_load(s_scr_game_menu);   break;
        case PAGE_SETTINGS:    lv_scr_load(s_scr_settings);     break;
        case PAGE_TETRIS_GAME:   lv_scr_load(s_scr_tetris);           break;
        case PAGE_SNAKE_GAME:    /* screen loaded in Snake_Start */   break;
        case PAGE_SOCRATES_METHOD:lv_scr_load(s_scr_socrates_method);break;
        case PAGE_PHOTO_VIEW:    lv_scr_load(s_scr_photo_view);     break;
        case PAGE_ARITHMETIC:    lv_scr_load(s_scr_arithmetic);     break;
        case PAGE_ARITHMETIC_GAME:lv_scr_load(s_scr_arithmetic_game);break;
        case PAGE_SOUND_TEST:    lv_scr_load(s_scr_sound_test);     break;
        default: break;
    }
}
AppPage_t UI_GetCurrentPage(void)  { return s_current_page; }

/* =================================================================
 * 主菜单 (左对齐 + 右侧图腾)
 * ================================================================= */
static int s_menu_sel = 0;
static lv_obj_t *s_menu_label[5] = {NULL};

static void ui_menu_update_highlight(void)
{
    for (int i = 0; i < 5; i++) {
        if (!s_menu_label[i]) continue;
        if (i == s_menu_sel) {
            lv_obj_set_style_text_color(s_menu_label[i], lv_color_white(), 0);
            lv_obj_set_style_bg_color(s_menu_label[i], lv_color_black(), 0);
        } else {
            lv_obj_set_style_text_color(s_menu_label[i], lv_color_black(), 0);
            lv_obj_set_style_bg_color(s_menu_label[i], lv_color_white(), 0);
        }
    }
}

/* =================================================================
 * 赫尔墨斯蛇杖 (Caduceus) - 在 ESP-IDF 下暂不启用
 * ================================================================= */
#if 0

/* LVGL INDEXED_1BIT 图片描述符: 调色板(8字节) + 赫尔墨斯蛇杖位图数据 */
static const uint8_t s_caduceus_img_data[8 + CADUCEUS_BMP_SIZE] = {
    /* 调色板: index 0 = 白色, index 1 = 黑色 (lv_color32_t, alpha=0xFF) */
    0xFF, 0xFF, 0xFF, 0xFF,  /* 白 (不透明) */
    0x00, 0x00, 0x00, 0xFF,  /* 黑 (不透明) */
    /* 位图数据 */
    CADUCEUS_BMP_CONTENT
};

static lv_img_dsc_t s_caduceus_img_dsc;

static void ui_create_coiled_dragon(lv_obj_t *parent)
{
    /* 初始化图片描述符 (C++ 不能用 designated initializer) */
    memset(&s_caduceus_img_dsc, 0, sizeof(s_caduceus_img_dsc));
    s_caduceus_img_dsc.header.cf = LV_IMG_CF_INDEXED_1BIT;
    s_caduceus_img_dsc.header.w = CADUCEUS_BMP_W;
    s_caduceus_img_dsc.header.h = CADUCEUS_BMP_H;
    s_caduceus_img_dsc.data_size = sizeof(s_caduceus_img_data);
    s_caduceus_img_dsc.data = s_caduceus_img_data;

    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, &s_caduceus_img_dsc);
    lv_img_set_pivot(img, 0, 0);   /* 从左上角缩放 */
    lv_img_set_zoom(img, 384);     /* 150% (256=100%) */
    lv_obj_set_pos(img, 130, 35);     /* 右移54px */
    lv_obj_move_background(img);   /* 放到目录后面做背景 */
}
#endif

static void ui_create_hermes_totem(lv_obj_t *parent)
{
    /* Hermes totem temporarily disabled in ESP-IDF */
    (void)parent;
}

static lv_obj_t* ui_create_main_menu(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    /* 标题 — 左上 */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "智元掌机");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_text_font(title, &font_cn_24, 0);
    lv_obj_set_pos(title, 10, 6);

    /* 菜单项 — 左对齐 */
    const char *items[] = {"情景学习", "短语学习", "算术练习", "小游戏", "设置"};
    int item_y[] = {46, 81, 116, 151, 186};
    for (int i = 0; i < 5; i++) {
        lv_obj_t *lb = lv_label_create(scr);
        lv_label_set_text(lb, items[i]);
        lv_obj_set_style_text_color(lb, lv_color_black(), 0);
        lv_obj_set_style_text_font(lb, &font_cn_16, 0);
        lv_obj_set_pos(lb, 10, item_y[i]);
        lv_obj_set_style_bg_color(lb, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        s_menu_label[i] = lb;
    }
    ui_menu_update_highlight();

    /* 底部提示 — 左下 */
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "▲▼ 选择  按下进入  长按返回");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint, &font_cn_16, 0);
    lv_obj_set_pos(hint, 10, 248);

    /* 右侧 Hermes 图腾 */
    ui_create_hermes_totem(scr);

    return scr;
}

int UI_Menu_GetSelection(void) { return s_menu_sel; }

/* =================================================================
 * 单词学习卡片
 * ================================================================= */
static int s_word_index = 0;
static int s_word_display_mode = 0; /* 0=英 1=中 2=例句 3=全 */
static lv_obj_t *s_word_label_top  = NULL; /* 状态栏: "3/50" */
static lv_obj_t *s_word_label_en   = NULL; /* 英文大号 */
static lv_obj_t *s_word_label_phon = NULL; /* 音标 */
static lv_obj_t *s_word_label_cn   = NULL; /* 中文 */
static lv_obj_t *s_word_label_ex   = NULL; /* 例句 */
static lv_obj_t *s_word_label_hint = NULL; /* 底部提示 */

/* 素描画 canvas (64x64, 右上角) */
static lv_obj_t *s_word_canvas      = NULL;

static void ui_word_update_display(void)
{
    const VocabDatabase_t *db = Vocab_GetDB();
    if (!db || s_word_index >= db->word_count) return;
    const VocabWord_t *w = &db->words[s_word_index];

    char buf[64];

    /* 状态栏: "12/50 · KET" */
    snprintf(buf, sizeof(buf), "%d/%d · %s",
        s_word_index + 1, db->word_count, w->level);
    lv_label_set_text(s_word_label_top, buf);

    /* 英文 */
    lv_label_set_text(s_word_label_en, w->word);

    /* 音标 (有就显示) */
    if (strlen(w->phonetic) > 0) {
        lv_label_set_text(s_word_label_phon, w->phonetic);
        lv_obj_clear_flag(s_word_label_phon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_word_label_phon, LV_OBJ_FLAG_HIDDEN);
    }

    /* 中文释义 (有就显示) */
    if (strlen(w->chinese) > 0) {
        lv_label_set_text(s_word_label_cn, w->chinese);
        lv_obj_clear_flag(s_word_label_cn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_word_label_cn, LV_OBJ_FLAG_HIDDEN);
    }

    /* 例句 */
    if (w->example_count > 0 && strlen(w->examples[0]) > 0) {
        lv_label_set_text(s_word_label_ex, w->examples[0]);
        lv_obj_clear_flag(s_word_label_ex, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_word_label_ex, LV_OBJ_FLAG_HIDDEN);
    }

    /* 加载全屏照片 (索引=word_index, 文件 full_NNN.sketch) */
    Sketch_Load(s_word_index, NULL);
}

static lv_obj_t* ui_create_word_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    /* 素描画: 在 RLCD 显示缓冲区直接绘制 (全屏照片) */
    s_word_canvas = NULL;

    /* 状态栏: "1/50 · KET" (左上, 透明) */
    s_word_label_top = lv_label_create(scr);
    lv_obj_set_style_text_color(s_word_label_top, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_word_label_top, &font_cn_16, 0);
    lv_obj_set_style_bg_opa(s_word_label_top, LV_OPA_TRANSP, 0);
    lv_obj_align(s_word_label_top, LV_ALIGN_TOP_LEFT, 6, 5);

    /* 英文单词 (右上, 大号白字有阴影效果——用黑色做视觉对比) */
    s_word_label_en = lv_label_create(scr);
    lv_label_set_text(s_word_label_en, "Hello");
    lv_obj_set_style_text_color(s_word_label_en, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_word_label_en, &font_cn_24, 0);
    lv_obj_set_style_bg_opa(s_word_label_en, LV_OPA_TRANSP, 0);
    lv_obj_align(s_word_label_en, LV_ALIGN_TOP_RIGHT, -10, 35);

    /* 音标 (单词下方, 透明) */
    s_word_label_phon = lv_label_create(scr);
    lv_label_set_text(s_word_label_phon, "");
    lv_obj_set_style_text_color(s_word_label_phon, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(s_word_label_phon, &font_cn_16, 0);
    lv_obj_set_style_bg_opa(s_word_label_phon, LV_OPA_TRANSP, 0);
    lv_obj_align(s_word_label_phon, LV_ALIGN_TOP_RIGHT, -10, 62);

    /* 中文释义 (音标下方, 透明) */
    s_word_label_cn = lv_label_create(scr);
    lv_label_set_text(s_word_label_cn, "");
    lv_obj_set_style_text_color(s_word_label_cn, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_word_label_cn, &font_cn_16, 0);
    lv_obj_set_style_bg_opa(s_word_label_cn, LV_OPA_TRANSP, 0);
    lv_obj_align(s_word_label_cn, LV_ALIGN_TOP_RIGHT, -10, 88);

    /* 例句 (底部, 透明, 自动换行) */
    s_word_label_ex = lv_label_create(scr);
    lv_label_set_text(s_word_label_ex, "");
    lv_obj_set_style_text_color(s_word_label_ex, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(s_word_label_ex, &font_cn_16, 0);
    lv_obj_set_style_bg_opa(s_word_label_ex, LV_OPA_TRANSP, 0);
    lv_obj_set_width(s_word_label_ex, 180);
    lv_label_set_long_mode(s_word_label_ex, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_word_label_ex, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_word_label_ex, LV_ALIGN_BOTTOM_LEFT, 8, -55);

    /* 底部提示 (透明) */
    s_word_label_hint = lv_label_create(scr);
    lv_label_set_text(s_word_label_hint,
        "< > 切换  按下发音  长按返回");
    lv_obj_set_style_text_color(s_word_label_hint, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(s_word_label_hint, &font_cn_16, 0);
    lv_obj_set_style_bg_opa(s_word_label_hint, LV_OPA_TRANSP, 0);
    lv_obj_align(s_word_label_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    return scr;
}

void UI_Word_Prev(void)
{
    VocabDatabase_t *db = Vocab_GetDB();
    if (!db) return;
    if (db->word_count == 0) return;
    s_word_index = (s_word_index - 1 + db->word_count) % db->word_count;
    ui_word_update_display();
    /* 切换时自动发音 */
    TtsPlayer_PlayWord(s_word_index);
}

void UI_Word_Next(void)
{
    VocabDatabase_t *db = Vocab_GetDB();
    if (!db) return;
    if (db->word_count == 0) return;
    s_word_index = (s_word_index + 1) % db->word_count;
    ui_word_update_display();
    TtsPlayer_PlayWord(s_word_index);
}

void UI_Word_CycleDisplayMode(void)
{
    s_word_display_mode = (s_word_display_mode + 1) % 4;
    ui_word_update_display();
}

int UI_Word_GetIndex(void) { return s_word_index; }
void UI_Word_SetIndex(int idx) { s_word_index = idx; ui_word_update_display(); }

/* =================================================================
 * 短语学习卡片
 * ================================================================= */
static int s_phrase_index = 0;
static lv_obj_t *s_phrase_top    = NULL;
static lv_obj_t *s_phrase_text   = NULL;
static lv_obj_t *s_phrase_cn     = NULL;
static lv_obj_t *s_phrase_scene  = NULL;
static lv_obj_t *s_phrase_hint   = NULL;

static void ui_phrase_update_display(void)
{
    VocabDatabase_t *db = Vocab_GetDB();
    if (!db || s_phrase_index >= db->phrase_count) return;
    const VocabPhrase_t *p = &db->phrases[s_phrase_index];

    char buf[64];
    snprintf(buf, sizeof(buf), "%d/%d",
        s_phrase_index + 1, db->phrase_count);
    lv_label_set_text(s_phrase_top, buf);

    lv_label_set_text(s_phrase_text, p->phrase);
    lv_label_set_text(s_phrase_cn, p->chinese);
    lv_label_set_text(s_phrase_scene,
        strlen(p->scene) > 0 ? p->scene : "");

    /* 加载对应全屏照片 (索引 0~99) */
    Sketch_Load(s_phrase_index, NULL);
}

static lv_obj_t* ui_create_phrase_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    /* 状态栏 (顶部, 半透明背景) */
    s_phrase_top = lv_label_create(scr);
    lv_obj_set_style_text_color(s_phrase_top, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_phrase_top, &font_cn_16, 0);
    lv_obj_set_style_bg_opa(s_phrase_top, LV_OPA_TRANSP, 0);
    lv_obj_align(s_phrase_top, LV_ALIGN_TOP_RIGHT, -8, 5);

    /* 短语文字 (底部居中, 半透明背景浮在照片上) */
    s_phrase_text = lv_label_create(scr);
    lv_label_set_text(s_phrase_text, "Hello");
    lv_obj_set_style_text_color(s_phrase_text, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_phrase_text, &font_cn_24, 0);
    lv_obj_set_style_bg_opa(s_phrase_text, LV_OPA_TRANSP, 0);
    lv_obj_align(s_phrase_text, LV_ALIGN_BOTTOM_MID, 0, -55);

    /* 中文释义 (短语下方) */
    s_phrase_cn = lv_label_create(scr);
    lv_label_set_text(s_phrase_cn, "");
    lv_obj_set_style_text_color(s_phrase_cn, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_phrase_cn, &font_cn_16, 0);
    lv_obj_set_style_bg_opa(s_phrase_cn, LV_OPA_TRANSP, 0);
    lv_obj_align(s_phrase_cn, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* 场景标签 */
    s_phrase_scene = lv_label_create(scr);
    lv_obj_set_style_text_color(s_phrase_scene, lv_color_hex(0x444444), 0);
    lv_obj_set_style_text_font(s_phrase_scene, &font_cn_16, 0);
    lv_obj_set_style_bg_opa(s_phrase_scene, LV_OPA_TRANSP, 0);
    lv_obj_align(s_phrase_scene, LV_ALIGN_TOP_LEFT, 8, 5);

    /* 底部提示 */
    s_phrase_hint = lv_label_create(scr);
    lv_label_set_text(s_phrase_hint,
        "< > 切换  按下发音  长按返回");
    lv_obj_set_style_text_color(s_phrase_hint, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(s_phrase_hint, &font_cn_16, 0);
    lv_obj_set_style_bg_opa(s_phrase_hint, LV_OPA_TRANSP, 0);
    lv_obj_align(s_phrase_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    return scr;
}

void UI_Phrase_Prev(void)
{
    VocabDatabase_t *db = Vocab_GetDB();
    if (!db || db->phrase_count == 0) return;
    s_phrase_index = (s_phrase_index - 1 + db->phrase_count) % db->phrase_count;
    ui_phrase_update_display();
    TtsPlayer_PlayPhrase(s_phrase_index);
}

void UI_Phrase_Next(void)
{
    VocabDatabase_t *db = Vocab_GetDB();
    if (!db || db->phrase_count == 0) return;
    s_phrase_index = (s_phrase_index + 1) % db->phrase_count;
    ui_phrase_update_display();
    TtsPlayer_PlayPhrase(s_phrase_index);
}

/* =================================================================
 * 游戏菜单
 * ================================================================= */
static int s_game_sel = 0;
static lv_obj_t *s_game_label[5] = {NULL};

static void ui_game_update_highlight(void)
{
    for (int i = 0; i < 5; i++) {
        if (!s_game_label[i]) continue;
        if (i == s_game_sel) {
            lv_obj_set_style_text_color(s_game_label[i], lv_color_white(), 0);
            lv_obj_set_style_bg_color(s_game_label[i], lv_color_black(), 0);
        } else {
            lv_obj_set_style_text_color(s_game_label[i], lv_color_black(), 0);
            lv_obj_set_style_bg_color(s_game_label[i], lv_color_white(), 0);
        }
    }
}

static lv_obj_t* ui_create_game_menu(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "小游戏");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_text_font(title, &font_cn_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    const char *items[] = {"贪吃蛇", "俄罗斯方块", "照片查看", "声音测试", "三角洲行动"};
    int y_pos[] = {-47, -24, -1, 22, 45};
    for (int i = 0; i < 5; i++) {
        lv_obj_t *lb = lv_label_create(scr);
        lv_label_set_text(lb, items[i]);
        lv_obj_set_style_text_color(lb, lv_color_black(), 0);
        lv_obj_set_style_text_font(lb, &font_cn_16, 0);
        lv_obj_align(lb, LV_ALIGN_CENTER, 0, y_pos[i]);
        lv_obj_set_style_bg_color(lb, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        s_game_label[i] = lb;
    }

    ui_game_update_highlight();

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "摇杆上下选择  按下进入  左键返回");
    lv_obj_set_style_text_color(hint, lv_color_black(), 0);
    lv_obj_set_style_text_font(hint, &font_cn_16, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    return scr;
}

/* =================================================================
 * 声音测试页面
 * ================================================================= */
static lv_obj_t* ui_create_sound_test(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "声音测试");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_text_font(title, &font_cn_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    lv_obj_t *info = lv_label_create(scr);
    lv_label_set_text(info, "播放1kHz测试音...\n长按返回");
    lv_obj_set_style_text_color(info, lv_color_black(), 0);
    lv_obj_set_style_text_font(info, &font_cn_16, 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 0);

    return scr;
}

/* =================================================================
 * 设置页面
 * ================================================================= */
static lv_obj_t *s_settings_label_vol = NULL;

static void ui_settings_update_vol(void)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "音量: %d (摇杆调节)", Audio_GetVolume());
    lv_label_set_text(s_settings_label_vol, buf);
}

static lv_obj_t* ui_create_settings_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "设置");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_text_font(title, &font_cn_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    s_settings_label_vol = lv_label_create(scr);
    lv_label_set_text(s_settings_label_vol, "音量: 70");
    lv_obj_set_style_text_color(s_settings_label_vol, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_settings_label_vol, &font_cn_16, 0);
    lv_obj_align(s_settings_label_vol, LV_ALIGN_TOP_LEFT, 15, 60);

    lv_obj_t *lb2 = lv_label_create(scr);
    lv_label_set_text(lb2, "新词/天: 5");
    lv_obj_set_style_text_color(lb2, lv_color_black(), 0);
    lv_obj_set_style_text_font(lb2, &font_cn_16, 0);
    lv_obj_align(lb2, LV_ALIGN_TOP_LEFT, 15, 90);

    lv_obj_t *lb3 = lv_label_create(scr);
    lv_label_set_text(lb3, "长按返回菜单");
    lv_obj_set_style_text_color(lb3, lv_color_black(), 0);
    lv_obj_set_style_text_font(lb3, &font_cn_16, 0);
    lv_obj_align(lb3, LV_ALIGN_TOP_LEFT, 15, 130);

    lv_obj_t *lb4 = lv_label_create(scr);
    lv_label_set_text(lb4, "版本: v1.0");
    lv_obj_set_style_text_color(lb4, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lb4, &font_cn_16, 0);
    lv_obj_align(lb4, LV_ALIGN_TOP_LEFT, 15, 170);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "长按返回");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x999999), 0);
    lv_obj_set_style_text_font(hint, &font_cn_16, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    return scr;
}

/* =================================================================
 * 照片查看页面 (全屏)
 * ================================================================= */
static lv_obj_t* ui_create_photo_view(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    /* 底部提示 */
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "< > 切换  按发音  长按返回");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x999999), 0);
    lv_obj_set_style_text_font(hint, &font_cn_16, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    ESP_LOGI(TAG_UI, "Photo view screen created");
    return scr;
}

/* =================================================================
 * 算术练习 — 等级选择页面
 * ================================================================= */
static int s_arith_level_sel = 0;
static lv_obj_t *s_arith_level_labels[ARITH_LEVEL_COUNT] = {NULL};

static void ui_arith_level_update(void)
{
    for (int i = 0; i < ARITH_LEVEL_COUNT; i++) {
        if (!s_arith_level_labels[i]) continue;
        if (i == s_arith_level_sel) {
            lv_obj_set_style_text_color(s_arith_level_labels[i], lv_color_white(), 0);
            lv_obj_set_style_bg_color(s_arith_level_labels[i], lv_color_black(), 0);
        } else {
            lv_obj_set_style_text_color(s_arith_level_labels[i], lv_color_black(), 0);
            lv_obj_set_style_bg_color(s_arith_level_labels[i], lv_color_white(), 0);
        }
    }
}

static lv_obj_t* ui_create_arith_level_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "选择难度");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_text_font(title, &font_cn_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    int y_pos[] = {-40, -15, 10, 35};
    for (int i = 0; i < ARITH_LEVEL_COUNT; i++) {
        lv_obj_t *lb = lv_label_create(scr);
        lv_label_set_text(lb, Arith_GetLevelName((ArithLevel_t)i));
        lv_obj_set_style_text_color(lb, lv_color_black(), 0);
        lv_obj_set_style_text_font(lb, &font_cn_16, 0);
        lv_obj_align(lb, LV_ALIGN_CENTER, 0, y_pos[i]);
        lv_obj_set_style_bg_color(lb, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        s_arith_level_labels[i] = lb;
    }
    ui_arith_level_update();

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "▲▼ 选择  按确认  长按返回");
    lv_obj_set_style_text_color(hint, lv_color_black(), 0);
    lv_obj_set_style_text_font(hint, &font_cn_16, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    return scr;
}

/* =================================================================
 * 算术练习 — 答题页面
 * ================================================================= */
/* 共享标签 */
static lv_obj_t *s_arith_game_top   = NULL; /* 顶栏: 等级+进度+分数 */
static lv_obj_t *s_arith_hint       = NULL; /* 底部提示 */
static lv_obj_t *s_arith_fb_label   = NULL; /* 正误反馈 (居中) */

/* ===== 竖式 (左侧) ===== */
static lv_obj_t *s_arith_eq_line1   = NULL; /* 第一行数字 */
static lv_obj_t *s_arith_eq_line2   = NULL; /* 运算符+第二行数字 */
static lv_obj_t *s_arith_eq_div     = NULL; /* 分隔线 ────── */
static lv_obj_t *s_arith_eq_result  = NULL; /* 结果或 ? */

/* ===== 九宫格 (右侧) ===== */
static lv_obj_t *s_arith_np_answer  = NULL; /* "答案: _ _ _" */
static lv_obj_t *s_arith_np_keys[ARITH_NP_ROWS][ARITH_NP_COLS] = {{NULL}}; /* 键位 */

/* ===== 比大小 ===== */
static lv_obj_t *s_arith_cmp_left   = NULL; /* 左边数字 */
static lv_obj_t *s_arith_cmp_right  = NULL; /* 右边数字 */

/* ===== 更新函数 ===== */

static void ui_arith_game_update(void)
{
    const ArithQuestion_t *q = Arith_GetCurrentQuestion();
    if (!q) {
        ESP_LOGW("ArithUI", "update: q=NULL idx=%d", Arith_GetState()->question_index);
        return;
    }

    ArithState_t *st = Arith_GetState();
    char buf[96];

    /* ---- 顶栏 ---- */
    snprintf(buf, sizeof(buf), "%s  第 %d/%d 题  %d分",
        Arith_GetLevelName(st->level),
        st->question_index + 1, ARITH_Q_PER_SESSION,
        Arith_GetScore());
    lv_label_set_text(s_arith_game_top, buf);

    /* ---- 隐藏所有题型专用元素 ---- */
    lv_obj_add_flag(s_arith_eq_line1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_arith_eq_line2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_arith_eq_div, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_arith_eq_result, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_arith_np_answer, LV_OBJ_FLAG_HIDDEN);
    for (int r = 0; r < ARITH_NP_ROWS; r++)
        for (int c = 0; c < ARITH_NP_COLS; c++)
            lv_obj_add_flag(s_arith_np_keys[r][c], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_arith_cmp_left, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_arith_cmp_right, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_arith_fb_label, LV_OBJ_FLAG_HIDDEN);

    if (q->type == ARITH_Q_COMPARE) {
        /* ===== 比大小 (左半屏幕, 大号) ===== */
        lv_obj_clear_flag(s_arith_cmp_left, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_arith_cmp_right, LV_OBJ_FLAG_HIDDEN);

        snprintf(buf, sizeof(buf), "%d", q->a);
        lv_label_set_text(s_arith_cmp_left, buf);
        snprintf(buf, sizeof(buf), "%d", q->b);
        lv_label_set_text(s_arith_cmp_right, buf);

        /* 高亮选中侧: 粗边框 */
        bool hl_left = (st->cursor == 0);
        lv_obj_set_style_border_width(s_arith_cmp_left, hl_left ? 4 : 1, 0);
        lv_obj_set_style_border_width(s_arith_cmp_right, !hl_left ? 4 : 1, 0);

        if (st->answered) {
            lv_obj_clear_flag(s_arith_fb_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_arith_fb_label, st->last_correct ? "✓ 对啦！" : "✗ 不对哦");
        }

        /* 提示 */
        if (st->answered && !st->session_done)
            lv_label_set_text(s_arith_hint, "按确认 下一题  长按返回");
        else if (st->answered && st->session_done)
            lv_label_set_text(s_arith_hint, "按确认 查看成绩  长按返回");
        else
            lv_label_set_text(s_arith_hint, "◄ 选左边  ► 选右边  按确认");

    } else {
        /* ===== 竖式 + 九宫格 (全屏布局) ===== */

        /* --- 左侧: 竖式公式 --- */
        lv_obj_clear_flag(s_arith_eq_line1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_arith_eq_line2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_arith_eq_div, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_arith_eq_result, LV_OBJ_FLAG_HIDDEN);

        /* 第一行: 右对齐显示 a */
        snprintf(buf, sizeof(buf), "  %d", q->a);
        lv_label_set_text(s_arith_eq_line1, buf);

        /* 第二行: 运算符 + b */
        snprintf(buf, sizeof(buf), "%s %d", Arith_GetOpDisplay(q->op), q->b);
        lv_label_set_text(s_arith_eq_line2, buf);

        /* 分隔线 */
        lv_label_set_text(s_arith_eq_div, "──────");

        /* 结果行 */
        if (st->answered) {
            snprintf(buf, sizeof(buf), "  %d", q->answer);
        } else {
            snprintf(buf, sizeof(buf), "  ?");
        }
        lv_label_set_text(s_arith_eq_result, buf);

        /* --- 右侧: 答案显示 + 九宫格 --- */
        lv_obj_clear_flag(s_arith_np_answer, LV_OBJ_FLAG_HIDDEN);
        for (int r = 0; r < ARITH_NP_ROWS; r++)
            for (int c = 0; c < ARITH_NP_COLS; c++)
                lv_obj_clear_flag(s_arith_np_keys[r][c], LV_OBJ_FLAG_HIDDEN);

        if (st->answered) {
            /* 已回答: 显示答案和反馈 */
            snprintf(buf, sizeof(buf), "答案: %d ✓", q->answer);
            lv_label_set_text(s_arith_np_answer, buf);

            /* 九宫格完全禁用样式 */
            for (int r = 0; r < ARITH_NP_ROWS; r++) {
                for (int c = 0; c < ARITH_NP_COLS; c++) {
                    lv_obj_set_style_bg_opa(s_arith_np_keys[r][c], LV_OPA_TRANSP, 0);
                    lv_obj_set_style_text_color(s_arith_np_keys[r][c], lv_color_hex(0xAAAAAA), 0);
                    lv_obj_set_style_border_width(s_arith_np_keys[r][c], 1, 0);
                }
            }

            lv_obj_clear_flag(s_arith_fb_label, LV_OBJ_FLAG_HIDDEN);
            if (st->last_correct) {
                snprintf(buf, sizeof(buf), "✓ 正确！");
                if (st->streak > 1)
                    snprintf(buf, sizeof(buf), "✓ 正确！连对%d题", st->streak);
                lv_label_set_text(s_arith_fb_label, buf);
            } else {
                lv_label_set_text(s_arith_fb_label, "✗ 不对哦");
            }

            if (st->session_done)
                lv_label_set_text(s_arith_hint, "按确认 查看成绩  长按返回");
            else
                lv_label_set_text(s_arith_hint, "按确认 下一题  长按返回");

        } else {
            /* 未作答: 显示输入状态 */
            const ArithNumPad_t *np = &st->numpad;

            /* 答案显示区: 单次 snprintf */
            char ans[32];
            ans[0] = '\0';
            for (int i = 0; i < ARITH_MAX_DIGITS; i++) {
                char c = (i < np->digit_count) ? ('0' + np->digits[i]) : '_';
                char seg[3] = {c, ' ', '\0'};
                strcat(ans, seg);
            }
            snprintf(buf, sizeof(buf), "答案: %s", ans);
            lv_label_set_text(s_arith_np_answer, buf);

            /* 九宫格渲染: 选中/未选中样式 */
            for (int r = 0; r < ARITH_NP_ROWS; r++) {
                for (int c = 0; c < ARITH_NP_COLS; c++) {
                    bool selected = (r == np->cursor_row && c == np->cursor_col);
                    lv_obj_set_style_bg_opa(s_arith_np_keys[r][c],
                        selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
                    lv_obj_set_style_bg_color(s_arith_np_keys[r][c],
                        lv_color_black(), 0);
                    lv_obj_set_style_text_color(s_arith_np_keys[r][c],
                        selected ? lv_color_white() : lv_color_black(), 0);
                    lv_obj_set_style_border_width(s_arith_np_keys[r][c],
                        selected ? 3 : 1, 0);
                }
            }

            lv_label_set_text(s_arith_hint, "▲▼◄► 选数字  按确认输入  长按返回");
        }
    }
}

/* ===== 创建答题页面 ===== */

static lv_obj_t* ui_create_arith_game_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    /* 顶栏 */
    s_arith_game_top = lv_label_create(scr);
    lv_label_set_text(s_arith_game_top, "幼儿园  第 1/10 题  0分");
    lv_obj_set_style_text_color(s_arith_game_top, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(s_arith_game_top, &font_cn_16, 0);
    lv_obj_align(s_arith_game_top, LV_ALIGN_TOP_LEFT, 8, 5);

    /* ===== 竖式 (左侧 0~200px) ===== */
    int eq_x = 55;  /* 竖式左偏移 */

    s_arith_eq_line1 = lv_label_create(scr);
    lv_label_set_text(s_arith_eq_line1, "  23");
    lv_obj_set_style_text_color(s_arith_eq_line1, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_arith_eq_line1, &font_cn_24, 0);
    lv_obj_align(s_arith_eq_line1, LV_ALIGN_LEFT_MID, eq_x, -30);

    s_arith_eq_line2 = lv_label_create(scr);
    lv_label_set_text(s_arith_eq_line2, "+ 15");
    lv_obj_set_style_text_color(s_arith_eq_line2, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_arith_eq_line2, &font_cn_24, 0);
    lv_obj_align(s_arith_eq_line2, LV_ALIGN_LEFT_MID, eq_x, 15);

    s_arith_eq_div = lv_label_create(scr);
    lv_label_set_text(s_arith_eq_div, "──────");
    lv_obj_set_style_text_color(s_arith_eq_div, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_arith_eq_div, &font_cn_16, 0);
    lv_obj_align(s_arith_eq_div, LV_ALIGN_LEFT_MID, eq_x, 55);

    s_arith_eq_result = lv_label_create(scr);
    lv_label_set_text(s_arith_eq_result, "  ?");
    lv_obj_set_style_text_color(s_arith_eq_result, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_arith_eq_result, &font_cn_24, 0);
    lv_obj_align(s_arith_eq_result, LV_ALIGN_LEFT_MID, eq_x, 85);

    /* ===== 比大小 (屏幕居中) ===== */
    s_arith_cmp_left = lv_label_create(scr);
    lv_label_set_text(s_arith_cmp_left, "15");
    lv_obj_set_style_text_color(s_arith_cmp_left, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_arith_cmp_left, &font_cn_24, 0);
    lv_obj_set_style_border_width(s_arith_cmp_left, 2, 0);
    lv_obj_set_style_border_color(s_arith_cmp_left, lv_color_black(), 0);
    lv_obj_set_style_pad_all(s_arith_cmp_left, 12, 0);
    lv_obj_set_style_radius(s_arith_cmp_left, 4, 0);
    lv_obj_align(s_arith_cmp_left, LV_ALIGN_CENTER, -80, 60);

    s_arith_cmp_right = lv_label_create(scr);
    lv_label_set_text(s_arith_cmp_right, "8");
    lv_obj_set_style_text_color(s_arith_cmp_right, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_arith_cmp_right, &font_cn_24, 0);
    lv_obj_set_style_border_width(s_arith_cmp_right, 2, 0);
    lv_obj_set_style_border_color(s_arith_cmp_right, lv_color_black(), 0);
    lv_obj_set_style_pad_all(s_arith_cmp_right, 12, 0);
    lv_obj_set_style_radius(s_arith_cmp_right, 4, 0);
    lv_obj_align(s_arith_cmp_right, LV_ALIGN_CENTER, 80, 60);

    /* ===== 九宫格 (右侧 200~480px) ===== */

    /* 答案显示 (九宫格上方) */
    s_arith_np_answer = lv_label_create(scr);
    lv_label_set_text(s_arith_np_answer, "答案: _ _ _");
    lv_obj_set_style_text_color(s_arith_np_answer, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_arith_np_answer, &font_cn_16, 0);
    lv_obj_align(s_arith_np_answer, LV_ALIGN_TOP_LEFT, 205, 28);

    /* 九宫格键位 (3列x4行) */
    int key_w = 58;
    int key_h = 38;
    int gap_x = 3;
    int gap_y = 3;
    int grid_x0 = 210;  /* 九宫格左上角 x */
    int grid_y0 = 58;   /* 九宫格左上角 y */

    for (int r = 0; r < ARITH_NP_ROWS; r++) {
        for (int c = 0; c < ARITH_NP_COLS; c++) {
            lv_obj_t *key = lv_label_create(scr);
            lv_label_set_text(key, Arith_NumpadGetKey(c, r));
            lv_obj_set_style_text_color(key, lv_color_black(), 0);
            lv_obj_set_style_text_font(key, &font_cn_24, 0);
            lv_obj_set_style_border_width(key, 1, 0);
            lv_obj_set_style_border_color(key, lv_color_black(), 0);
            lv_obj_set_style_radius(key, 3, 0);
            lv_obj_set_style_pad_top(key, 2, 0);
            lv_obj_set_style_pad_bottom(key, 2, 0);
            lv_obj_set_width(key, key_w);
            lv_obj_set_height(key, key_h);
            /* 文字居中 */
            lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(key, grid_x0 + c * (key_w + gap_x),
                          grid_y0 + r * (key_h + gap_y));
            s_arith_np_keys[r][c] = key;
        }
    }

    /* 反馈标签 (底部中央) */
    s_arith_fb_label = lv_label_create(scr);
    lv_label_set_text(s_arith_fb_label, "");
    lv_obj_set_style_text_color(s_arith_fb_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_arith_fb_label, &font_cn_16, 0);
    lv_obj_align(s_arith_fb_label, LV_ALIGN_CENTER, 0, 210);

    /* 底部提示 */
    s_arith_hint = lv_label_create(scr);
    lv_label_set_text(s_arith_hint, "▲▼◄► 选数字  按确认输入  长按返回");
    lv_obj_set_style_text_color(s_arith_hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_arith_hint, &font_cn_16, 0);
    lv_obj_align(s_arith_hint, LV_ALIGN_BOTTOM_MID, 0, -5);

    return scr;
}

/* =================================================================
 * 摇杆交互处理 (主路由)
 * ================================================================= */
bool UI_HandleJoystick(int action)
{
    VocabDatabase_t *db = Vocab_GetDB();

    /* ---- 主菜单 ---- */
    if (s_current_page == PAGE_MAIN_MENU) {
        if (action == JOY_ACTION_UP) {
            s_menu_sel = (s_menu_sel > 0) ? s_menu_sel - 1 : 4;
            ui_menu_update_highlight();
            return true;
        }
        if (action == JOY_ACTION_DOWN) {
            s_menu_sel = (s_menu_sel < 4) ? s_menu_sel + 1 : 0;
            ui_menu_update_highlight();
            return true;
        }
        if (action == JOY_ACTION_PRESS) {
            if (s_menu_sel == 0) {        /* 情景学习 (全屏照片) */
                UI_SwitchPage(PAGE_PHOTO_VIEW);
                /* 只清缓存，不调用RLCD_Display，让Sketch_Load一次性渲染 */
                RlcdPort.RLCD_ColorClear(0xFF);
                Sketch_SetPhotoMode(true);
                if (!SdAudio_IsAvailable()) {
                    ESP_LOGE(TAG_UI, "情景学习: SD卡不可用!");
                }
                SketchSource_t src = Sketch_Load(s_word_index, NULL);
                if (src != SKETCH_SOURCE_SD) {
                    ESP_LOGE(TAG_UI, "情景学习: 照片加载失败(src=%d)", src);
                } else {
                    ESP_LOGI(TAG_UI, "情景学习: 照片加载成功");
                }
                s_photo_idx = s_word_index;
                if (db && s_word_index < db->word_count)
                    TtsPlayer_PlayWord(s_word_index);
            } else if (s_menu_sel == 1) {  /* 短语学习 */
                UI_SwitchPage(PAGE_PHRASE_LEARN);
                ui_phrase_update_display();
                if (db && s_phrase_index < db->phrase_count)
                    TtsPlayer_PlayPhrase(s_phrase_index);
            } else if (s_menu_sel == 2) {  /* 算术练习 */
                Arith_Init(ARITH_LEVEL_KINDERGARTEN);
                UI_SwitchPage(PAGE_ARITHMETIC);
            } else if (s_menu_sel == 3) {  /* 小游戏 */
                UI_SwitchPage(PAGE_GAME_MENU);
            } else if (s_menu_sel == 4) {  /* 设置 */
                ui_settings_update_vol();
                UI_SwitchPage(PAGE_SETTINGS);
            }
            return true;
        }
        return false;
    }

    /* ---- 单词学习 (全屏照片+透明标签) ---- */
    if (s_current_page == PAGE_WORD_LEARN) {
        if (action == JOY_ACTION_LEFT || action == JOY_ACTION_UP) {
            if (s_word_index == 0) {
                UI_SwitchPage(PAGE_MAIN_MENU);  // 第一个词返回
            } else {
                UI_Word_Prev();
            }
            return true;
        }
        if (action == JOY_ACTION_RIGHT || action == JOY_ACTION_DOWN) {
            UI_Word_Next();
            return true;
        }
        if (action == JOY_ACTION_PRESS) {
            VocabDatabase_t *db = Vocab_GetDB();
            if (db && s_word_index < db->word_count)
                TtsPlayer_PlayWord(s_word_index);
            return true;
        }
        if (action == JOY_ACTION_LONG_PRESS) {
            UI_SwitchPage(PAGE_MAIN_MENU);
            return true;
        }
        return false;
    }

    /* ---- 短语学习 ---- */
    if (s_current_page == PAGE_PHRASE_LEARN) {
        if (action == JOY_ACTION_LEFT) {
            if (s_phrase_index == 0) {
                UI_SwitchPage(PAGE_MAIN_MENU);  // 第一个短语左键返回
            } else {
                UI_Phrase_Prev();
            }
            return true;
        }
        if (action == JOY_ACTION_UP) {
            UI_Phrase_Prev();
            return true;
        }
        if (action == JOY_ACTION_RIGHT || action == JOY_ACTION_DOWN) {
            UI_Phrase_Next();
            return true;
        }
        if (action == JOY_ACTION_PRESS) {
            VocabDatabase_t *db = Vocab_GetDB();
            if (db && s_phrase_index < db->phrase_count)
                TtsPlayer_PlayPhrase(s_phrase_index);
            return true;
        }
        if (action == JOY_ACTION_LONG_PRESS) {
            UI_SwitchPage(PAGE_MAIN_MENU);
            return true;
        }
        return false;
    }

    /* ---- 游戏菜单 ---- */
    if (s_current_page == PAGE_GAME_MENU) {
        if (action == JOY_ACTION_LEFT) {
            UI_SwitchPage(PAGE_MAIN_MENU);
            return true;
        }
        if (action == JOY_ACTION_UP) {
            s_game_sel = (s_game_sel > 0) ? s_game_sel - 1 : 4;
            ui_game_update_highlight();
            return true;
        }
        if (action == JOY_ACTION_DOWN) {
            s_game_sel = (s_game_sel < 4) ? s_game_sel + 1 : 0;
            ui_game_update_highlight();
            return true;
        }
        if (action == JOY_ACTION_LONG_PRESS) {
            UI_SwitchPage(PAGE_MAIN_MENU);
            return true;
        }
        if (action == JOY_ACTION_PRESS) {
            if (s_game_sel == 0) {        /* 贪吃蛇 */
                Snake_Start();
                s_current_page = PAGE_SNAKE_GAME;
            } else if (s_game_sel == 1) { /* 俄罗斯方块 */
                lv_scr_load(s_scr_tetris);
                s_current_page = PAGE_TETRIS_GAME;
                UI_Tetris_Start();
            } else if (s_game_sel == 2) { /* 照片查看 */
                UI_SwitchPage(PAGE_PHOTO_VIEW);
                RlcdPort.RLCD_ColorClear(0xFF);
                RlcdPort.RLCD_Display();
                Sketch_SetPhotoMode(true);
                s_photo_idx = 0;
                s_word_index = 0;
                Sketch_Load(0, NULL);
            } else if (s_game_sel == 3) { /* 声音测试 */
                UI_SwitchPage(PAGE_SOUND_TEST);
                /* 播放1kHz测试音 (0.1秒) */
                static int16_t tone_buf[1600 * 2];
                for (int i = 0; i < 1600; i++) {
                    float v = sinf(2 * M_PI * 1000 * i / 16000.0f) * 25000;
                    tone_buf[i*2] = (int16_t)v;
                    tone_buf[i*2+1] = (int16_t)v;
                }
                Audio_PlayPCM(tone_buf, 1600);
            } else if (s_game_sel == 4) { /* 三角洲行动 (TODO) */
                // stub
            }
            return true;
        }
        return false;
    }

    /* ---- 设置 ---- */
    if (s_current_page == PAGE_SETTINGS) {
        if (action == JOY_ACTION_LEFT) {
            UI_SwitchPage(PAGE_MAIN_MENU);
            return true;
        }
        if (action == JOY_ACTION_UP) {
            uint8_t vol = Audio_GetVolume();
            if (vol < 95) Audio_SetVolume(vol + 5);
            ui_settings_update_vol();
            return true;
        }
        if (action == JOY_ACTION_DOWN) {
            uint8_t vol = Audio_GetVolume();
            if (vol > 5) Audio_SetVolume(vol - 5);
            else Audio_SetVolume(0);
            ui_settings_update_vol();
            return true;
        }
        if (action == JOY_ACTION_LONG_PRESS) {
            UI_SwitchPage(PAGE_MAIN_MENU);
            return true;
        }
        return false;
    }

    /* ---- 俄罗斯方块 ---- */
    if (s_current_page == PAGE_TETRIS_GAME) {
        if (action == JOY_ACTION_LONG_PRESS) {
            UI_SwitchPage(PAGE_MAIN_MENU);  // 长按返回主菜单
            return true;
        }
        if (action == JOY_ACTION_UP) {
            UI_Tetris_Rotate();
            return true;
        }
        if (action == JOY_ACTION_DOWN) {
            UI_Tetris_SetDirection(0, 1);
            return true;
        }
        if (action == JOY_ACTION_LEFT) {
            UI_Tetris_SetDirection(-1, 0);
            return true;
        }
        if (action == JOY_ACTION_RIGHT) {
            UI_Tetris_SetDirection(1, 0);
            return true;
        }
        return false;
    }

    /* ---- 贪吃蛇 ---- */
    if (s_current_page == PAGE_SNAKE_GAME) {
        if (action == JOY_ACTION_LONG_PRESS) {
            UI_SwitchPage(PAGE_MAIN_MENU);
            Snake_Stop();
            return true;
        }
        if (action == JOY_ACTION_UP)    { Snake_SetDir(SNAKE_DIR_UP);    return true; }
        if (action == JOY_ACTION_DOWN)  { Snake_SetDir(SNAKE_DIR_DOWN);  return true; }
        if (action == JOY_ACTION_LEFT)  { Snake_SetDir(SNAKE_DIR_LEFT);  return true; }
        if (action == JOY_ACTION_RIGHT) { Snake_SetDir(SNAKE_DIR_RIGHT); return true; }
        if (action == JOY_ACTION_PRESS) {
            if (Snake_IsGameOver()) {
                Snake_Start();  /* 重新开始 */
            }
            return true;
        }
        return true;
    }

/* ---- 照片查看 (情景学习) ---- */
    if (s_current_page == PAGE_PHOTO_VIEW) {
        if (action == JOY_ACTION_LONG_PRESS) {
            Sketch_SetPhotoMode(false);
            UI_SwitchPage(PAGE_MAIN_MENU);
            return true;
        }
        if (action == JOY_ACTION_LEFT || action == JOY_ACTION_UP) {
            if (s_photo_idx > 0) {
                s_photo_idx--;
                s_word_index = s_photo_idx;
                Sketch_Load(s_photo_idx, NULL);
            }
            return true;
        }
        if (action == JOY_ACTION_RIGHT || action == JOY_ACTION_DOWN) {
            int max_idx = db ? db->word_count : 50;
            if (s_photo_idx < max_idx - 1) {
                s_photo_idx++;
                s_word_index = s_photo_idx;
                Sketch_Load(s_photo_idx, NULL);
            }
            return true;
        }
        if (action == JOY_ACTION_PRESS) {
            if (db && s_word_index < db->word_count)
                TtsPlayer_PlayWord(s_word_index);
            return true;
        }
        return true;
    }

    /* ---- 算术练习 — 选等级 ---- */
    if (s_current_page == PAGE_ARITHMETIC) {
        if (action == JOY_ACTION_UP) {
            s_arith_level_sel = (s_arith_level_sel > 0) ? s_arith_level_sel - 1 : ARITH_LEVEL_COUNT - 1;
            ui_arith_level_update();
            return true;
        }
        if (action == JOY_ACTION_DOWN) {
            s_arith_level_sel = (s_arith_level_sel < ARITH_LEVEL_COUNT - 1) ? s_arith_level_sel + 1 : 0;
            ui_arith_level_update();
            return true;
        }
        if (action == JOY_ACTION_PRESS) {
            Arith_Init((ArithLevel_t)s_arith_level_sel);
            UI_SwitchPage(PAGE_ARITHMETIC_GAME);
            ui_arith_game_update();
            return true;
        }
        if (action == JOY_ACTION_LONG_PRESS) {
            UI_SwitchPage(PAGE_MAIN_MENU);
            return true;
        }
        return true;
    }

    /* ---- 算术练习 — 答题 ---- */
    if (s_current_page == PAGE_ARITHMETIC_GAME) {
        if (action == JOY_ACTION_LONG_PRESS) {
            ESP_LOGI("ArithUI", "long press -> menu");
            UI_SwitchPage(PAGE_MAIN_MENU);
            return true;
        }

        ArithState_t *st = Arith_GetState();
        const ArithQuestion_t *q = Arith_GetCurrentQuestion();

        if (!q) {
            ESP_LOGW("ArithUI", "joy: q=NULL idx=%d", st ? st->question_index : -1);
            return true;
        }

        if (!st->answered) {
            /* 未作答 */
            if (q->type == ARITH_Q_COMPARE) {
                /* ---- 比大小 ---- */
                if (action == JOY_ACTION_LEFT || action == JOY_ACTION_RIGHT) {
                    Arith_CursorMove(0);  /* 切换左右 */
                    ui_arith_game_update();
                    return true;
                }
            } else {
                /* ---- 九宫格 ---- */
                int dx = 0, dy = 0;
                if (action == JOY_ACTION_UP)       dy = -1;
                else if (action == JOY_ACTION_DOWN)  dy = 1;
                else if (action == JOY_ACTION_LEFT)  dx = -1;
                else if (action == JOY_ACTION_RIGHT) dx = 1;

                if (dx != 0 || dy != 0) {
                    Arith_NumpadMove(dx, dy);
                    ui_arith_game_update();
                    return true;
                }
            }

            /* 按确认: 比大小 或 九宫格 */
            if (action == JOY_ACTION_PRESS) {
                if (q && q->type == ARITH_Q_COMPARE) {
                    Arith_Answer();
                } else {
                    Arith_NumpadInput();
                }
                ui_arith_game_update();
                return true;
            }
        } else {
            /* 已回答: 按确认进入下一题或结束 */
            if (action == JOY_ACTION_PRESS) {
                if (!Arith_IsDone()) {
                    Arith_Next();
                    ui_arith_game_update();
                } else {
                    UI_SwitchPage(PAGE_ARITHMETIC);
                }
                return true;
            }
        }
        return true;
    }

    /* ---- 声音测试 ---- */
    if (s_current_page == PAGE_SOUND_TEST) {
        if (action == JOY_ACTION_LONG_PRESS || action == JOY_ACTION_LEFT) {
            UI_SwitchPage(PAGE_GAME_MENU);
            return true;
        }
        return true;  /* 屏蔽其它操作 */
    }

    return false;
}

/* =================================================================
 * UI 初始化 — 创建所有页面
 * ================================================================= */
void UI_Init(void)
{
    /* 初始化素描画系统 (检查 SD 卡目录) */
    Sketch_Init();

    s_scr_main_menu    = ui_create_main_menu();
    s_scr_word_learn   = ui_create_word_screen();
    s_scr_phrase_learn = ui_create_phrase_screen();
    s_scr_game_menu    = ui_create_game_menu();
    s_scr_settings     = ui_create_settings_screen();
    s_scr_tetris       = ui_create_tetris_game();
    /* 贪吃蛇: 初始化并创建 LVGL 屏幕 */
    Snake_Init();
    Snake_CreateScreen();
    s_scr_photo_view   = ui_create_photo_view();
    s_scr_arithmetic   = ui_create_arith_level_screen();
    s_scr_arithmetic_game = ui_create_arith_game_screen();
    s_scr_sound_test = ui_create_sound_test();

    UI_SwitchPage(PAGE_MAIN_MENU);
    ESP_LOGI(TAG_UI, "UI initialized");
}

/* =================================================================
 * 状态查询
 * ================================================================= */
bool UI_IsReady(void) { return true; }

/* =================================================================
 * 贪吃蛇游戏 (stub — 待实现)
 * ================================================================= */
void UI_Snake_Start(void)   {}
void UI_Snake_Pause(void)   {}
void UI_Snake_Resume(void)  {}
void UI_Snake_Restart(void) {}
void UI_Snake_Tick(void)    {}
void UI_Snake_SetDirection(int dx, int dy) {}

/* =================================================================
 * 俄罗斯方块游戏
 * ================================================================= */
#define TETRIS_W      10
#define TETRIS_H      20
#define TETRIS_CELL   12

static const uint8_t g_tetris_pieces[7][4][4] = {
    { {0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0} },  /* I */
    { {0,0,0,0}, {0,1,1,0}, {0,1,1,0}, {0,0,0,0} },  /* O */
    { {0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0} },  /* S */
    { {0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0} },  /* Z */
    { {0,0,0,0}, {0,1,0,0}, {1,1,1,0}, {0,0,0,0} },  /* J */
    { {0,0,0,0}, {0,0,1,0}, {1,1,1,0}, {0,0,0,0} },  /* L */
    { {0,0,0,0}, {0,1,0,0}, {0,1,0,0}, {1,1,0,0} },  /* T */
};

static const uint8_t g_tetris_colors[7] = { 1, 2, 3, 4, 5, 6, 7 };

typedef struct {
    uint8_t board[TETRIS_H][TETRIS_W];
    int current_piece;
    int x, y;
    int rotation;
    int score;
    int lines;
    int level;
    bool game_over;
    bool paused;
    int tick_ms;
} TetrisState_t;

static TetrisState_t s_tetris_state;
static lv_obj_t *s_tetris_canvas = NULL;
static lv_obj_t *s_tetris_score_label = NULL;
static lv_obj_t *s_tetris_lines_label = NULL;

static bool ui_tetris_check_collision(int px, int py, int rot)
{
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int src_x = rot == 0 ? x : (rot == 1 ? 3 - y : (rot == 2 ? 3 - x : y));
            int src_y = rot == 0 ? y : (rot == 1 ? x : (rot == 2 ? 3 - y : 3 - x));
            if (g_tetris_pieces[s_tetris_state.current_piece][src_y][src_x]) {
                int board_x = px + x;
                int board_y = py + y;
                if (board_x < 0 || board_x >= TETRIS_W || board_y >= TETRIS_H) return true;
                if (board_y >= 0 && s_tetris_state.board[board_y][board_x]) return true;
            }
        }
    }
    return false;
}

static void ui_tetris_place_piece(void)
{
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int src_x = s_tetris_state.rotation == 0 ? x : (s_tetris_state.rotation == 1 ? 3 - y : (s_tetris_state.rotation == 2 ? 3 - x : y));
            int src_y = s_tetris_state.rotation == 0 ? y : (s_tetris_state.rotation == 1 ? x : (s_tetris_state.rotation == 2 ? 3 - y : 3 - x));
            if (g_tetris_pieces[s_tetris_state.current_piece][src_y][src_x]) {
                int bx = s_tetris_state.x + x;
                int by = s_tetris_state.y + y;
                if (by >= 0 && by < TETRIS_H && bx >= 0 && bx < TETRIS_W) {
                    s_tetris_state.board[by][bx] = g_tetris_colors[s_tetris_state.current_piece];
                }
            }
        }
    }

    int cleared = 0;
    for (int y = TETRIS_H - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < TETRIS_W; x++) {
            if (!s_tetris_state.board[y][x]) { full = false; break; }
        }
        if (full) {
            cleared++;
            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < TETRIS_W; x++)
                    s_tetris_state.board[yy][x] = s_tetris_state.board[yy-1][x];
            }
            for (int x = 0; x < TETRIS_W; x++) s_tetris_state.board[0][x] = 0;
            y++;
        }
    }

    if (cleared > 0) {
        s_tetris_state.lines += cleared;
        s_tetris_state.score += cleared * 100 * cleared;
    }

    s_tetris_state.current_piece = rand() % 7;
    s_tetris_state.x = TETRIS_W / 2 - 2;
    s_tetris_state.y = 0;
    s_tetris_state.rotation = 0;

    if (ui_tetris_check_collision(s_tetris_state.x, s_tetris_state.y, 0))
        s_tetris_state.game_over = true;
}

static void ui_tetris_update_display(void)
{
    lv_canvas_fill_bg(s_tetris_canvas, lv_color_white(), LV_OPA_COVER);

    static lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    static const lv_color_t colors[] = {
        lv_color_black(), lv_color_hex(0x00ffff), lv_color_hex(0xffff00),
        lv_color_hex(0xff00ff), lv_color_hex(0x00ff00), lv_color_hex(0xff0000),
        lv_color_hex(0x0000ff), lv_color_hex(0xff8800)
    };

    for (int y = 0; y < TETRIS_H; y++) {
        for (int x = 0; x < TETRIS_W; x++) {
            if (s_tetris_state.board[y][x]) {
                rect_dsc.bg_color = colors[s_tetris_state.board[y][x] % 8];
                lv_canvas_draw_rect(s_tetris_canvas, x * TETRIS_CELL, y * TETRIS_CELL,
                                    TETRIS_CELL - 1, TETRIS_CELL - 1, &rect_dsc);
            }
        }
    }

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int src_x = s_tetris_state.rotation == 0 ? x : (s_tetris_state.rotation == 1 ? 3 - y : (s_tetris_state.rotation == 2 ? 3 - x : y));
            int src_y = s_tetris_state.rotation == 0 ? y : (s_tetris_state.rotation == 1 ? x : (s_tetris_state.rotation == 2 ? 3 - y : 3 - x));
            if (g_tetris_pieces[s_tetris_state.current_piece][src_y][src_x]) {
                int bx = s_tetris_state.x + x;
                int by = s_tetris_state.y + y;
                if (bx >= 0 && bx < TETRIS_W && by >= 0 && by < TETRIS_H) {
                    rect_dsc.bg_color = colors[g_tetris_colors[s_tetris_state.current_piece] % 8];
                    lv_canvas_draw_rect(s_tetris_canvas, bx * TETRIS_CELL, by * TETRIS_CELL,
                                        TETRIS_CELL - 1, TETRIS_CELL - 1, &rect_dsc);
                }
            }
        }
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "分数:%d", s_tetris_state.score);
    lv_label_set_text(s_tetris_score_label, buf);
    snprintf(buf, sizeof(buf), "行:%d", s_tetris_state.lines);
    lv_label_set_text(s_tetris_lines_label, buf);
}

void UI_Tetris_Start(void)
{
    memset(s_tetris_state.board, 0, sizeof(s_tetris_state.board));
    s_tetris_state.current_piece = rand() % 7;
    s_tetris_state.x = TETRIS_W / 2 - 2;
    s_tetris_state.y = 0;
    s_tetris_state.rotation = 0;
    s_tetris_state.score = 0;
    s_tetris_state.lines = 0;
    s_tetris_state.level = 1;
    s_tetris_state.game_over = false;
    s_tetris_state.paused = false;
    s_tetris_state.tick_ms = 500;
    ui_tetris_update_display();
}

void UI_Tetris_Pause(void)  { s_tetris_state.paused = true; }
void UI_Tetris_Resume(void) { s_tetris_state.paused = false; }
void UI_Tetris_Restart(void) { UI_Tetris_Start(); }

void UI_Tetris_Tick(void)
{
    if (s_current_page != PAGE_TETRIS_GAME) return;
    if (s_tetris_state.game_over || s_tetris_state.paused) return;

    if (!ui_tetris_check_collision(s_tetris_state.x, s_tetris_state.y + 1, s_tetris_state.rotation))
        s_tetris_state.y++;
    else
        ui_tetris_place_piece();
    ui_tetris_update_display();
}

void UI_Tetris_SetDirection(int dx, int dy)
{
    if (s_tetris_state.game_over || s_tetris_state.paused) return;
    if (dy > 0) {
        if (!ui_tetris_check_collision(s_tetris_state.x, s_tetris_state.y + 1, s_tetris_state.rotation)) {
            s_tetris_state.y++;
            ui_tetris_update_display();
        }
    } else if (dx != 0) {
        if (!ui_tetris_check_collision(s_tetris_state.x + dx, s_tetris_state.y, s_tetris_state.rotation)) {
            s_tetris_state.x += dx;
            ui_tetris_update_display();
        }
    }
}

void UI_Tetris_Rotate(void)
{
    if (s_tetris_state.game_over || s_tetris_state.paused) return;
    int new_rot = (s_tetris_state.rotation + 1) % 4;
    if (!ui_tetris_check_collision(s_tetris_state.x, s_tetris_state.y, new_rot))
        s_tetris_state.rotation = new_rot;
    else if (!ui_tetris_check_collision(s_tetris_state.x - 1, s_tetris_state.y, new_rot))
        { s_tetris_state.x -= 1; s_tetris_state.rotation = new_rot; }
    else if (!ui_tetris_check_collision(s_tetris_state.x + 1, s_tetris_state.y, new_rot))
        { s_tetris_state.x += 1; s_tetris_state.rotation = new_rot; }
    ui_tetris_update_display();
}

static lv_obj_t* ui_create_tetris_game(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "俄罗斯方块");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_text_font(title, &font_cn_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 5);

    s_tetris_score_label = lv_label_create(scr);
    lv_label_set_text(s_tetris_score_label, "分数:0");
    lv_obj_set_style_text_color(s_tetris_score_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_tetris_score_label, &font_cn_16, 0);
    lv_obj_align(s_tetris_score_label, LV_ALIGN_TOP_RIGHT, -10, 5);

    s_tetris_lines_label = lv_label_create(scr);
    lv_label_set_text(s_tetris_lines_label, "行:0");
    lv_obj_set_style_text_color(s_tetris_lines_label, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_tetris_lines_label, &font_cn_16, 0);
    lv_obj_align(s_tetris_lines_label, LV_ALIGN_TOP_RIGHT, -80, 5);

    static uint8_t cbuf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(TETRIS_W * TETRIS_CELL, TETRIS_H * TETRIS_CELL)];
    s_tetris_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_tetris_canvas, cbuf, TETRIS_W * TETRIS_CELL, TETRIS_H * TETRIS_CELL, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_tetris_canvas, LV_ALIGN_CENTER, 0, -5);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "上下:旋转/下落  左右:移动  长按:返回");
    lv_obj_set_style_text_color(hint, lv_color_black(), 0);
    lv_obj_set_style_text_font(hint, &font_cn_16, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    return scr;
}
