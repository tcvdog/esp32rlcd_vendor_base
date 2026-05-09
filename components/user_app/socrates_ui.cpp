#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "ui_manager.h"
#include "socrates_ui.h"
#include "socrates_content.h"
#include "tts_player.h"
#include "joystick_bsp.h"

LV_FONT_DECLARE(font_cn_16);
LV_FONT_DECLARE(font_cn_24);

static const char *TAG = "SocratesUI";

static int   s_current_page = -1;
static lv_obj_t *s_scr_menu   = NULL;
static lv_obj_t *s_scr_qa      = NULL;
static lv_obj_t *s_scr_browse  = NULL;
static lv_obj_t *s_scr_method  = NULL;

static int   s_menu_sel = 0;
static lv_obj_t *s_menu_items[4] = {NULL};

static int   s_cat_sel = 0;
static lv_obj_t *s_cat_items[SOCRATES_MAX_CATEGORIES] = {NULL};
static int   s_cat_count = 0;

static const SocraticQuestion_t *s_cur_q = NULL;
static int   s_selected_opt = -1;
static bool  s_show_hint = false;
static bool  s_show_result = false;

static lv_obj_t *s_qa_question_lb  = NULL;
static lv_obj_t *s_qa_hint_lb    = NULL;
static lv_obj_t *s_qa_opt_lb[4]   = {NULL};
static lv_obj_t *s_qa_result_lb  = NULL;
static lv_obj_t *s_qa_further_lb = NULL;
static lv_obj_t *s_qa_status_lb  = NULL;

static int   s_method_sel = 0;
static int   s_method_cat_sel = 0;
static const char *s_method_cats[] = {"理解", "专注", "记忆", "笔记", "好奇"};

static lv_obj_t *s_method_cat_lb[5] = {NULL};
static lv_obj_t *s_method_list_lb[SOCRATES_MAX_METHODS] = {NULL};
static int   s_method_count = 0;

static void set_black_white(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_white(), 0);
    lv_obj_set_style_text_color(obj, lv_color_black(), 0);
}

static lv_obj_t* make_label(lv_obj_t *parent, const char *text, const lv_font_t *font)
{
    lv_obj_t *lb = lv_label_create(parent);
    lv_label_set_text(lb, text);
    lv_obj_set_style_text_color(lb, lv_color_black(), 0);
    if (font) lv_obj_set_style_text_font(lb, font, 0);
    return lb;
}

static void menu_update_highlight(void)
{
    for (int i = 0; i < 4; i++) {
        if (!s_menu_items[i]) continue;
        if (i == s_menu_sel) {
            lv_obj_set_style_text_color(s_menu_items[i], lv_color_white(), 0);
            lv_obj_set_style_bg_color(s_menu_items[i], lv_color_black(), 0);
        } else {
            lv_obj_set_style_text_color(s_menu_items[i], lv_color_black(), 0);
            lv_obj_set_style_bg_color(s_menu_items[i], lv_color_white(), 0);
        }
    }
}

static lv_obj_t* create_menu_scr(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    set_black_white(scr);

    lv_obj_t *title = make_label(scr, "苏格拉底百科全书", &font_cn_24);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    const char *items[] = {"随机出题", "按分类浏览", "记忆学习法", "返回主菜单"};
    int y_pos[] = {-30, 0, 30, 60};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *lb = make_label(scr, items[i], &font_cn_16);
        lv_obj_set_style_bg_color(lb, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        lv_obj_align(lb, LV_ALIGN_CENTER, 0, y_pos[i]);
        s_menu_items[i] = lb;
    }
    menu_update_highlight();

    lv_obj_t *hint = make_label(scr, "摇杆上下选择  按下确认  长按返回", &font_cn_16);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    return scr;
}

static void qa_update_display(void)
{
    if (!s_cur_q) return;

    lv_label_set_text(s_qa_question_lb, s_cur_q->question);

    if (s_show_result && s_selected_opt >= 0) {
        bool correct = s_cur_q->options[s_selected_opt].correct;
        lv_label_set_text(s_qa_result_lb, correct ? "回答正确" : "回答错误");
        lv_obj_set_style_text_color(s_qa_result_lb,
            correct ? lv_color_black() : lv_color_hex(0x333333), 0);
        lv_obj_clear_flag(s_qa_result_lb, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text(s_qa_further_lb,
            s_cur_q->options[s_selected_opt].explanation);
        lv_obj_clear_flag(s_qa_further_lb, LV_OBJ_FLAG_HIDDEN);
    } else if (s_show_hint) {
        lv_label_set_text(s_qa_hint_lb, s_cur_q->socratic_hint);
        lv_obj_clear_flag(s_qa_hint_lb, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_qa_result_lb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_qa_further_lb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_qa_hint_lb, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < s_cur_q->option_count && i < 4; i++) {
        if (!s_qa_opt_lb[i]) continue;
        if (s_show_result) {
            if (s_cur_q->options[i].correct) {
                lv_obj_set_style_text_color(s_qa_opt_lb[i], lv_color_black(), 0);
                lv_obj_set_style_bg_color(s_qa_opt_lb[i], lv_color_white(), 0);
            } else if (i == s_selected_opt) {
                lv_obj_set_style_text_color(s_qa_opt_lb[i], lv_color_hex(0x333333), 0);
                lv_obj_set_style_bg_color(s_qa_opt_lb[i], lv_color_white(), 0);
            } else {
                lv_obj_set_style_text_color(s_qa_opt_lb[i], lv_color_black(), 0);
                lv_obj_set_style_bg_color(s_qa_opt_lb[i], lv_color_white(), 0);
            }
        } else if (i == s_selected_opt) {
            lv_obj_set_style_text_color(s_qa_opt_lb[i], lv_color_white(), 0);
            lv_obj_set_style_bg_color(s_qa_opt_lb[i], lv_color_black(), 0);
        } else {
            lv_obj_set_style_text_color(s_qa_opt_lb[i], lv_color_black(), 0);
            lv_obj_set_style_bg_color(s_qa_opt_lb[i], lv_color_white(), 0);
        }
    }

    char buf[64];
    if (s_cur_q->grade[0]) {
        snprintf(buf, sizeof(buf), "%s | %s | 难度:%d",
                 s_cur_q->category, s_cur_q->grade, s_cur_q->difficulty);
    } else {
        snprintf(buf, sizeof(buf), "%s | 难度:%d",
                 s_cur_q->category, s_cur_q->difficulty);
    }
    lv_label_set_text(s_qa_status_lb, buf);
}

static lv_obj_t* create_qa_scr(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    set_black_white(scr);

    s_qa_status_lb = make_label(scr, "", &font_cn_16);
    lv_obj_align(s_qa_status_lb, LV_ALIGN_TOP_LEFT, 8, 5);

    s_qa_question_lb = make_label(scr, "问题加载中...", &font_cn_24);
    lv_obj_set_width(s_qa_question_lb, 380);
    lv_obj_set_style_text_align(s_qa_question_lb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_qa_question_lb, LV_ALIGN_TOP_MID, 0, 30);

    s_qa_hint_lb = make_label(scr, "", &font_cn_16);
    lv_obj_set_width(s_qa_hint_lb, 360);
    lv_obj_set_style_text_align(s_qa_hint_lb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_qa_hint_lb, lv_color_hex(0x444444), 0);
    lv_obj_align(s_qa_hint_lb, LV_ALIGN_CENTER, 0, -10);
    lv_obj_add_flag(s_qa_hint_lb, LV_OBJ_FLAG_HIDDEN);

    int y_pos[] = {20, 50, 80, 110};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *lb = make_label(scr, "", &font_cn_16);
        lv_obj_set_width(lb, 360);
        lv_obj_set_style_bg_color(lb, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        lv_obj_align(lb, LV_ALIGN_CENTER, 0, y_pos[i]);
        s_qa_opt_lb[i] = lb;
    }

    s_qa_result_lb = make_label(scr, "", &font_cn_24);
    lv_obj_align(s_qa_result_lb, LV_ALIGN_CENTER, 0, -40);
    lv_obj_add_flag(s_qa_result_lb, LV_OBJ_FLAG_HIDDEN);

    s_qa_further_lb = make_label(scr, "", &font_cn_16);
    lv_obj_set_width(s_qa_further_lb, 360);
    lv_obj_set_style_text_align(s_qa_further_lb, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_qa_further_lb, lv_color_hex(0x444444), 0);
    lv_obj_align(s_qa_further_lb, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_add_flag(s_qa_further_lb, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *hint = make_label(scr,
        "上下选选项  按下确认  左键提示  长按返回", &font_cn_16);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    return scr;
}

static void browse_update_highlight(void)
{
    for (int i = 0; i < s_cat_count; i++) {
        if (!s_cat_items[i]) continue;
        if (i == s_cat_sel) {
            lv_obj_set_style_text_color(s_cat_items[i], lv_color_white(), 0);
            lv_obj_set_style_bg_color(s_cat_items[i], lv_color_black(), 0);
        } else {
            lv_obj_set_style_text_color(s_cat_items[i], lv_color_black(), 0);
            lv_obj_set_style_bg_color(s_cat_items[i], lv_color_white(), 0);
        }
    }
}

static lv_obj_t* create_browse_scr(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    set_black_white(scr);

    lv_obj_t *title = make_label(scr, "按分类浏览", &font_cn_24);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    SocraticCategory_t cats[SOCRATES_MAX_CATEGORIES];
    s_cat_count = Socrates_GetCategories(cats, SOCRATES_MAX_CATEGORIES);

    for (int i = 0; i < s_cat_count && i < SOCRATES_MAX_CATEGORIES; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[%s] %s (%d题)",
                 cats[i].icon, cats[i].name, cats[i].question_count);
        lv_obj_t *lb = make_label(scr, buf, &font_cn_16);
        lv_obj_set_style_bg_color(lb, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        lv_obj_align(lb, LV_ALIGN_CENTER, 0, -50 + i * 28);
        s_cat_items[i] = lb;
    }
    browse_update_highlight();

    lv_obj_t *hint = make_label(scr,
        "上下选择  按下出题  左键返回", &font_cn_16);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    return scr;
}

static void method_cat_update(void)
{
    for (int i = 0; i < 5; i++) {
        if (!s_method_cat_lb[i]) continue;
        if (i == s_method_cat_sel) {
            lv_obj_set_style_text_color(s_method_cat_lb[i], lv_color_white(), 0);
            lv_obj_set_style_bg_color(s_method_cat_lb[i], lv_color_black(), 0);
        } else {
            lv_obj_set_style_text_color(s_method_cat_lb[i], lv_color_black(), 0);
            lv_obj_set_style_bg_color(s_method_cat_lb[i], lv_color_white(), 0);
        }
    }
}

static void method_list_update(void)
{
    const MemoryMethod_t *ms[SOCRATES_MAX_METHODS];
    s_method_count = Socrates_GetMethodsByCategory(
        s_method_cats[s_method_cat_sel], ms, SOCRATES_MAX_METHODS);

    for (int i = 0; i < s_method_count && i < SOCRATES_MAX_METHODS; i++) {
        if (!s_method_list_lb[i]) continue;
        lv_label_set_text(s_method_list_lb[i], ms[i]->name);
        if (i == s_method_sel) {
            lv_obj_set_style_text_color(s_method_list_lb[i], lv_color_white(), 0);
            lv_obj_set_style_bg_color(s_method_list_lb[i], lv_color_black(), 0);
        } else {
            lv_obj_set_style_text_color(s_method_list_lb[i], lv_color_black(), 0);
            lv_obj_set_style_bg_color(s_method_list_lb[i], lv_color_white(), 0);
        }
    }
}

static lv_obj_t* create_method_scr(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    set_black_white(scr);

    lv_obj_t *title = make_label(scr, "记忆与学习法", &font_cn_24);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    for (int i = 0; i < 5; i++) {
        lv_obj_t *lb = make_label(scr, s_method_cats[i], &font_cn_16);
        lv_obj_set_style_bg_color(lb, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 3, 0);
        lv_obj_align(lb, LV_ALIGN_TOP_MID, -80 + i * 40, 45);
        s_method_cat_lb[i] = lb;
    }
    method_cat_update();

    for (int i = 0; i < SOCRATES_MAX_METHODS; i++) {
        lv_obj_t *lb = make_label(scr, "", &font_cn_16);
        lv_obj_set_style_bg_color(lb, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(lb, 4, 0);
        lv_obj_align(lb, LV_ALIGN_CENTER, 0, -30 + i * 26);
        s_method_list_lb[i] = lb;
    }
    method_list_update();

    lv_obj_t *hint = make_label(scr,
        "左右切分类  上下选方法  按下查看  长按返回", &font_cn_16);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    return scr;
}

static lv_obj_t* create_method_detail_scr(const MemoryMethod_t *m)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    set_black_white(scr);

    lv_obj_t *title = make_label(scr, m->name, &font_cn_24);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *cat = make_label(scr, m->category, &font_cn_16);
    lv_obj_set_style_text_color(cat, lv_color_hex(0x666666), 0);
    lv_obj_align(cat, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *sum = make_label(scr, m->summary, &font_cn_16);
    lv_obj_set_width(sum, 360);
    lv_obj_set_style_text_align(sum, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sum, LV_ALIGN_TOP_MID, 0, 65);

    for (int i = 0; i < m->step_count && i < SOCRATES_METHOD_STEPS; i++) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%d. %s", i+1, m->steps[i]);
        lv_obj_t *lb = make_label(scr, buf, &font_cn_16);
        lv_obj_set_width(lb, 360);
        lv_obj_align(lb, LV_ALIGN_TOP_LEFT, 20, 100 + i * 22);
    }

    lv_obj_t *tip_label = make_label(scr, "苏格拉底式提示", &font_cn_16);
    lv_obj_set_style_text_color(tip_label, lv_color_hex(0x444444), 0);
    lv_obj_align(tip_label, LV_ALIGN_BOTTOM_LEFT, 8, -50);

    lv_obj_t *tip = make_label(scr, m->socratic_tip, &font_cn_16);
    lv_obj_set_width(tip, 360);
    lv_obj_set_style_text_color(tip, lv_color_hex(0x444444), 0);
    lv_obj_align(tip, LV_ALIGN_BOTTOM_LEFT, 8, -25);

    lv_obj_t *kids = make_label(scr, "孩子版", &font_cn_16);
    lv_obj_set_style_text_color(kids, lv_color_hex(0x444444), 0);
    lv_obj_align(kids, LV_ALIGN_BOTTOM_LEFT, 8, -100);

    lv_obj_t *kids_tip = make_label(scr, m->for_kids, &font_cn_16);
    lv_obj_set_width(kids_tip, 360);
    lv_obj_set_style_text_color(kids_tip, lv_color_hex(0x444444), 0);
    lv_obj_align(kids_tip, LV_ALIGN_BOTTOM_LEFT, 8, -75);

    lv_obj_t *hint = make_label(scr, "长按返回列表", &font_cn_16);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    return scr;
}

void SocratesUI_Init(void)
{
    if (s_scr_menu) return;
    ESP_LOGI(TAG, "Init Socrates UI");
    s_scr_menu   = create_menu_scr();
    s_scr_qa    = create_qa_scr();
    s_scr_browse  = create_browse_scr();
    s_scr_method  = create_method_scr();
}

void SocratesUI_SwitchPage(AppPage_t page)
{
    s_current_page = page;
    s_show_hint = false;
    s_show_result = false;
    s_selected_opt = -1;

    switch (page) {
        case PAGE_SOCRATES_MENU:   lv_scr_load(s_scr_menu);   break;
        case PAGE_SOCRATES_QA:     lv_scr_load(s_scr_qa);     break;
        case PAGE_SOCRATES_BROWSE: lv_scr_load(s_scr_browse); break;
        case PAGE_SOCRATES_METHOD: lv_scr_load(s_scr_method); break;
        default: break;
    }
}

void SocratesUI_SetQuestion(const SocraticQuestion_t *q)
{
    s_cur_q = q;
    s_selected_opt = -1;
    s_show_hint = false;
    s_show_result = false;
    if (!q) return;

    for (int i = 0; i < q->option_count && i < 4; i++) {
        if (s_qa_opt_lb[i])
            lv_label_set_text(s_qa_opt_lb[i], q->options[i].text);
    }
    for (int i = q->option_count; i < 4; i++) {
        if (s_qa_opt_lb[i])
            lv_obj_add_flag(s_qa_opt_lb[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < q->option_count; i++) {
        if (s_qa_opt_lb[i])
            lv_obj_clear_flag(s_qa_opt_lb[i], LV_OBJ_FLAG_HIDDEN);
    }
    qa_update_display();
}

void SocratesUI_SetMethod(const MemoryMethod_t *m)
{
    if (!m) return;
    lv_obj_t *detail = create_method_detail_scr(m);
    lv_scr_load(detail);
}

bool SocratesUI_HandleJoystick(int action)
{
    if (s_current_page == PAGE_SOCRATES_MENU) {
        if (action == JOY_ACTION_UP) {
            s_menu_sel = (s_menu_sel > 0) ? s_menu_sel - 1 : 3;
            menu_update_highlight();
            return true;
        }
        if (action == JOY_ACTION_DOWN) {
            s_menu_sel = (s_menu_sel < 3) ? s_menu_sel + 1 : 0;
            menu_update_highlight();
            return true;
        }
        if (action == JOY_ACTION_PRESS) {
            if (s_menu_sel == 0) {
                const SocraticQuestion_t *q = Socrates_GetRandomQuestion();
                if (q) { SocratesUI_SetQuestion(q); SocratesUI_SwitchPage(PAGE_SOCRATES_QA); }
            } else if (s_menu_sel == 1) {
                SocratesUI_SwitchPage(PAGE_SOCRATES_BROWSE);
            } else if (s_menu_sel == 2) {
                SocratesUI_SwitchPage(PAGE_SOCRATES_METHOD);
            } else if (s_menu_sel == 3) {
                UI_SwitchPage(PAGE_MAIN_MENU);
            }
            return true;
        }
        if (action == JOY_ACTION_LONG_PRESS) {
            UI_SwitchPage(PAGE_MAIN_MENU);
            return true;
        }
        return false;
    }

    if (s_current_page == PAGE_SOCRATES_QA) {
        if (s_show_result) {
            if (action == JOY_ACTION_PRESS || action == JOY_ACTION_LONG_PRESS) {
                SocratesUI_SwitchPage(PAGE_SOCRATES_MENU);
            }
            return true;
        }
        if (s_show_hint) {
            if (action == JOY_ACTION_LEFT) {
                s_show_hint = false;
                qa_update_display();
                return true;
            }
            if (action == JOY_ACTION_PRESS) {
                s_show_hint = false;
                qa_update_display();
                return true;
            }
            return true;
        }
        if (action == JOY_ACTION_UP) {
            if (s_cur_q && s_selected_opt > 0) s_selected_opt--;
            qa_update_display();
            return true;
        }
        if (action == JOY_ACTION_DOWN) {
            if (s_cur_q && s_selected_opt < s_cur_q->option_count - 1)
                s_selected_opt++;
            qa_update_display();
            return true;
        }
        if (action == JOY_ACTION_LEFT) {
            s_show_hint = true;
            qa_update_display();
            return true;
        }
        if (action == JOY_ACTION_PRESS && s_selected_opt >= 0) {
            s_show_result = true;
            qa_update_display();
            return true;
        }
        if (action == JOY_ACTION_LONG_PRESS) {
            SocratesUI_SwitchPage(PAGE_SOCRATES_MENU);
            return true;
        }
        return false;
    }

    if (s_current_page == PAGE_SOCRATES_BROWSE) {
        if (action == JOY_ACTION_UP) {
            if (s_cat_sel > 0) s_cat_sel--;
            browse_update_highlight();
            return true;
        }
        if (action == JOY_ACTION_DOWN) {
            if (s_cat_sel < s_cat_count - 1) s_cat_sel++;
            browse_update_highlight();
            return true;
        }
        if (action == JOY_ACTION_PRESS) {
            SocraticCategory_t cats[SOCRATES_MAX_CATEGORIES];
            int cnt = Socrates_GetCategories(cats, SOCRATES_MAX_CATEGORIES);
            if (s_cat_sel < cnt) {
                const SocraticQuestion_t *q =
                    Socrates_GetRandomByCategory(cats[s_cat_sel].id);
                if (q) { SocratesUI_SetQuestion(q); SocratesUI_SwitchPage(PAGE_SOCRATES_QA); }
            }
            return true;
        }
        if (action == JOY_ACTION_LEFT || action == JOY_ACTION_LONG_PRESS) {
            SocratesUI_SwitchPage(PAGE_SOCRATES_MENU);
            return true;
        }
        return false;
    }

    if (s_current_page == PAGE_SOCRATES_METHOD) {
        if (action == JOY_ACTION_LEFT) {
            if (s_method_cat_sel > 0) s_method_cat_sel--;
            s_method_sel = 0;
            method_cat_update();
            method_list_update();
            return true;
        }
        if (action == JOY_ACTION_RIGHT) {
            if (s_method_cat_sel < 4) s_method_cat_sel++;
            s_method_sel = 0;
            method_cat_update();
            method_list_update();
            return true;
        }
        if (action == JOY_ACTION_UP) {
            if (s_method_sel > 0) s_method_sel--;
            method_list_update();
            return true;
        }
        if (action == JOY_ACTION_DOWN) {
            if (s_method_sel < s_method_count - 1) s_method_sel++;
            method_list_update();
            return true;
        }
        if (action == JOY_ACTION_PRESS) {
            const MemoryMethod_t *ms[SOCRATES_MAX_METHODS];
            int cnt = Socrates_GetMethodsByCategory(
                s_method_cats[s_method_cat_sel], ms, SOCRATES_MAX_METHODS);
            if (s_method_sel < cnt && ms[s_method_sel])
                SocratesUI_SetMethod(ms[s_method_sel]);
            return true;
        }
        if (action == JOY_ACTION_LONG_PRESS) {
            SocratesUI_SwitchPage(PAGE_SOCRATES_MENU);
            return true;
        }
        return false;
    }

    return false;
}
