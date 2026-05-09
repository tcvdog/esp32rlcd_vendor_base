/**
 * @file sm2_scheduler.cpp
 * @brief SM-2 间隔重复调度器实现
 *
 * 基于 SuperMemo SM-2 算法 + ESP32 时间管理。
 * 使用 Unix 时间戳 (秒) 进行调度，兼容 NVS 持��化。
 */
#include "sm2_scheduler.h"
#include "vocab_loader.h"
#include <esp_log.h>
#include <string.h>
#include <math.h>

static const char *TAG = "SM2";

/* ========== 最大复习队列容量 ========== */
#define SM2_MAX_ITEMS   VOCAB_MAX_WORD
#define SM2_NEW_PER_DAY 5       /* 每天新词上限 */

/* ========== 全局调度状态 ========== */
static Sm2Item_t s_items[SM2_MAX_ITEMS];
static int s_item_count = 0;
static int s_initialized = 0;

/* 每日统计数据 */
static int s_today_new = 0;
static int s_today_review = 0;
static int s_today_correct = 0;
static int s_today_total = 0;

void Sm2_Init(void)
{
    VocabDatabase_t *db = Vocab_GetDB();
    int count = db->word_count;
    if (count > SM2_MAX_ITEMS) count = SM2_MAX_ITEMS;

    memset(s_items, 0, sizeof(s_items));
    s_item_count = count;

    for (int i = 0; i < count; i++) {
        s_items[i].word_id = i;
        s_items[i].ease_factor = 2.5f;
        s_items[i].interval_days = 1;
        s_items[i].next_review = 0;      /* 立即可以学习 */
        s_items[i].level = SM2_LEVEL_NEW;
    }

    s_initialized = 1;
    s_today_new = 0;
    s_today_review = 0;
    s_today_correct = 0;
    s_today_total = 0;

    ESP_LOGI(TAG, "SM-2 scheduler initialized: %d items", count);
}

void Sm2_RecordReview(Sm2Item_t *item, uint8_t quality, uint32_t now)
{
    if (!item || quality > 5) return;

    item->total_reviews++;
    item->quality_last = quality;
    item->last_review = now;

    /* 更新统计 */
    s_today_total++;
    if (quality >= SM2_Q_HARD_CORRECT) {
        item->correct_count++;
        s_today_correct++;
    } else {
        item->error_count++;
    }

    /* SM-2 核心算法 */
    float new_ef = item->ease_factor
        + (0.1f - (5 - quality) * (0.08f + (5 - quality) * 0.02f));

    if (new_ef < 1.3f) new_ef = 1.3f;
    item->ease_factor = new_ef;

    if (quality >= SM2_Q_HARD_CORRECT) {
        /* 回答正确 */
        item->repetition++;
        item->consecutive_correct++;

        switch (item->repetition) {
            case 1: item->interval_days = 1;   break;
            case 2: item->interval_days = 6;   break;
            default:
                item->interval_days = (uint16_t)roundf(
                    item->interval_days * item->ease_factor);
                break;
        }

        /* 最长间隔 */
        if (item->interval_days > 365) item->interval_days = 365;

        /* 更新学习阶段 */
        if (item->repetition < 2) {
            item->level = SM2_LEVEL_LEARNING;
        } else {
            item->level = SM2_LEVEL_REVIEWING;
        }
        if (item->interval_days >= 30) {
            item->level = SM2_LEVEL_MASTERED;
        }
    } else {
        /* 回答错误 — 重置 */
        item->repetition = 0;
        item->interval_days = 1;         /* 1天后重试 */
        item->level = SM2_LEVEL_LEARNING;
    }

    /* 计算下次复习时间 */
    item->next_review = now + (uint32_t)item->interval_days * 86400;

    ESP_LOGD(TAG, "Review word[%d]: q=%d, EF=%.2f, interval=%dd, level=%d",
             item->word_id, quality, item->ease_factor,
             item->interval_days, item->level);
}

int Sm2_GetDueItems(Sm2Item_t *items[], int max, uint32_t now)
{
    if (!s_initialized) return 0;

    int count = 0;
    for (int i = 0; i < s_item_count && count < max; i++) {
        /* 已学的词，且到期或超期 */
        if (s_items[i].level != SM2_LEVEL_NEW &&
            s_items[i].next_review <= now) {
            items[count++] = &s_items[i];
        }
    }

    /* 按超期程度排序 (超期最久的排前面) */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (items[i]->next_review > items[j]->next_review) {
                Sm2Item_t *tmp = items[i];
                items[i] = items[j];
                items[j] = tmp;
            }
        }
    }

    return count;
}

int Sm2_GetWeakItems(Sm2Item_t *items[], int max)
{
    if (!s_initialized) return 0;

    int count = 0;
    for (int i = 0; i < s_item_count && count < max; i++) {
        /* 已学且错误率 > 30% */
        if (s_items[i].total_reviews > 0) {
            float error_rate = (float)s_items[i].error_count /
                               (float)s_items[i].total_reviews;
            if (error_rate > 0.3f) {
                items[count++] = &s_items[i];
            }
        }
    }

    /* 按错误率降序 */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            float er_i = (float)items[i]->error_count / items[i]->total_reviews;
            float er_j = (float)items[j]->error_count / items[j]->total_reviews;
            if (er_i < er_j) {
                Sm2Item_t *tmp = items[i];
                items[i] = items[j];
                items[j] = tmp;
            }
        }
    }

    return count;
}

int Sm2_GetNewItems(Sm2Item_t *items[], int max,
                    const WordProgress_t *progress, int total)
{
    if (!s_initialized) return 0;

    /* 今天已经学够了新词 */
    if (s_today_new >= SM2_NEW_PER_DAY) return 0;

    int actual_max = max;
    if (actual_max > SM2_NEW_PER_DAY - s_today_new) {
        actual_max = SM2_NEW_PER_DAY - s_today_new;
    }

    int count = 0;
    /* 找 level=NEW 的词 */
    for (int i = 0; i < s_item_count && count < actual_max; i++) {
        if (s_items[i].level == SM2_LEVEL_NEW) {
            items[count++] = &s_items[i];
        }
    }

    /* 按 frequency 降序 (高频优先) */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (items[i]->word_id > items[j]->word_id) {
                Sm2Item_t *tmp = items[i];
                items[i] = items[j];
                items[j] = tmp;
            }
        }
    }

    return count;
}

int Sm2_GetTotalLearned(void)
{
    int count = 0;
    for (int i = 0; i < s_item_count; i++) {
        if (s_items[i].level != SM2_LEVEL_NEW) count++;
    }
    return count;
}

int Sm2_GetTotalDue(void)
{
    return s_today_review;
}

int Sm2_GetTotalMastered(void)
{
    int count = 0;
    for (int i = 0; i < s_item_count; i++) {
        if (s_items[i].level == SM2_LEVEL_MASTERED) count++;
    }
    return count;
}

float Sm2_GetAccuracy(void)
{
    if (s_today_total == 0) return 0;
    return (float)s_today_correct / (float)s_today_total * 100.0f;
}
