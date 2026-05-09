#pragma once

/**
 * @file bt_audio.h
 * @brief 外接蓝牙音频模块驱动 (JDY-62 / BK3266 系列)
 *
 * 硬件连接：
 *   BT_TX → ESP RX (GPIO18)
 *   BT_RX → ESP TX (GPIO17)
 *   BT_VCC → 3.3V
 *   BT_GND → GND
 *   BT_AUDIO_OUT → 功放输入 (或 ES8311 LINE_IN)
 *
 * 模块特点:
 *   - A2DP Sink (手机→模块→喇叭)
 *   - HFP (免提通话)
 *   - UART 115200bps AT 命令控制
 *   - 板载 DAC + 耳机功放 (可直接推喇叭)
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 蓝牙状态 ========== */
typedef enum {
    BT_STATE_DISCONNECTED = 0,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
    BT_STATE_PLAYING,
} BtState_t;

/* ========== 初始化 ========== */

/**
 * 初始化蓝牙模块 UART
 * @param tx_pin  ESP TX 引脚 (接模块 RX)
 * @param rx_pin  ESP RX 引脚 (接模块 TX)
 * @param baud    波特率 (默认 115200)
 * @return true 成功
 */
bool BtAudio_Init(int tx_pin, int rx_pin, int baud);

/* ========== 状态查询 ========== */

/** 获取当前蓝牙连接状态 */
BtState_t BtAudio_GetState(void);

/** 获取已连接设备名称 (如果已连接) */
const char* BtAudio_GetDeviceName(void);

/** 获取蓝牙 MAC 地址 */
const char* BtAudio_GetMac(void);

/** 查询模块是否在线 (UART 应答) */
bool BtAudio_IsModuleAlive(void);

/* ========== 控制命令 ========== */

/** 查询/刷新连接状态 (发送 AT+CONN? 命令) */
void BtAudio_RefreshState(void);

/** 设置音量 0-100 (同步到模块) */
bool BtAudio_SetVolume(uint8_t vol);

/** 获取模块当前音量 */
uint8_t BtAudio_GetModuleVolume(void);

/** 断开当前蓝牙连接 */
bool BtAudio_Disconnect(void);

/** 进入/退出配对模式 */
bool BtAudio_EnterPairing(void);

/* ========== 主循环 ========== */

/**
 * 蓝牙模块主循环 — 需要在主循环/定时器定期调用
 * 处理 UART 数据接收和状态更新
 */
void BtAudio_Process(void);

/**
 * 设置状态变化回调 (可选)
 */
typedef void (*BtAudio_StateCallback_t)(BtState_t new_state);
void BtAudio_SetCallback(BtAudio_StateCallback_t cb);

#ifdef __cplusplus
}
#endif
