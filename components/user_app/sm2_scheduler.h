/**
 * @file sm2_scheduler.h
 * @brief SM-2 记忆曲线调度算法 — 间隔重复 + 错误追踪
 *
 * 基于 SuperMemo SM-2 算法改进版，专门为英语单词学习优化。
 *
 * 质量评分 (q):
 *   5 = 完美回忆
 *   4 = 正确但有犹豫
 *   3 = 正确但困难
 *   2 = 错误但记得见过
 *   1 = 完全遗忘
 *   0 = 完全错误
 *
 * EF (Ease Factor) 更新:
 *   EF' = EF + (0.1 - (5-q) * (0.08 + (5-q) * 0.02))
 *   EF' = max(1.3, EF')
 *
 * 间隔计算:
 *   第1次正确 (q>=3): 1 天
 *   第2次正确 (q>=3): 6 天
 *   第3次起:          前一间隔 * EF
 *   最长间隔:          365 天
 *   回答错误 (q<3):   重置为 1 小时
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* 需要 WordProgress_t 类型用于获取新词 */
#include "vocab_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 学习阶段 ========== */
typedef enum {
    SM2_LEVEL_NEW = 0,         /* 未学 */
    SM2_LEVEL_LEARNING,        /* 学习中 (阶段1-2) */
    SM2_LEVEL_REVIEWING,       /* 复习中 (阶段3+) */
    SM2_LEVEL_MASTERED,        /* 已掌握 (间隔 > 30天) */
} Sm2Level_t;

/* ========== 学习质量评分 ========== */
#define SM2_Q_BLACKOUT     0   /* 完全错误 */
#define SM2_Q_WRONG        1   /* 忘记 */
#define SM2_Q_HARD_WRONG   2   /* 错误但记得见过 */
#define SM2_Q_HARD_CORRECT 3   /* 正确但困难 */
#define SM2_Q_CORRECT      4   /* 正确有犹豫 */
#define SM2_Q_PERFECT      5   /* 完美回忆 */

/* ========== 调度状态 (单个词条) ========== */
typedef struct {
    uint16_t word_id;           /* 单词索引 */
    uint8_t  repetition;        /* 连续正确次数 (q>=3) */
    uint8_t  total_reviews;     /* 总复习次数 */
    uint8_t  correct_count;     /* 总正确次数 (q>=3) */
    uint8_t  error_count;       /* 总错误次数 (q<3) */
    uint8_t  consecutive_correct; /* 连续正确 (含质量) */
    uint8_t  quality_last;      /* 上次评分 */
    float    ease_factor;       /* EF 值 1.3~∞ */
    uint16_t interval_days;     /* 当前间隔 (天) */
    uint32_t next_review;       /* 下次复习时间 (Unix秒) */
    uint32_t last_review;       /* 上次复习时间 */
    uint8_t  level;             /* Sm2Level_t */
} Sm2Item_t;

/* ========== 调度器 ========== */

/**
 * 初始化 SM-2 调度器
 */
void Sm2_Init(void);

/**
 * 记录一次复习质量评分并返回更新后的项
 * @param item  调度项指针 (会被修改)
 * @param quality 质量评分 0-5
 * @param now  当前 Unix 时间戳
 */
void Sm2_RecordReview(Sm2Item_t *item, uint8_t quality, uint32_t now);

/**
 * 获取当前需要复习的词列表 (按 overdue 程度排序)
 * @param items   输出数组
 * @param max     max items
 * @param now     当前时间
 * @return        overdue 数量
 */
int Sm2_GetDueItems(Sm2Item_t *items[], int max, uint32_t now);

/**
 * 获取当前学习阶段的微弱词 (高错误率)
 * @param items   输出数组
 * @param max     max items
 * @return        数量
 */
int Sm2_GetWeakItems(Sm2Item_t *items[], int max);

/**
 * 获取可学习的新词 (level=NEW, 按 frequency 排序)
 * @param items   输出数组
 * @param max     max items
 * @param total   总词库大小
 * @param progress 进度数组
 * @return        推荐新词数
 */
int Sm2_GetNewItems(Sm2Item_t *items[], int max,
                    const WordProgress_t *progress, int total);

/**
 * 获取统计信息
 */
int  Sm2_GetTotalLearned(void);     /* 已学的词数 */
int  Sm2_GetTotalDue(void);         /* 待复习词数 */
int  Sm2_GetTotalMastered(void);    /* 已掌握词数 */
float Sm2_GetAccuracy(void);        /* 总正确率 */

#ifdef __cplusplus
}
#endif
