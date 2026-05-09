#pragma once

/**
 * @file word_data.h
 * @brief 预置单词和短语词库数据 + 运行时数据结构
 */

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 数据结构 ========== */

/* 单词条目 */
typedef struct {
    const char *english;
    const char *chinese;
    const char *phonetic;   // 音标
    const char *category;   // 分类 (如: 水果, 动物, 日常)
} WordEntry_t;

/* 短语条目 */
typedef struct {
    const char *phrase;
    const char *chinese;
    const char *example;    // 例句
    const char *level;      // 难度: 初级/中级/高级
} PhraseEntry_t;

/* 学习状态标记 */
typedef enum {
    WORD_STATUS_NEW = 0,        // 新词, 未学习
    WORD_STATUS_LEARNING,       // 正在学习
    WORD_STATUS_KNOWN,          // 已掌握
    WORD_STATUS_FAVORITE,       // 收藏
} WordStatus_t;

/* ========== 常量定义 ========== */

#define WORD_COUNT      50
#define PHRASE_COUNT    20

/* ========== 词库数据 ========== */

static const WordEntry_t g_word_list[WORD_COUNT] = {
    /* ---- 初级 (基础) ---- */
    {"apple",       "苹果",     "/ˈæp.əl/",      "水果"},
    {"banana",      "香蕉",     "/bəˈnɑː.nə/",   "水果"},
    {"orange",      "橙子",     "/ˈɒr.ɪndʒ/",    "水果"},
    {"water",       "水",       "/ˈwɔː.tər/",    "饮食"},
    {"bread",       "面包",     "/bred/",         "饮食"},
    {"milk",        "牛奶",     "/mɪlk/",         "饮食"},
    {"cat",         "猫",       "/kæt/",          "动物"},
    {"dog",         "狗",       "/dɒɡ/",          "动物"},
    {"bird",        "鸟",       "/bɜːd/",         "动物"},
    {"fish",        "鱼",       "/fɪʃ/",          "动物"},

    /* ---- 初级 (日常) ---- */
    {"book",        "书",       "/bʊk/",          "学习"},
    {"pen",         "钢笔",     "/pen/",           "学习"},
    {"school",      "学校",     "/skuːl/",         "场所"},
    {"teacher",     "老师",     "/ˈtiː.tʃər/",    "人物"},
    {"student",     "学生",     "/ˈstjuː.dənt/",  "人物"},
    {"friend",      "朋友",     "/frend/",         "社会"},
    {"family",      "家庭",     "/ˈfæm.əl.i/",    "社会"},
    {"house",       "房子",     "/haʊs/",          "场所"},
    {"car",         "汽车",     "/kɑːr/",          "交通"},
    {"bus",         "公交",     "/bʌs/",           "交通"},

    /* ---- 初级 (形容词/动词) ---- */
    {"happy",       "快乐",     "/ˈhæp.i/",       "情感"},
    {"sad",         "悲伤",     "/sæd/",           "情感"},
    {"big",         "大的",     "/bɪɡ/",           "描述"},
    {"small",       "小的",     "/smɔːl/",         "描述"},
    {"hot",         "热的",     "/hɒt/",           "描述"},
    {"cold",        "冷的",     "/kəʊld/",         "描述"},
    {"run",         "跑",       "/rʌn/",           "动作"},
    {"walk",        "走",       "/wɔːk/",          "动作"},
    {"eat",         "吃",       "/iːt/",           "动作"},
    {"drink",       "喝",       "/drɪŋk/",         "动作"},

    /* ---- 中级 ---- */
    {"computer",    "电脑",     "/kəmˈpjuː.tər/", "科技"},
    {"internet",    "互联网",   "/ˈɪn.tə.net/",    "科技"},
    {"weather",     "天气",     "/ˈweð.ər/",       "自然"},
    {"mountain",    "山",       "/ˈmaʊn.tɪn/",    "自然"},
    {"river",       "河流",     "/ˈrɪv.ər/",       "自然"},
    {"forest",      "森林",     "/ˈfɒr.ɪst/",     "自然"},
    {"morning",     "早上",     "/ˈmɔː.nɪŋ/",     "时间"},
    {"evening",     "晚上",     "/ˈiːv.nɪŋ/",     "时间"},
    {"holiday",     "假期",     "/ˈhɒl.ɪ.deɪ/",   "生活"},
    {"breakfast",   "早餐",     "/ˈbrek.fəst/",   "饮食"},

    /* ---- 中级 (继续) ---- */
    {"important",   "重要的",   "/ɪmˈpɔː.tənt/",  "描述"},
    {"beautiful",   "美丽的",   "/ˈbjuː.tɪ.fəl/", "描述"},
    {"dangerous",   "危险的",   "/ˈdeɪn.dʒər.əs/","描述"},
    {"different",   "不同的",   "/ˈdɪf.ər.ənt/",  "描述"},
    {"difficult",   "困难的",   "/ˈdɪf.ɪ.kəlt/",  "描述"},
    {"wonderful",   "精彩的",   "/ˈwʌn.də.fəl/",  "情感"},
    {"remember",    "记住",     "/rɪˈmem.bər/",   "动作"},
    {"understand",  "理解",     "/ˌʌn.dəˈstænd/", "动作"},
    {"practice",    "练习",     "/ˈpræk.tɪs/",    "学习"},
    {"knowledge",   "知识",     "/ˈnɒl.ɪdʒ/",    "学习"},
};

/* ========== 短语库 ========== */

static const PhraseEntry_t g_phrase_list[PHRASE_COUNT] = {
    /* 日常对话 */
    {"How are you?",            "你好吗？",              "How are you today? I'm fine, thanks.",            "初级"},
    {"Nice to meet you.",       "很高兴认识你。",        "Nice to meet you, my name is Tom.",               "初级"},
    {"Thank you very much.",    "非常感谢你。",          "Thank you very much for your help.",              "初级"},
    {"You're welcome.",         "不客气。",              "You're welcome! Happy to help.",                  "初级"},
    {"Excuse me.",              "打扰一下/对不起。",     "Excuse me, where is the bus station?",            "初级"},

    /* 日常交流 */
    {"I don't understand.",     "我不明白。",            "I don't understand. Can you explain again?",      "初级"},
    {"Could you repeat that?",  "你能重复一下吗？",      "Could you repeat that? I didn't catch it.",       "初级"},
    {"What time is it?",        "几点了？",              "What time is it? I need to go to class.",         "初级"},
    {"How much is this?",       "这个多少钱？",          "How much is this? I'd like to buy it.",           "初级"},
    {"I'd like to...",          "我想要...",             "I'd like to order a cup of coffee.",              "初级"},

    /* 中级 */
    {"What do you think?",      "你怎么看？",            "What do you think about this plan?",              "中级"},
    {"That makes sense.",       "有道理。",              "That makes sense. I understand now.",             "中级"},
    {"It depends on...",        "这取决于...",           "It depends on the weather whether we go out.",    "中级"},
    {"I'm looking forward to",  "我期待着...",           "I'm looking forward to meeting you.",             "中级"},
    {"Take your time.",         "慢慢来，别着急。",      "Don't rush. Take your time to finish the work.",  "中级"},

    /* 中高级 */
    {"Let's keep in touch.",    "保持联系。",            "Let's keep in touch after graduation.",           "中级"},
    {"I'm afraid I can't.",     "恐怕我不能。",          "I'm afraid I can't come to the party tonight.",   "中级"},
    {"It's up to you.",         "由你决定。",            "Where should we eat? It's up to you.",            "中级"},
    {"That's a good idea.",     "好主意。",              "That's a good idea! Let's do it.",                "初级"},
    {"Practice makes perfect.", "熟能生巧。",            "Don't give up. Practice makes perfect.",          "高级"},
};

/* ========== 辅助函数 ========== */

/* 获取单词总数 */
static inline int WordData_GetWordCount(void) { return WORD_COUNT; }

/* 获取短语总数 */
static inline int WordData_GetPhraseCount(void) { return PHRASE_COUNT; }

/* 获取指定索引的单词 (空安全检查) */
static inline const WordEntry_t* WordData_GetWord(int index)
{
    if (index < 0 || index >= WORD_COUNT) return NULL;
    return &g_word_list[index];
}

/* 获取指定索引的短语 */
static inline const PhraseEntry_t* WordData_GetPhrase(int index)
{
    if (index < 0 || index >= PHRASE_COUNT) return NULL;
    return &g_phrase_list[index];
}

#ifdef __cplusplus
}
#endif
