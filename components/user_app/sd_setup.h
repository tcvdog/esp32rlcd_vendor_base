/**
 * @file sd_setup.h
 * @brief SD 卡初始化工具 — 将嵌入的音频数据写入 SD 卡
 *
 * 在 setup() 中调用 SdAudio_Init() 后，调用此模块检查
 * 是否需要将内置的 70 个音频段写到 SD 卡上。
 *
 * 触发条件：SD 卡已挂载，但 /sdcard/audio/ 目录不存在或为空
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 检测并复制嵌入音频到 SD 卡
 * 仅在第一次使用时执行 (检测 /sdcard/audio/ 是否存在)
 * @return true 表示成功写入或无需写入, false 表示失败
 */
bool SdSetup_CopyAudioToSD(void);

#ifdef __cplusplus
}
#endif
