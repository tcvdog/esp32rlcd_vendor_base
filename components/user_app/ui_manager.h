#pragma once

/**
 * @file ui_manager.h
 * @brief LVGL UI 管理层 - 主菜单 + 单词卡片 + 导航
 */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 页面枚举 ========== */
typedef enum {
    PAGE_MAIN_MENU = 0,
    PAGE_WORD_LEARN,
    PAGE_PHRASE_LEARN,
    PAGE_GAME_MENU,
    PAGE_SNAKE_GAME,
    PAGE_TETRIS_GAME,
    PAGE_SETTINGS,
    PAGE_SOCRATES_MENU,
    PAGE_SOCRATES_QA,
    PAGE_SOCRATES_BROWSE,
    PAGE_SOCRATES_METHOD,
    PAGE_PHOTO_VIEW,
    PAGE_ARITHMETIC,       /* 算术练习 — 选等级 */
    PAGE_ARITHMETIC_GAME,  /* 算术练习 — 答题 */
    PAGE_SOUND_TEST,
    PAGE_COUNT
} AppPage_t;

/* ========== 初始化与生命周期 ========== */

/* 初始化 UI: 创建所有页面 */
void UI_Init(void);

/* 切换页面 */
void UI_SwitchPage(AppPage_t page);

/* 获取当前页面 */
AppPage_t UI_GetCurrentPage(void);

/* ========== 摇杆交互 ========== */

/* 
 * 处理摇杆动作, 返回 true 表示已消费
 * 由主循环中轮询调用
 */
bool UI_HandleJoystick(int action);

/* ========== 主菜单控制 ========== */

/* 获取当前菜单选中行 */
int UI_Menu_GetSelection(void);

/* ========== 单词学习控制 ========== */

/* 切换到上一个/下一个单词 */
void UI_Word_Prev(void);
void UI_Word_Next(void);

/* 切换显示模式: 0=英文, 1=中文, 2=例句, 3=音标 */
void UI_Word_CycleDisplayMode(void);

/* 获取当前单词索引 */
int UI_Word_GetIndex(void);

/* 设置当前单词索引 */
void UI_Word_SetIndex(int idx);

/* ========== 短语学习控制 ========== */

void UI_Phrase_Prev(void);
void UI_Phrase_Next(void);

/* ========== 状态查询 ========== */

/* 返回 true 如果 UI 已经初始化完成 */
bool UI_IsReady(void);

/* ========== 贪吃蛇游戏控制 ========== */

void UI_Snake_Start(void);
void UI_Snake_Pause(void);
void UI_Snake_Resume(void);
void UI_Snake_Restart(void);
void UI_Snake_Tick(void);
void UI_Snake_SetDirection(int dx, int dy);

/* ========== 俄罗斯方块游戏控制 ========== */

void UI_Tetris_Start(void);
void UI_Tetris_Pause(void);
void UI_Tetris_Resume(void);
void UI_Tetris_Restart(void);
void UI_Tetris_Tick(void);
void UI_Tetris_SetDirection(int dx, int dy);
void UI_Tetris_Rotate(void);

#ifdef __cplusplus
}
#endif
