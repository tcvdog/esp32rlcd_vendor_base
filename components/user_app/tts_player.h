// SPDX-License-Identifier: MIT
// tts_player.h - TTS 发音播放管理层
// 基于 audio_bsp I2S 驱动 + audio_data.h 嵌入音频
#ifndef TTS_PLAYER_H
#define TTS_PLAYER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 播放类型 ========== */
typedef enum {
    PLAY_TYPE_WORD = 0,     // 单词发音
    PLAY_TYPE_PHRASE = 1,   // 短语发音
} PlayType_t;

/* ========== 回调 (播放完成通知) ========== */
typedef void (*TtsPlayer_Callback_t)(void);

/* ========== 初始化与配置 ========== */

/**
 * 初始化 TTS 播放器
 * @param callback 播放完成回调（可选，传 NULL 则无回调）
 * @return true 成功
 */
bool TtsPlayer_Init(TtsPlayer_Callback_t callback);

/**
 * 注册播放完成回调
 */
void TtsPlayer_SetCallback(TtsPlayer_Callback_t callback);

/* ========== 播放控制 ========== */

/**
 * 播放指定单词的发音
 * @param wordIndex 单词索引 (0~49)
 * @return true 成功开始播放
 */
bool TtsPlayer_PlayWord(int wordIndex);

/**
 * 播放指定短语的发音
 * @param phraseIndex 短语索引 (0~19)
 * @return true 成功开始播放
 */
bool TtsPlayer_PlayPhrase(int phraseIndex);

/**
 * 停止当前播放
 */
void TtsPlayer_Stop(void);

/**
 * 当前是否正在播放
 */
bool TtsPlayer_IsPlaying(void);

/**
 * 获取当前播放进度 (已播放字节数)
 */
uint32_t TtsPlayer_GetPosition(void);

/**
 * 获取当前播放总大小
 */
uint32_t TtsPlayer_GetTotalSize(void);

/* ========== 主循环处理 (需要定期调用) ========== */

/**
 * 播放器主循环 - 每次调用发送 I2S 数据
 * 需要在 LVGL 的 timer 或 loop() 中定期调用
 */
void TtsPlayer_Process(void);

/* ========== SD 卡初始化辅助 (供 sd_setup 调用) ========== */

/**
 * 获取嵌入的单词音频数据指针
 */
const uint8_t* TtsPlayer_GetWordData(int wordIndex);

/**
 * 获取嵌入的单词音频数据大小
 */
uint32_t TtsPlayer_GetWordSize(int wordIndex);

/**
 * 获取嵌入的短语音频数据指针
 */
const uint8_t* TtsPlayer_GetPhraseData(int phraseIndex);

/**
 * 获取嵌入的短语音频数据大小
 */
uint32_t TtsPlayer_GetPhraseSize(int phraseIndex);

/**
 * 获取单词总数
 */
int TtsPlayer_GetWordCount(void);

/**
 * 获取短语总数
 */
int TtsPlayer_GetPhraseCount(void);

#ifdef __cplusplus
}
#endif

#endif /* TTS_PLAYER_H */
