/**
 * @file sd_setup.cpp
 * @brief 将嵌入的 audio_data.h 音频复制到 SD 卡
 * 
 * 通过 TtsPlayer 访问函数获取音频数据，避免 audio_data.h 重复编译。
 */
#include "sd_setup.h"
#include "tts_player.h"
#include "sd_audio_player.h"
#include <esp_log.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "SdSetup";

static const char *WORD_NAMES[] = {
    "apple","banana","orange","water","bread","milk","cat","dog","bird","fish",
    "book","pen","school","teacher","student","friend","family","house","car","bus",
    "happy","sad","big","small","hot","cold","run","walk","eat","drink",
    "computer","internet","weather","mountain","river","forest","morning","evening",
    "holiday","breakfast","important","beautiful","dangerous","different","difficult",
    "wonderful","remember","understand","practice","knowledge"
};

bool SdSetup_CopyAudioToSD(void)
{
    if (!SdAudio_IsAvailable()) {
        ESP_LOGW(TAG, "SD card not available, skipping audio copy");
        return false;
    }

    const char *audio_dir = "/sdcard/audio";
    int word_count = TtsPlayer_GetWordCount();
    int phrase_count = TtsPlayer_GetPhraseCount();
    int total_needed = word_count + phrase_count;

    /* 检查音频目录是否已存在且有完整数据 */
    DIR *dir = opendir(audio_dir);
    if (dir) {
        int file_count = 0;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strstr(entry->d_name, ".ulaw")) file_count++;
        }
        closedir(dir);
        if (file_count >= total_needed) {
            ESP_LOGI(TAG, "SD audio already initialized (%d files found)", file_count);
            return true;
        }
        ESP_LOGI(TAG, "Found %d audio files, need %d, re-initializing...",
                 file_count, total_needed);
    } else {
        if (mkdir(audio_dir, 0755) != 0 && errno != EEXIST) {
            ESP_LOGE(TAG, "Failed to create %s (errno=%d)", audio_dir, errno);
            return false;
        }
        ESP_LOGI(TAG, "Created %s", audio_dir);
    }

    /* 写入单词音频 */
    int written = 0;
    ESP_LOGI(TAG, "Writing %d word audio files to SD...", word_count);

    for (int i = 0; i < word_count; i++) {
        const uint8_t *data = TtsPlayer_GetWordData(i);
        uint32_t size = TtsPlayer_GetWordSize(i);
        if (!data || size == 0) {
            ESP_LOGW(TAG, "  [%02d] %s — no data", i,
                     i < (int)(sizeof(WORD_NAMES)/sizeof(WORD_NAMES[0])) ? WORD_NAMES[i] : "?");
            continue;
        }

        char path[64];
        snprintf(path, sizeof(path), "%s/word_%02d.ulaw", audio_dir, i);

        FILE *f = fopen(path, "wb");
        if (!f) {
            ESP_LOGE(TAG, "  [%02d] Failed to open %s", i, path);
            continue;
        }
        size_t wb = fwrite(data, 1, size, f);
        fclose(f);

        if (wb == size) {
            ESP_LOGI(TAG, "  [%02d] %-15s → %s (%u bytes)", i,
                     i < (int)(sizeof(WORD_NAMES)/sizeof(WORD_NAMES[0])) ? WORD_NAMES[i] : "?",
                     path, size);
            written++;
        } else {
            ESP_LOGE(TAG, "  [%02d] Write failed (%zu/%u)", i, wb, size);
        }
    }

    /* 写入短语音频 */
    ESP_LOGI(TAG, "Writing %d phrase audio files to SD...", phrase_count);

    for (int i = 0; i < phrase_count; i++) {
        const uint8_t *data = TtsPlayer_GetPhraseData(i);
        uint32_t size = TtsPlayer_GetPhraseSize(i);
        if (!data || size == 0) {
            ESP_LOGW(TAG, "  [%02d] phrase — no data", i);
            continue;
        }

        char path[64];
        snprintf(path, sizeof(path), "%s/phrase_%02d.ulaw", audio_dir, i);

        FILE *f = fopen(path, "wb");
        if (!f) {
            ESP_LOGE(TAG, "  [%02d] Failed to open %s", i, path);
            continue;
        }
        size_t wb = fwrite(data, 1, size, f);
        fclose(f);

        if (wb == size) {
            ESP_LOGI(TAG, "  [%02d] phrase → %s (%u bytes)", i, path, size);
            written++;
        } else {
            ESP_LOGE(TAG, "  [%02d] Write failed (%zu/%u)", i, wb, size);
        }
    }

    ESP_LOGI(TAG, "SD audio setup complete: %d/%d files written",
             written, total_needed);
    return (written > 0);
}
