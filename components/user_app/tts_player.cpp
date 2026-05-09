// SPDX-License-Identifier: MIT
// tts_player.cpp - TTS 发音播放管理层
// 基于 audio_bsp (Audio_*) + audio_data.h 嵌入音频
#include "tts_player.h"
/* 嵌入音频数据 (仅在无 SD 卡时编译, 通过 EMBED_AUDIO 控制) */
#ifdef CONFIG_EMBED_AUDIO
#include "audio_data.h"
#else
/* 无嵌入音频时的回退定义 */
#define AUDIO_WORD_COUNT   0
#define AUDIO_PHRASE_COUNT 0
typedef struct { const uint8_t *data; uint32_t size; } AudioClip_t;
#define AUDIO_GET_WORD(idx)        ((void)(idx), (const AudioClip_t*)NULL)
#define AUDIO_GET_PHRASE(idx)      ((void)(idx), (const AudioClip_t*)NULL)
#endif
#include "sd_audio_player.h"
#include "audio_bsp.h"
#include <esp_log.h>
#include <cstdio>

static const char *TAG_TTS = "TTS";

/* ========== 内部状态 ========== */
typedef struct {
    bool            initialized;
    bool            playing;
    PlayType_t      playType;
    int             clipIndex;        // 当前播放的 clip 索引
    const uint8_t  *data;             // 当前音频数据指针
    uint32_t        totalSize;        // 总大小 (bytes)
    uint32_t        readPos;          // 已读取位置 (bytes)
    TtsPlayer_Callback_t callback;    // 播放完成回调
} TtsPlayer_State_t;

static TtsPlayer_State_t s_state = {0};

/* ========== 内部函数 ========== */

static void reset_state(void)
{
    s_state.playing    = false;
    s_state.playType   = PLAY_TYPE_WORD;
    s_state.clipIndex  = -1;
    s_state.data       = NULL;
    s_state.totalSize  = 0;
    s_state.readPos    = 0;
}

/* ========== 公开 API ========== */

bool TtsPlayer_Init(TtsPlayer_Callback_t callback)
{
    // Audio_Init 已在 lvgl_demo.cpp 的 setup() 中调用，此处不再重复调用
    // 避免 I2S 驱动重复安装导致失败

    reset_state();
    s_state.initialized = true;
    s_state.callback    = callback;

    return true;
}

void TtsPlayer_SetCallback(TtsPlayer_Callback_t callback)
{
    s_state.callback = callback;
}

/**
 * 内部：流式播放 (非阻塞)
 * 每次调用 Audio_PlayULaw(ptr, size) 来逐步送数据
 * 但 Audio_PlayULaw 是阻塞的，所以改为分块播放
 * 
 * 实际策略：第一次调用 Audio_PlayULaw 发全部，但检查播放状态
 * 如果正在播放就不重复发送
 */
static bool play_clip(const AudioClip_t *clip, PlayType_t type, int index)
{
    if (!clip || clip->data == NULL || clip->size == 0)
        return false;

    s_state.playing    = true;
    s_state.playType   = type;
    s_state.clipIndex  = index;
    s_state.data       = clip->data;
    s_state.totalSize  = clip->size;
    s_state.readPos    = 0;

    // 使用非阻塞 DMA 后台播放
    Audio_StartULaw(clip->data, clip->size);
    return true;
}

bool TtsPlayer_PlayWord(int wordIndex)
{
    if (!s_state.initialized) {
        ESP_LOGI(TAG_TTS, "TTS not initialized!");
        return false;
    }

    ESP_LOGI(TAG_TTS, "PlayWord index=%d", wordIndex);

    /* 如果正在播放，先停止 */
    if (Audio_IsPlaying()) {
        Audio_Stop();
    }

    /* 尝试从 SD 卡播放 */
    if (SdAudio_IsAvailable()) {
        char path[64];
        snprintf(path, sizeof(path), "/sdcard/audio/word_%02d.ulaw", wordIndex);
        if (SdAudio_PlayFile(path)) {
            return true;
        }
        ESP_LOGD(TAG_TTS, "SD file not found: %s, falling back to embedded", path);
    }

    /* 回退到嵌入数据 */
    const AudioClip_t *clip = AUDIO_GET_WORD(wordIndex);
    return play_clip(clip, PLAY_TYPE_WORD, wordIndex);
}

bool TtsPlayer_PlayPhrase(int phraseIndex)
{
    if (!s_state.initialized)
        return false;

    if (Audio_IsPlaying()) {
        Audio_Stop();
    }

    /* 尝试从 SD 卡播放 */
    if (SdAudio_IsAvailable()) {
        char path[64];
        snprintf(path, sizeof(path), "/sdcard/audio/phrase_%02d.ulaw", phraseIndex);
        if (SdAudio_PlayFile(path)) {
            return true;
        }
    }

    const AudioClip_t *clip = AUDIO_GET_PHRASE(phraseIndex);
    return play_clip(clip, PLAY_TYPE_PHRASE, phraseIndex);
}

void TtsPlayer_Stop(void)
{
    Audio_Stop();
    reset_state();
}

bool TtsPlayer_IsPlaying(void)
{
    if (!s_state.initialized)
        return false;
    return Audio_IsPlaying();
}

uint32_t TtsPlayer_GetPosition(void)
{
    return s_state.readPos;
}

uint32_t TtsPlayer_GetTotalSize(void)
{
    return s_state.totalSize;
}

void TtsPlayer_Process(void)
{
    if (!s_state.initialized || !s_state.playing)
        return;

    /* 如果当前是从 SD 卡播放，走 SD 路径 */
    if (SdAudio_IsPlaying()) {
        SdAudio_Process();
        return;
    }

    /* 否则走嵌入数据的非阻塞进给 */
    Audio_Process();

    /* 检查是否播放结束 */
    if (!Audio_IsPlaying()) {
        TtsPlayer_Callback_t cb = s_state.callback;
        reset_state();
        if (cb)
            cb();
    }
}

/* ========== SD 卡初始化辅助 ========== */

const uint8_t* TtsPlayer_GetWordData(int wordIndex)
{
    const AudioClip_t *c = AUDIO_GET_WORD(wordIndex);
    return c ? c->data : NULL;
}

uint32_t TtsPlayer_GetWordSize(int wordIndex)
{
    const AudioClip_t *c = AUDIO_GET_WORD(wordIndex);
    return c ? c->size : 0;
}

const uint8_t* TtsPlayer_GetPhraseData(int phraseIndex)
{
    const AudioClip_t *c = AUDIO_GET_PHRASE(phraseIndex);
    return c ? c->data : NULL;
}

uint32_t TtsPlayer_GetPhraseSize(int phraseIndex)
{
    const AudioClip_t *c = AUDIO_GET_PHRASE(phraseIndex);
    return c ? c->size : 0;
}

int TtsPlayer_GetWordCount(void)
{
    return AUDIO_WORD_COUNT;
}

int TtsPlayer_GetPhraseCount(void)
{
    return AUDIO_PHRASE_COUNT;
}
