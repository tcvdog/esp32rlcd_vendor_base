/**
 * @file socrates_content.h
 * @brief 苏格拉底百科全书 — 内容加载与管理
 *
 * 从 SD 卡加载通识内容（K12 + 新闻 + 科技 + 记忆法），
 * 支持预设问题 + 实时生成，多个选项带苏格拉底式引导。
 *
 * 依赖: cJSON (ESP-IDF 内置)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 常量 ========== */
#define SOCRATES_MAX_QUESTIONS  10
#define SOCRATES_MAX_OPTIONS    4
#define SOCRATES_MAX_CATEGORIES 12
#define SOCRATES_MAX_FURTHER    4
#define SOCRATES_MAX_METHODS    16

#define SOCRATES_Q_TEXT_LEN    128
#define SOCRATES_HINT_LEN      96
#define SOCRATES_EXPL_LEN      128
#define SOCRATES_OPT_TEXT_LEN  96
#define SOCRATES_CAT_NAME_LEN  16
#define SOCRATES_CAT_ICON_LEN  4
#define SOCRATES_GRADE_LEN     8
#define SOCRATES_METHOD_NAME   24
#define SOCRATES_METHOD_STEPS  6
#define SOCRATES_STEP_TEXT_LEN 64

/* ========== 选项结构 ========== */
typedef struct {
    char text[SOCRATES_OPT_TEXT_LEN];   /* 选项文字 */
    bool correct;                        /* 是否正确 */
    char explanation[SOCRATES_EXPL_LEN]; /* 解释 */
} SocraticOption_t;

/* ========== 问题结构 ========== */
typedef struct {
    int   id;
    char  category[SOCRATES_CAT_NAME_LEN]; /* math/physics/chemistry/biology/history/geography/tech/news */
    char  grade[SOCRATES_GRADE_LEN];       /* G6 / G7 / G8 / G9 / G6+ / G8+ / G6+ */
    int   difficulty;                       /* 1=easy, 2=medium, 3=hard */
    char  question[SOCRATES_Q_TEXT_LEN];   /* 问题 */
    char  socratic_hint[SOCRATES_HINT_LEN]; /* 苏格拉底式提示 */
    SocraticOption_t options[SOCRATES_MAX_OPTIONS];
    int   option_count;
    char  further_questions[SOCRATES_MAX_FURTHER][SOCRATES_Q_TEXT_LEN];
    int   further_count;
} SocraticQuestion_t;

/* ========== 分类结构 ========== */
typedef struct {
    char id[SOCRATES_CAT_NAME_LEN];
    char name[SOCRATES_CAT_NAME_LEN];
    char icon[SOCRATES_CAT_ICON_LEN];
    int  question_count;   /* 该分类下的问题数 */
} SocraticCategory_t;

/* ========== 记忆/学习法结构 ========== */
typedef struct {
    char  id[SOCRATES_CAT_NAME_LEN];
    char  name[SOCRATES_METHOD_NAME];
    char  category[SOCRATES_CAT_NAME_LEN]; /* 理解/专注/记忆/笔记/好奇 */
    char  icon[SOCRATES_CAT_ICON_LEN];
    char  summary[SOCRATES_Q_TEXT_LEN];
    char  steps[SOCRATES_METHOD_STEPS][SOCRATES_STEP_TEXT_LEN];
    int   step_count;
    char  socratic_tip[SOCRATES_HINT_LEN];
    char  for_kids[SOCRATES_Q_TEXT_LEN];
} MemoryMethod_t;

/* ========== 内容数据库 ========== */
typedef struct {
    SocraticQuestion_t questions[SOCRATES_MAX_QUESTIONS];
    int                 question_count;
    SocraticCategory_t  categories[SOCRATES_MAX_CATEGORIES];
    int                 category_count;
    MemoryMethod_t      methods[SOCRATES_MAX_METHODS];
    int                 method_count;
    bool                loaded;
} SocratesDatabase_t;

/* ========== API ========== */

/** 获取全局数据库单例 */
SocratesDatabase_t* Socrates_GetDB(void);

/**
 * 从 SD 卡 /sdcard/vocab/socrates.json + memory_methods.json 加载
 * 失败则回退到内嵌数据
 */
bool Socrates_LoadAll(void);

/** 是否已加载 */
bool Socrates_IsLoaded(void);

/** 获取问题总数 */
int Socrates_GetQuestionCount(void);

/** 按索引获取问题 */
const SocraticQuestion_t* Socrates_GetQuestion(int index);

/** 按分类获取问题（输出指针数组） */
int Socrates_GetQuestionsByCategory(const char *category,
                                   const SocraticQuestion_t **out,
                                   int max_out);

/** 按难度获取问题 */
int Socrates_GetQuestionsByDifficulty(int difficulty,
                                    const SocraticQuestion_t **out,
                                    int max_out);

/** 随机获取一个问题 */
const SocraticQuestion_t* Socrates_GetRandomQuestion(void);

/** 随机获取某分类的问题 */
const SocraticQuestion_t* Socrates_GetRandomByCategory(const char *category);

/** 获取分类列表 */
int Socrates_GetCategories(SocraticCategory_t *out, int max_out);

/** 获取分类下问题数 */
int Socrates_GetCategoryQuestionCount(const char *category);

/** 记忆/学习法 API */
int  Socrates_GetMethodCount(void);
const MemoryMethod_t* Socrates_GetMethod(int index);
int  Socrates_GetMethodsByCategory(const char *category,
                                   const MemoryMethod_t **out,
                                   int max_out);

#ifdef __cplusplus
}
#endif
