#pragma once

/**
 * @file arithmetic.h
 * @brief 算术练习模块 — 幼儿园到三年级
 *
 * 交互方式（摇杆+按钮）:
 *   比大小: ← → 移动高亮, 按确认
 *   选择题: ↑ ↓ 滚动选项, 按确认
 *
 * 四级难度:
 *   幼儿园: 1-10 比大小 + 加法
 *   一年级: 1-100 比大小 + 20以内加减
 *   二年级: 1-100 加减 + 乘法口诀
 *   三年级: 1-100 加减乘除
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ========== 常量 ========== */
#define ARITH_Q_PER_SESSION  10   /* 每轮题数 */
#define ARITH_OPTIONS_COUNT   4   /* 选择题选项数 (保留不用) */
#define ARITH_MAX_DIGITS      6   /* 竖式答案最多位数 */
#define ARITH_NP_COLS         3   /* 九宫格列数 */
#define ARITH_NP_ROWS         4   /* 九宫格行数 */
#define ARITH_NP_NONE        -1

/* ========== 题目类型 ========== */
typedef enum {
    ARITH_Q_COMPARE = 0,   /* 比大小: ← → 选择 */
    ARITH_Q_NUMPAD,        /* 竖式 + 九宫格数字输入 */
} ArithQuestionType_t;

/* ========== 难度等级 ========== */
typedef enum {
    ARITH_LEVEL_KINDERGARTEN = 0,  /* 幼儿园 */
    ARITH_LEVEL_GRADE_1,           /* 一年级 */
    ARITH_LEVEL_GRADE_2,           /* 二年级 */
    ARITH_LEVEL_GRADE_3,           /* 三年级 */
    ARITH_LEVEL_COUNT
} ArithLevel_t;

/* ========== 等级名称 ========== */
/** 获取等级名称 */
extern const char* Arith_GetLevelName(ArithLevel_t level);

/** 获取运算符的显示文本 (如 '*'→"×", '/'→"÷") */
extern const char* Arith_GetOpDisplay(char op);

/* ========== 单道题 ========== */
typedef struct {
    ArithQuestionType_t type;  /* 题型 */
    int a, b;                  /* 操作数/比较数 */
    char op;                   /* 运算符 '+','-','*','/','?' (ASCII, 不含多字节) */
    int answer;                /* 正确答案 */
    /* 比大小用 */
    int cursor_side;           /* 0=左(大), 1=右(大) */
} ArithQuestion_t;

/* ========== 九宫格状态 ========== */
typedef struct {
    int cursor_col;                         /* 0-2 */
    int cursor_row;                         /* 0-3 */
    int digits[ARITH_MAX_DIGITS];           /* 已输入的数字 (索引>>) */
    int digit_count;
} ArithNumPad_t;

/* ========== 练习状态 ========== */
typedef struct {
    ArithLevel_t level;                     /* 当前难度 */
    int question_index;                     /* 当前第几题 (0~9) */
    int correct_count;                      /* 正确数 */
    int score;                              /* 总分 */
    int cursor;                             /* 当前选中 (比大小:0=左,1=右) */
    bool answered;                          /* 是否已作答 */
    bool last_correct;                      /* 上题是否正确 */
    int streak;                             /* 连续正确 */
    ArithQuestion_t questions[ARITH_Q_PER_SESSION];
    ArithNumPad_t numpad;                   /* 九宫格状态 (竖式模式) */
    bool session_done;                      /* 本轮完成 */
} ArithState_t;

/* ========== API ========== */

/** 初始化并生成新一轮题目 */
void Arith_Init(ArithLevel_t level);

/** 获取状态指针 */
ArithState_t* Arith_GetState(void);

/** 获取当前题目 */
const ArithQuestion_t* Arith_GetCurrentQuestion(void);

/** 获取当前题型 */
ArithQuestionType_t Arith_GetCurrentType(void);

/** 用当前 cursor 位置作答 (比大小) */
bool Arith_Answer(void);

/** 进入下一题, 返回 false 表示本轮结束 */
bool Arith_Next(void);

/** 移动 cursor (比大小: 0↔1) */
void Arith_CursorMove(int dir);

/** ===== 九宫格操作 (竖式模式) ===== */

/** 移动九宫格光标 (dx,dy ∈ {-1,0,1}) */
void Arith_NumpadMove(int dx, int dy);

/** 输入当前光标上的数字/操作, 返回新 digit_count */
int Arith_NumpadInput(void);

/** 退格删除最后一位数字 */
void Arith_NumpadBackspace(void);

/** 获取九宫格光标位置的标签文字 (返回静态字符串) */
const char* Arith_NumpadGetKey(int col, int row);

/** 获取当前已输入的数字 (作为整数, 0 表示空) */
int Arith_NumpadGetValue(void);

/** 检查已输入数字是否正确 */
bool Arith_NumpadCheckAnswer(void);

/** 获取进度文本如 "3/10" */
void Arith_GetProgress(char *buf, int buf_size);

/** 获取总分 */
int Arith_GetScore(void);

/** 获取统计摘要 */
void Arith_GetSummary(char *buf, int buf_size);

/** 重置本轮 */
void Arith_Reset(void);

/** 检查是否本轮完成 */
bool Arith_IsDone(void);

#ifdef __cplusplus
}
#endif
