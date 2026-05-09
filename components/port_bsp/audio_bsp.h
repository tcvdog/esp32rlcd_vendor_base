/**
 * @file audio_bsp.h
 * @brief I2S 音频播放驱动 - ES8311 Audio Codec
 *
 * 硬件连接 (ES8311) — 实测 S3_RLCD_4_2 板：
 *   I2S DOUT (DAC输出) -> GPIO 8
 *   I2S BCLK (位时钟)   -> GPIO 9
 *   I2S DIN  (ADC输入)   -> GPIO 10  (当前未使用)
 *   I2S WS   (帧时钟)    -> GPIO 45
 *   PA_EN    (功放使能)   -> GPIO 46  (高电平使能喇叭)
 *   MCLK     (主时钟)    -> GPIO 16  (4.096MHz = 256 x 16kHz)
 *   I2C SDA              -> GPIO 13
 *   I2C SCL              -> GPIO 14
 *   ES8311 I2C 地址: 0x18 (ADDR=GND)
 *   ES7210 I2C 地址: 0x40 (ADC麦克风, 当前未使用)
 *
 * 音频格式: 16-bit, 16kHz, mono -> 双声道输出, I2S Philips 标准
 * ES8311 初始化: I2C 写寄存器配置
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 引脚配置 ========== */
#define PIN_I2S_MCLK    GPIO_NUM_16
#define PIN_I2S_BCLK    GPIO_NUM_9
#define PIN_I2S_WS      GPIO_NUM_45
#define PIN_I2S_DOUT    GPIO_NUM_8

/* I2C 控制引脚 */
#define PIN_I2C_SDA     GPIO_NUM_13
#define PIN_I2C_SCL     GPIO_NUM_14

/* PA 功放使能引脚 (GPIO46 高电平使能喇叭输出) */
#define PIN_PA_ENABLE   GPIO_NUM_46

/* ========== 初始化 ========== */

/** 初始化 ES8311 音频编解码器 (I2C 配置 + I2S 驱动) */
void Audio_Init(void);

/** 反初始化 */
void Audio_Deinit(void);

/* ========== 播放控制 ========== */

/**
 * 播放一段 PCM 数据 (阻塞)
 * @param data    PCM 数据 (16-bit, 16kHz, mono)
 * @param samples 采样点数
 */
void Audio_PlayPCM(const int16_t *data, uint32_t samples);

/**
 * 播放一段 8-bit u-law 编码的数据 (阻塞)
 * @param data  u-law 编码数据 (8-bit, 8kHz)
 * @param len   字节数
 */
void Audio_PlayULaw(const uint8_t *data, uint32_t len);

/* ========== 非阻塞播放 (DMA 后台) ========== */

/**
 * 开始非阻塞 u-law 播放
 * 调用后立即返回，数据在后台通过 DMA 逐步发送
 * @param data  u-law 编码数据 (8-bit, 8kHz) - 调用方需保证数据生命周期
 * @param len   字节数
 */
void Audio_StartULaw(const uint8_t *data, uint32_t len);

/**
 * 非阻塞播放主循环 — 每次调用喂一小块数据给 I2S DMA
 * 需要在主循环或定时器中定期调用 (推荐每 10-20ms 一次)
 */
void Audio_Process(void);

/**
 * 播放一段 16-bit 16kHz PCM 数据 (非阻塞, DMA 背景播放)
 * 注意: 内部调用 Audio_StartULaw 不适用; 保留用于向后兼容
 */
void Audio_PlayPCM_DMA(const int16_t *data, uint32_t samples);

/** 等待当前 DMA 播放结束 (阻塞) */
void Audio_WaitDone(void);

/** 停止当前播放 */
void Audio_Stop(void);

/** 查询是否正在播放 (非阻塞模式下返回真实状态) */
bool Audio_IsPlaying(void);

/* ========== 音量控制 ========== */

/** 设置音量 0-100 (通过 ES8311 寄存器控制) */
void Audio_SetVolume(uint8_t vol);

/** 获取当前音量 */
uint8_t Audio_GetVolume(void);

#ifdef __cplusplus
}
#endif
