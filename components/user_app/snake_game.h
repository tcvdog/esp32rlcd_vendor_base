#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 常量 ========== */
#define SNAKE_CELL     10       /* 每格像素 */
#define SNAKE_COLS     48       /* 列数 (480/10) */
#define SNAKE_ROWS     25       /* 行数 (250/10, 顶部留22px显示分数) */
#define SNAKE_MAX_LEN  (SNAKE_COLS * SNAKE_ROWS)
#define SNAKE_TICK_MS  180      /* 初始速度(毫秒/步) */

/* ========== 方向 ========== */
typedef enum {
    SNAKE_DIR_UP,
    SNAKE_DIR_DOWN,
    SNAKE_DIR_LEFT,
    SNAKE_DIR_RIGHT,
} SnakeDir_t;

/* ========== API ========== */

/** 初始化蛇 */
void Snake_Init(void);

/** 创建 LVGL 游戏屏幕 (初始化时调用一次) */
void Snake_CreateScreen(void);

/** 开始新游戏 */
void Snake_Start(void);

/** 停止游戏 */
void Snake_Stop(void);

/** 设置方向 (在下一帧生效) */
void Snake_SetDir(SnakeDir_t dir);

/** 游戏步进 (由定时器调用) */
void Snake_Tick(void);

/** 获取分数 */
int Snake_GetScore(void);

/** 是否游戏结束 */
bool Snake_IsGameOver(void);

/** 是否游戏中 */
bool Snake_IsRunning(void);

/** 重置 (重新开始) */
void Snake_Reset(void);

#ifdef __cplusplus
}
#endif
