/**
 * @file sd_audio_player.cpp
 * @brief SD 卡音频播放器实现
 *
 * 工作流程:
 *   SdAudio_PlayFile(path) → fopen → fread (整文件到堆) → Audio_StartULaw
 *   SdAudio_Process() → Audio_Process() (非阻塞 I2S 进给)
 *   播放结束 → free 堆内存
 */
#include "sd_audio_player.h"
#include "audio_bsp.h"
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <cstring>
#include <cstdio>

static const char *TAG = "SdAudio";

/* ========== 内部状态 ========== */
typedef struct {
    bool        initialized;
    bool        available;      // SD 卡已挂载且可读写
    bool        playing;        // 正在播放
    uint8_t    *heap_buf;       // 从文件读取的 u-law 数据 (堆分配)
    uint32_t    heap_size;      // 缓冲区大小
    char        mount_point[32]; // 挂载点路径
} SdAudioState_t;

static SdAudioState_t s_sd = {0};

/* SDMMC 引脚配置 */
#define SDMMC_CLK   38
#define SDMMC_CMD   21
#define SDMMC_D0    39
#define SDMMC_WIDTH 1   // 1-bit 模式

/* ========== 实现 ========== */

bool SdAudio_Init(const char *mount_point)
{
    if (s_sd.initialized && s_sd.available) {
        return true;
    }

    if (!mount_point) mount_point = "/sdcard";
    strncpy(s_sd.mount_point, mount_point, sizeof(s_sd.mount_point) - 1);
    s_sd.mount_point[sizeof(s_sd.mount_point) - 1] = '\0';

    ESP_LOGI(TAG, "Mounting SD card on %s (CLK=%d, CMD=%d, D0=%d, %d-bit)...",
             mount_point, SDMMC_CLK, SDMMC_CMD, SDMMC_D0, SDMMC_WIDTH);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 3;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;  // 强制 1-bit 模式

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = SDMMC_WIDTH;
    slot_config.clk   = (gpio_num_t)SDMMC_CLK;
    slot_config.cmd   = (gpio_num_t)SDMMC_CMD;
    slot_config.d0    = (gpio_num_t)SDMMC_D0;

    /* 最多重试 3 次, 每次间隔 500ms, 应对接触不良 */
    esp_err_t ret = ESP_ERR_TIMEOUT;
    sdmmc_card_t *card = NULL;
    for (int attempt = 0; attempt < 3 && ret != ESP_OK; attempt++) {
        if (attempt > 0) {
            ESP_LOGW(TAG, "SD mount retry %d/3...", attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        ret = esp_vfs_fat_sdmmc_mount(
            s_sd.mount_point, &host, &slot_config, &mount_config, &card);
    }

    if (ret != ESP_OK || card == NULL) {
        ESP_LOGW(TAG, "SD card mount failed (ret=%d): %s", ret,
                 ret == ESP_ERR_NOT_FOUND ? "no card" :
                 ret == ESP_ERR_TIMEOUT ? "timeout" : "unknown");
        s_sd.available = false;
    } else {
        sdmmc_card_print_info(stdout, card);
        ESP_LOGI(TAG, "SD card mounted successfully");
        s_sd.available = true;
    }

    s_sd.initialized = true;
    s_sd.playing = false;
    s_sd.heap_buf = NULL;
    s_sd.heap_size = 0;

    return s_sd.available;
}

bool SdAudio_IsAvailable(void)
{
    return s_sd.available && s_sd.initialized;
}

bool SdAudio_Reinit(void)
{
    /* 重置状态后重新初始化 */
    s_sd.initialized = false;
    s_sd.available = false;
    return SdAudio_Init(s_sd.mount_point);
}

bool SdAudio_PlayFile(const char *path)
{
    if (!s_sd.available || !path) {
        return false;
    }

    /* 如果正在播放，先停止并释放 */
    if (s_sd.playing) {
        SdAudio_Stop();
    }

    ESP_LOGI(TAG, "Opening audio file: %s", path);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Failed to open: %s", path);
        return false;
    }

    /* 获取文件大小 */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size <= 0) {
        ESP_LOGW(TAG, "Invalid file size: %ld", file_size);
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);

    /* 分配堆内存 */
    uint8_t *buf = (uint8_t *)malloc(file_size);
    if (!buf) {
        ESP_LOGE(TAG, "malloc(%ld) failed!", file_size);
        fclose(f);
        return false;
    }

    /* 读取文件内容 */
    size_t bytes_read = fread(buf, 1, file_size, f);
    fclose(f);

    if (bytes_read != (size_t)file_size) {
        ESP_LOGW(TAG, "Read %zu/%ld bytes", bytes_read, file_size);
        free(buf);
        return false;
    }

    /* 保存状态 */
    s_sd.heap_buf  = buf;
    s_sd.heap_size = file_size;
    s_sd.playing   = true;

    /* 通过现有 Audio 系统非阻塞播放 */
    Audio_StartULaw(buf, file_size);

    ESP_LOGI(TAG, "Started playing: %s (%ld bytes)", path, file_size);
    return true;
}

void SdAudio_Process(void)
{
    if (!s_sd.playing) return;

    /* 转发到 Audio 系统的非阻塞进给 */
    Audio_Process();

    /* 检查播放是否结束 */
    if (!Audio_IsPlaying()) {
        ESP_LOGD(TAG, "Playback finished, freeing buffer");
        Audio_Stop();
        if (s_sd.heap_buf) {
            free(s_sd.heap_buf);
            s_sd.heap_buf = NULL;
        }
        s_sd.heap_size = 0;
        s_sd.playing = false;
    }
}

void SdAudio_Stop(void)
{
    if (s_sd.playing) {
        Audio_Stop();
        if (s_sd.heap_buf) {
            free(s_sd.heap_buf);
            s_sd.heap_buf = NULL;
        }
        s_sd.heap_size = 0;
        s_sd.playing = false;
        ESP_LOGI(TAG, "Playback stopped");
    }
}

bool SdAudio_IsPlaying(void)
{
    return s_sd.playing;
}
