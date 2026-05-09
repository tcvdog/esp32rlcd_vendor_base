/**
 * @file vocab_loader.h
 * @brief PET 分级词库 JSON 加载器 — 从 SD 卡加载词库
 *
 * 从 /sdcard/vocab/ 目录读取 JSON 词库文件，
 * 解析后放入运行时数据结构，供学习引擎使用。
 *
 * 依赖: ArduinoJson (已添加到 platformio.ini)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 常量 ========== */
#define VOCAB_MAX_WORD        80    /* 最大词条数 */
#define VOCAB_MAX_PHRASE     100    /* 最大短语数 */
#define VOCAB_WORD_LEN         32   /* 单词最大长度 */
#define VOCAB_CN_LEN           48   /* 中文最大长度 */
#define VOCAB_PHONETIC_LEN     20   /* 音标最大长度 */
#define VOCAB_EXAMPLE_LEN      96   /* 例句最大长度 */
#define VOCAB_EXAMPLES_MAX      2   /* 每个词最大例句数 */
#define VOCAB_QUESTIONS_MAX     2   /* 每个词最大苏格拉底问题数 */
#define VOCAB_RELATED_MAX       4   /* 最大关联词数 */

/* ========== 数据结构 ========== */

/* 苏格拉底问题条目 */
typedef struct {
    char type[16];              /* 题型: en2cn/cn2en/fill/socratic */
    char prompt[128];           /* 问题 */
    char answer[64];            /* 答案 (开放式问题可为空) */
} VocabQuestion_t;

/* 单词条目 (运行时) */
typedef struct {
    char word[VOCAB_WORD_LEN];
    char phonetic[VOCAB_PHONETIC_LEN];
    char chinese[VOCAB_CN_LEN];
    char pos[8];                /* 词性: n/v/adj/adv/prep/... */
    char category[24];          /* 分类 */
    char level[8];              /* KET/PET/FCE */
    uint16_t frequency;         /* 词频 */
    uint8_t  section;           /* 节编号 (1~8), 0=未分节 */
    uint8_t  example_count;
    char examples[VOCAB_EXAMPLES_MAX][VOCAB_EXAMPLE_LEN];
    uint8_t  question_count;
    VocabQuestion_t questions[VOCAB_QUESTIONS_MAX];
    uint8_t  related_count;
    char related_words[VOCAB_RELATED_MAX][VOCAB_WORD_LEN];
} VocabWord_t;

/* 短语条目 */
typedef struct {
    char phrase[VOCAB_WORD_LEN];
    char chinese[VOCAB_CN_LEN];
    char level[8];              /* KET/PET/FCE */
    char scene[24];             /* 场景 */
} VocabPhrase_t;

/* 词库集合 */
typedef struct {
    VocabWord_t  words[VOCAB_MAX_WORD];
    uint16_t     word_count;
    VocabPhrase_t phrases[VOCAB_MAX_PHRASE];
    uint16_t     phrase_count;
    bool         loaded;
} VocabDatabase_t;

/* ========== API ========== */

/** 获取全局词库单例 */
VocabDatabase_t* Vocab_GetDB(void);

/**
 * 从 SD 卡 /sdcard/vocab/ 加载所有词库
 * 按 manifest.json → 各分级文件 → phrases.json 顺序加载
 * @return true 成功
 */
bool Vocab_LoadAll(void);

/**
 * 从指定章节/阶段获取词汇子集
 * @param level    "KET" / "PET" / "FCE" / NULL=所有
 * @param category "食物" / "动物" / NULL=所有
 * @param out      输出数组 (指向 VocabWord_t* 指针)
 * @param max      输出数组最大容量
 * @return 匹配词数
 */
int Vocab_GetWordsByFilter(const char *level, const char *category,
                           VocabWord_t **out, int max);

/**
 * 通过 ID 获取单词 (直接索引)
 */
const VocabWord_t* Vocab_GetWord(int index);

/**
 * 通过 ID 获取短语
 */
const VocabPhrase_t* Vocab_GetPhrase(int index);

/** 获取词库统计 */
int Vocab_GetWordCount(void);
int Vocab_GetPhraseCount(void);
bool Vocab_IsLoaded(void);

/**
 * 获取当前节的信息
 * @return 当前节编号 (1~8), 0=未设置
 */
int Vocab_GetCurrentSection(void);

/**
 * 切换到指定节
 * @param section 节编号 (1~8)
 */
void Vocab_SetSection(int section);

/**
 * 按节筛选获取单词
 * @param section 节编号 (1~8, 0=所有)
 * @param out     输出数组
 * @param max     输出数组最大容量
 * @return        匹配词数
 */
int Vocab_GetWordsBySection(int section, VocabWord_t **out, int max);

/*
 * 学习进度数据结构 (SM-2 算法用)
 */
typedef struct {
    uint16_t word_id;            /* 对应 VocabWord 索引 */
    uint8_t  total_reviews;
    uint8_t  correct_count;      /* 正确次数 (q >= 3) */
    uint8_t  consecutive_correct;
    uint8_t  error_count;
    uint16_t last_interval_h;    /* 上次间隔 (小时) */
    uint32_t next_review_time;   /* 下次复习时间戳 (Unix time) */
    float    ease_factor;        /* SM-2 EF值 */
    uint8_t  quality_last;       /* 上次质量评分 0-5 */
    uint8_t  level;              /* 当前学习阶段 0=未学, 1=学习中, 2=已掌握 */
} WordProgress_t;

/** 初始化学习进度 (全部重置) */
void Vocab_InitProgress(void);

/**
 * 获取/设置指定词的进度
 */
WordProgress_t* Vocab_GetProgress(uint16_t word_id);

#ifdef __cplusplus
}
#endif
