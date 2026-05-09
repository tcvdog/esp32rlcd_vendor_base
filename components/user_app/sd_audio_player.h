/**
 * @file sd_audio_player.h
 * @brief SD 卡音频播放器 — 从 SD 卡读取 u-law 文件，通过 Audio 系统播放
 *
 * 文件格式: 原始 u-law 8kHz 数据 (无文件头，和嵌入的 audio_data.h 格式一致)
 * 存放路径: /sdcard/audio/ 目录下
 *
 * 降级策略: SD 卡未挂载或文件不存在时静默返回 false，
 *           调用方（TTS Player）自动回退到嵌入的 audio_data.h
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 初始化 ========== */

/**
 * 初始化 SD 卡并挂载文件系统
 * @param mount_point  挂载点路径 (例如 "/sdcard", 默认值即可)
 * @return true 挂载成功, false 失败 (SD 未插入或不支持)
 */
bool SdAudio_Init(const char *mount_point);

/** 获取 SD 卡是否可用 */
bool SdAudio_IsAvailable(void);

/**
 * 重新初始化 SD 卡 (在需要时重试挂载)
 * @return true 挂载成功
 */
bool SdAudio_Reinit(void);

/* ========== 播放控制 ========== */

/**
 * 从 SD 卡播放 u-law 音频文件
 * 文件读取到堆内存后通过 Audio_StartULaw 非阻塞播放
 * @param path  文件路径 (例如 "/sdcard/audio/word_00.ulaw")
 * @return true 开始播放, false 文件不存在或读取失败
 */
bool SdAudio_PlayFile(const char *path);

/**
 * SD 卡播放器主循环 — 需要在主循环中定期调用
 * 内部转发到 Audio_Process()
 */
void SdAudio_Process(void);

/**
 * 停止当前 SD 卡音频播放
 */
void SdAudio_Stop(void);

/**
 * 查询 SD 卡音频是否正在播放
 */
bool SdAudio_IsPlaying(void);

#ifdef __cplusplus
}
#endif
