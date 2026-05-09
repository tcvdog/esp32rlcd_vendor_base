/**
 * @file vocab_loader.cpp
 * @brief PET 分级词库 JSON 加载器实现 (基于 ArduinoJson)
 *
 * 加载流程:
 *   1. 读 /sdcard/vocab/manifest.json → 获取文件列表
 *   2. 按清单加载各分级 JSON → 解析为 VocabWord_t
 *   3. 加载 phrases.json → 解析为 VocabPhrase_t
 *   4. 初始化学习进度 (NVS 或 内存)
 *
 * 注意: 如果 SD 卡不可用，回退到内嵌的 word_data.h 词库
 */
#include "vocab_loader.h"
#include "sd_audio_player.h"   // 用于检查 SD 卡可用性
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cJSON.h>           /* ESP-IDF 内置 JSON 解析器 */

static const char *TAG = "Vocab";

/* ========== 全局词库单例 ========== */
static VocabDatabase_t s_db = {0};

/* ========== 内嵌回退词库 ========== */
#include "word_data.h"  // 现有的 word_data.h

/* 运行时生成苏格拉底问题 */
static void generate_socratic_questions(VocabWord_t *vw)
{
    int count = 0;
    vw->question_count = 2;

    /* 类型1: en2cn */
    snprintf(vw->questions[0].type, sizeof(vw->questions[0].type), "en2cn");
    snprintf(vw->questions[0].prompt, sizeof(vw->questions[0].prompt),
             "What does '%s' mean in Chinese?", vw->word);
    snprintf(vw->questions[0].answer, sizeof(vw->questions[0].answer),
             "%s", vw->chinese);

    /* 类型2: cn2en */
    snprintf(vw->questions[1].type, sizeof(vw->questions[1].type), "cn2en");
    snprintf(vw->questions[1].prompt, sizeof(vw->questions[1].prompt),
             "How do you say '%s' in English?", vw->chinese);
    snprintf(vw->questions[1].answer, sizeof(vw->questions[1].answer),
             "%s", vw->word);
}

/* ========== 学习进度 ========== */
static WordProgress_t s_progress[VOCAB_MAX_WORD] = {0};
static int s_progress_initialized = 0;

/* ========== 内部辅助 ========== */

/**
 * 读取 SD 卡文件到堆内存
 * 调用者需要 free() 返回值
 */
static char* read_file_to_heap(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open: %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    char *buf = (char*)malloc(size + 1);
    if (!buf) {
        ESP_LOGE(TAG, "OOM reading %s (%ld bytes)", path, size);
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, size, f);
    fclose(f);
    buf[read] = '\0';

    ESP_LOGI(TAG, "Read %s: %zu bytes", path, read);
    return buf;
}

/**
 * 加载单个词库 JSON 文件到数据库
 */
static bool load_vocab_json(const char *path, const char *level)
{
    char *json = read_file_to_heap(path);
    if (!json) return false;

    cJSON *root = cJSON_Parse(json);
    free(json);

    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed: %s", path);
        return false;
    }

    cJSON *words = cJSON_GetObjectItem(root, "words");
    if (!words || !cJSON_IsArray(words)) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "No 'words' array in %s", path);
        return false;
    }

    int count = cJSON_GetArraySize(words);
    ESP_LOGI(TAG, "Loading %d words from %s [%s]", count, path, level);

    for (int i = 0; i < count; i++) {
        cJSON *w = cJSON_GetArrayItem(words, i);
        if (!w || s_db.word_count >= VOCAB_MAX_WORD) break;

        VocabWord_t *vw = &s_db.words[s_db.word_count];

        /* 基础字段 */
        cJSON *item;

        item = cJSON_GetObjectItem(w, "word");
        strncpy(vw->word, item && item->valuestring ? item->valuestring : "?", VOCAB_WORD_LEN - 1);

        item = cJSON_GetObjectItem(w, "chinese");
        strncpy(vw->chinese, item && item->valuestring ? item->valuestring : "?", VOCAB_CN_LEN - 1);

        item = cJSON_GetObjectItem(w, "phonetic");
        strncpy(vw->phonetic, item && item->valuestring ? item->valuestring : "", VOCAB_PHONETIC_LEN - 1);

        item = cJSON_GetObjectItem(w, "pos");
        strncpy(vw->pos, item && item->valuestring ? item->valuestring : "", 7);

        item = cJSON_GetObjectItem(w, "category");
        strncpy(vw->category, item && item->valuestring ? item->valuestring : "", 23);

        strncpy(vw->level, level, 7);

        item = cJSON_GetObjectItem(w, "section");
        vw->section = item ? (uint8_t)item->valueint : 0;

        item = cJSON_GetObjectItem(w, "frequency");
        vw->frequency = item ? item->valueint : 500;

        /* 例句 */
        cJSON *examples = cJSON_GetObjectItem(w, "examples");
        if (examples && cJSON_IsArray(examples)) {
            int ec = cJSON_GetArraySize(examples);
            if (ec > VOCAB_EXAMPLES_MAX) ec = VOCAB_EXAMPLES_MAX;
            vw->example_count = ec;
            for (int j = 0; j < ec; j++) {
                cJSON *ex = cJSON_GetArrayItem(examples, j);
                if (ex && ex->valuestring) {
                    strncpy(vw->examples[j], ex->valuestring, VOCAB_EXAMPLE_LEN - 1);
                }
            }
        }

        /* 苏格拉底问题 */
        cJSON *questions = cJSON_GetObjectItem(w, "socratic_questions");
        if (questions && cJSON_IsArray(questions)) {
            int qc = cJSON_GetArraySize(questions);
            if (qc > VOCAB_QUESTIONS_MAX) qc = VOCAB_QUESTIONS_MAX;
            vw->question_count = qc;
            for (int j = 0; j < qc; j++) {
                cJSON *q = cJSON_GetArrayItem(questions, j);
                if (!q) continue;

                cJSON *qt = cJSON_GetObjectItem(q, "type");
                if (qt && qt->valuestring)
                    strncpy(vw->questions[j].type, qt->valuestring, 15);

                cJSON *qp = cJSON_GetObjectItem(q, "prompt");
                if (qp && qp->valuestring)
                    strncpy(vw->questions[j].prompt, qp->valuestring, 127);

                cJSON *qa = cJSON_GetObjectItem(q, "answer");
                if (qa && qa->valuestring)
                    strncpy(vw->questions[j].answer, qa->valuestring, 63);
            }
        }

        /* 关联词 */
        cJSON *related = cJSON_GetObjectItem(w, "related_words");
        if (related && cJSON_IsArray(related)) {
            int rc = cJSON_GetArraySize(related);
            if (rc > VOCAB_RELATED_MAX) rc = VOCAB_RELATED_MAX;
            vw->related_count = rc;
            for (int j = 0; j < rc; j++) {
                cJSON *rw = cJSON_GetArrayItem(related, j);
                if (rw && rw->valuestring) {
                    strncpy(vw->related_words[j], rw->valuestring, VOCAB_WORD_LEN - 1);
                }
            }
        }

        /* 运行时生成苏格拉底问题 (JSON 中不再嵌入) */
        generate_socratic_questions(vw);

        s_db.word_count++;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %d words, total=%d", count, s_db.word_count);
    return true;
}

/**
 * 加载短语音频 JSON 文件
 */
static bool load_phrases_json(const char *path)
{
    char *json = read_file_to_heap(path);
    if (!json) return false;

    cJSON *root = cJSON_Parse(json);
    free(json);

    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed: %s", path);
        return false;
    }

    cJSON *phrases = cJSON_GetObjectItem(root, "phrases");
    if (!phrases || !cJSON_IsArray(phrases)) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "No 'phrases' array in %s", path);
        return false;
    }

    int count = cJSON_GetArraySize(phrases);
    ESP_LOGI(TAG, "Loading %d phrases", count);

    for (int i = 0; i < count; i++) {
        cJSON *p = cJSON_GetArrayItem(phrases, i);
        if (!p || s_db.phrase_count >= VOCAB_MAX_PHRASE) break;

        VocabPhrase_t *vp = &s_db.phrases[s_db.phrase_count];
        cJSON *item;

        item = cJSON_GetObjectItem(p, "phrase");
        strncpy(vp->phrase, item && item->valuestring ? item->valuestring : "?", VOCAB_WORD_LEN - 1);

        item = cJSON_GetObjectItem(p, "chinese");
        strncpy(vp->chinese, item && item->valuestring ? item->valuestring : "?", VOCAB_CN_LEN - 1);

        item = cJSON_GetObjectItem(p, "level");
        strncpy(vp->level, item && item->valuestring ? item->valuestring : "KET", 7);

        item = cJSON_GetObjectItem(p, "scene");
        strncpy(vp->scene, item && item->valuestring ? item->valuestring : "", 23);

        s_db.phrase_count++;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %d phrases", count);
    return true;
}

/* 加载内嵌回退词库 */
static void load_embedded_fallback(void)
{
    for (int i = 0; i < WORD_COUNT && i < VOCAB_MAX_WORD; i++) {
        const WordEntry_t *we = WordData_GetWord(i);
        if (!we) break;
        VocabWord_t *vw = &s_db.words[i];
        strncpy(vw->word, we->english, VOCAB_WORD_LEN - 1);
        strncpy(vw->chinese, we->chinese, VOCAB_CN_LEN - 1);
        strncpy(vw->phonetic, we->phonetic ? we->phonetic : "", VOCAB_PHONETIC_LEN - 1);
        strncpy(vw->category, we->category ? we->category : "", 23);
        strncpy(vw->level, "KET", 7);
        vw->frequency = 500;
        vw->section = (i / 15) + 1;
        generate_socratic_questions(vw);
        s_db.word_count++;
    }
    for (int i = 0; i < PHRASE_COUNT && i < VOCAB_MAX_PHRASE; i++) {
        const PhraseEntry_t *pe = WordData_GetPhrase(i);
        if (!pe) break;
        VocabPhrase_t *vp = &s_db.phrases[i];
        strncpy(vp->phrase, pe->phrase, VOCAB_WORD_LEN - 1);
        strncpy(vp->chinese, pe->chinese, VOCAB_CN_LEN - 1);
        strncpy(vp->level, pe->level ? pe->level : "KET", 7);
        s_db.phrase_count++;
    }
    ESP_LOGI(TAG, "Fallback loaded: %d words, %d phrases", s_db.word_count, s_db.phrase_count);
    s_db.loaded = true;
    Vocab_InitProgress();
}

/* ========== API 实现 ========== */

VocabDatabase_t* Vocab_GetDB(void)
{
    return &s_db;
}

bool Vocab_LoadAll(void)
{
    if (s_db.loaded) return true;

    memset(&s_db, 0, sizeof(s_db));

    if (!SdAudio_IsAvailable()) {
        ESP_LOGW(TAG, "SD card not available, falling back to embedded word data");
        load_embedded_fallback();
        return true;
    }

    /* ========== SD 卡加载 ========== */

    /* 1. 读 manifest */
    char *manifest = read_file_to_heap("/sdcard/vocab/manifest.json");
    if (!manifest) {
        ESP_LOGE(TAG, "No manifest.json found on SD, loading embedded data");
        /* 加载内嵌词库作为回退 */
        load_embedded_fallback();
        return true;
    }

    cJSON *manifest_root = cJSON_Parse(manifest);
    free(manifest);

    if (!manifest_root) {
        ESP_LOGE(TAG, "manifest.json parse failed, loading embedded data");
        load_embedded_fallback();
        return true;
    }

    /* 2. 加载各节文件 (每次只读一个 ~7KB 文件) */
    cJSON *files = cJSON_GetObjectItem(manifest_root, "files");
    if (files && cJSON_IsArray(files)) {
        int n_files = cJSON_GetArraySize(files);
        for (int i = 0; i < n_files && s_db.word_count < VOCAB_MAX_WORD; i++) {
            cJSON *f = cJSON_GetArrayItem(files, i);
            if (!f) continue;
            cJSON *fn = cJSON_GetObjectItem(f, "file");
            if (!fn || !fn->valuestring) continue;
            char path[48];
            snprintf(path, sizeof(path), "/sdcard/vocab/%s", fn->valuestring);
            if (load_vocab_json(path, "PET")) {
                ESP_LOGI(TAG, "Loaded section: %s", fn->valuestring);
            } else {
                ESP_LOGW(TAG, "Failed to load section: %s", fn->valuestring);
            }
        }
    } else {
        ESP_LOGW(TAG, "No 'files' array in manifest.json");
    }

    /* 3. 加载短语 (处理文件不存在的情况) */
    cJSON *phrase_files = cJSON_GetObjectItem(manifest_root, "phrase_files");
    bool phrases_loaded = false;
    if (phrase_files && cJSON_IsArray(phrase_files)) {
        cJSON *pf0 = cJSON_GetArrayItem(phrase_files, 0);
        if (pf0) {
            cJSON *pfn = cJSON_GetObjectItem(pf0, "file");
            if (pfn && pfn->valuestring) {
                char path[48];
                snprintf(path, sizeof(path), "/sdcard/vocab/%s", pfn->valuestring);
                phrases_loaded = load_phrases_json(path);
            }
        }
    }
    if (!phrases_loaded) {
        load_phrases_json("/sdcard/vocab/phrases.json");  // 旧路径兼容
    }

    cJSON_Delete(manifest_root);

    /* 4. 如果 SD 卡没有成功加载任何单词，回退到内嵌词库 */
    if (s_db.word_count == 0) {
        ESP_LOGW(TAG, "SD card had no loadable words, falling back to embedded");
        /* 保留已从 SD 加载的短语 */
        uint16_t saved_phrases = s_db.phrase_count;
        load_embedded_fallback();
        if (saved_phrases > s_db.phrase_count) {
            s_db.phrase_count = saved_phrases;
        }
        return true;
    }

    s_db.loaded = true;
    Vocab_InitProgress();

    ESP_LOGI(TAG, "SD vocab loaded: %d words, %d phrases",
             s_db.word_count, s_db.phrase_count);
    return true;
}

int Vocab_GetWordsByFilter(const char *level, const char *category,
                           VocabWord_t **out, int max)
{
    int count = 0;
    for (int i = 0; i < s_db.word_count && count < max; i++) {
        bool match = true;
        if (level && strcmp(s_db.words[i].level, level) != 0) match = false;
        if (category && strcmp(s_db.words[i].category, category) != 0) match = false;
        if (match) out[count++] = &s_db.words[i];
    }
    return count;
}

const VocabWord_t* Vocab_GetWord(int index)
{
    if (index < 0 || index >= s_db.word_count) return NULL;
    return &s_db.words[index];
}

const VocabPhrase_t* Vocab_GetPhrase(int index)
{
    if (index < 0 || index >= s_db.phrase_count) return NULL;
    return &s_db.phrases[index];
}

int Vocab_GetWordCount(void)    { return s_db.word_count; }
int Vocab_GetPhraseCount(void)  { return s_db.phrase_count; }
bool Vocab_IsLoaded(void)       { return s_db.loaded; }

/* ========== 学习进度管理 ========== */

void Vocab_InitProgress(void)
{
    memset(s_progress, 0, sizeof(s_progress));
    for (int i = 0; i < VOCAB_MAX_WORD; i++) {
        s_progress[i].word_id = i;
        s_progress[i].ease_factor = 2.5f;
        s_progress[i].last_interval_h = 1;
        s_progress[i].next_review_time = 0;
        s_progress[i].quality_last = 0;
        s_progress[i].level = 0;
    }
    s_progress_initialized = 1;
    ESP_LOGI(TAG, "Progress initialized for %d words", s_db.word_count);
}

WordProgress_t* Vocab_GetProgress(uint16_t word_id)
{
    if (!s_progress_initialized) return NULL;
    if (word_id >= VOCAB_MAX_WORD) return NULL;
    return &s_progress[word_id];
}

/* ========== 节 (Section) 管理 ========== */
static int s_current_section = 1;   /* 默认第1节 */

int Vocab_GetCurrentSection(void)
{
    return s_current_section;
}

void Vocab_SetSection(int section)
{
    if (section < 1) section = 1;
    if (section > 8) section = 8;
    s_current_section = section;
    ESP_LOGI(TAG, "Switched to section %d", section);
}

int Vocab_GetWordsBySection(int section, VocabWord_t **out, int max)
{
    if (!out || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < s_db.word_count && count < max; i++) {
        /* JSON词库有 section 字段; 嵌入式词库没有, 按每节15词计算 */
        int word_section = s_db.words[i].section;
        if (word_section == 0) {
            word_section = (i / 15) + 1;  /* 嵌入式词库按位置分节 */
        }
        if (section == 0 || word_section == section) {
            out[count++] = &s_db.words[i];
        }
    }
    return count;
}
