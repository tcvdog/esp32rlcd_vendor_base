# ESP32-RLCD 智元掌机 — 项目文档

## 1. 项目架构

```
esp32rlcd_vendor_base/
├── main/                          # 主程序 (入口)
│   ├── main.cpp                   # 主循环: 菜单系统 + 音频播放 + 图片浏览
│   ├── user_config.h              # 用户配置宏
│   └── CMakeLists.txt             # PRIV_REQUIRES port_bsp esp_driver_sdmmc fatfs
├── components/
│   ├── port_bsp/                  # 板级支持包 (关键组件)
│   │   ├── display_bsp.cpp/.h     # RLCD 400x300 1-bit 显示驱动
│   │   ├── codec_bsp.cpp/.h       # ES8311 音频编解码器驱动 (基于 esp_codec_dev)
│   │   ├── i2c_bsp.cpp/.h         # I2C 总线驱动 (SCL=14, SDA=13)
│   │   ├── joystick_bsp.c/.h      # 摇杆驱动 (ADC1_CH0/CH1 + GPIO3)
│   │   ├── audio_bsp.cpp/.h       # 独立音频播放驱动 (16kHz μ-law 非阻塞)
│   │   ├── button_bsp.c/.h        # 物理按钮
│   │   ├── bt_audio.cpp/.h        # 蓝牙音频 (未使用)
│   │   └── pcm/canon.pcm          # 内嵌测试音频 (24kHz 16bit stereo, 约1.9MB)
│   ├── app_bsp/                   # 应用层支持
│   │   ├── lvgl_bsp.cpp/.h        # LVGL 移植层 (显示驱动 + 输入设备)
│   │   ├── font_cn_16.c           # 中文字库 16px (LVGL font format)
│   │   └── font_cn_24.c           # 中文字库 24px
│   ├── user_app/                  # 旧项目功能模块 (当前 main.cpp 未使用)
│   │   ├── ui_manager.cpp/.h      # 旧版 LVGL UI
│   │   ├── vocab_loader.cpp/.h    # 词库加载
│   │   ├── sd_audio_player.cpp/.h # SD卡音频播放器
│   │   ├── sketch_player.cpp/.h   # 素描画/照片显示引擎
│   │   ├── sm2_scheduler.cpp/.h   # SM-2 记忆算法
│   │   ├── arithmetic.cpp/.h      # 算术练习
│   │   ├── socrates_*.cpp/.h      # 苏格拉底问答框架
│   │   ├── snake_game.cpp/.h      # 贪吃蛇
│   │   ├── tts_player.cpp/.h      # TTS 播放
│   │   ├── sd_setup.cpp/.h        # SD卡设置
│   │   └── word_data.h            # 单词数据
│   ├── esp_codec_dev/             # Espressif 官方 codec 库 (源码内嵌)
│   └── ExternLib/codec_board/     # 板级 codec 初始化 (board_cfg.txt 驱动配置)
│       ├── codec_init.c           # I2S + SDMMC + Codec 硬件初始化
│       └── board_cfg.txt          # 板型配置 (S3_RLCD_4_2 的引脚映射)
├── managed_components/            # PlatformIO 管理的组件 (lvgl 等)
├── platformio.ini                 # 编译配置
├── sdkconfig.defaults             # ESP-IDF 默认配置
├── partitions.csv                 # 分区表 (factory 7MB)
└── CMakeLists.txt                 # 顶层 CMake
```

### main.cpp 功能模块

main.cpp 是当前活动的全部功能入口, 约 420 行。包含:

| 模块 | 说明 |
|------|------|
| 菜单系统 | 主菜单 (5项) + 游戏子菜单 (4项) + 各页面 |
| 音频测试 | 播放内嵌 `canon.pcm`, 支持摇杆调音量 |
| 图片测试 | 从 SD 卡读取 `full_NNN.sketch` 并显示 (照片模式) |
| SD卡挂载 | SDMMC 1-bit, CLK=38, CMD=21, D0=39, 挂载点 `/sdcard` |
| LVGL 显示 | 白底黑字, 中文字库, 底部状态栏 |

---

## 2. 使用的工具和框架

### 开发环境
- **IDE**: PlatformIO (VS Code 插件或 CLI)
- **框架**: ESP-IDF (espressif32 @ 5.5.x+)
- **MCU**: ESP32-S3 (ESP32-S3-RLCD-4.2 开发板)
- **显示**: LVGL v8.x (managed_component)

### 依赖库 (在 components/ 中内嵌)
- `esp_codec_dev` — Espressif 官方音频编解码器驱动
- `codec_board` — 厂家板级配置 (含 ES8311 初始化 + SDMMC 挂载)

### 配置文件
| 文件 | 作用 |
|------|------|
| `platformio.ini` | PIO 环境配置: 16MB flash, USB CDC, ESP-IDF 框架 |
| `sdkconfig.defaults` | ESP-IDF Kconfig 默认值: 16MB flash, Octal PSRAM, LVGL 配置 |
| `partitions.csv` | 分区表: factory 7MB, NVS + phy |

---

## 3. 关键引脚定义

### 摇杆 (joystick_bsp.c)
| 信号 | 引脚 | 说明 |
|------|------|------|
| X (左右) | GP1 (`ADC1_CH0`) | ADC 12-bit, 中心值 ~1878 |
| Y (上下) | GP2 (`ADC1_CH1`) | ADC 12-bit, 中心值 ~1773 |
| SW (按下) | GP3 | INPUT_PULLUP, 低电平按下 |
| 死区 | 300 ADC 单位 | 超出死区才判定为方向 |
| 长按 | 800ms | 持续按下超过 800ms 触发 `LONG_PRESS` |
| 防连跳 | `s_last_dir` | 方向改变只触发一次, 回中才能再次触发 |

### 音频 (ES8311 codec + I2S)
- **board_cfg.txt**: `Board: S3_RLCD_4_2`

| 信号 | 引脚 | 说明 |
|------|------|------|
| I2C SCL | GPIO 14 | ES8311 I2C 时钟 |
| I2C SDA | GPIO 13 | ES8311 I2C 数据 |
| I2S MCLK | GPIO 16 | 主时钟 4.096MHz (256×16kHz) |
| I2S BCLK | GPIO 9 | 位时钟 |
| I2S WS | GPIO 45 | 帧时钟/字选择 |
| I2S DOUT | GPIO 8 | DAC 数据输出 (至 ES8311) |
| I2S DIN | GPIO 10 | ADC 数据输入 (未使用) |
| PA_EN | GPIO 46 | 功放使能 (高=喇叭响) |
| ES8311 I2C | 0x18 | 7-bit I2C 地址 |

- **I2S 格式**: 左对齐 (I2S_STD_MSB_SLOT_DEFAULT_CONFIG), 32-bit slot, stereo
- **默认采样率**: 16kHz (但 canon.pcm 为 24kHz)
- **关键寄存器**: ES8311 reg04 = 0x20 (dac_osr=32, 对应 4.096MHz/16kHz)

### 显示 (RLCD — 反射式 LCD, 黑白 1-bit)
| 信号 | 引脚 | 说明 |
|------|------|------|
| SPI MOSI | GPIO 12 | 显示数据 |
| SPI SCLK | GPIO 11 | SPI 时钟 |
| DC | GPIO 5 | 数据/命令选择 |
| CS | GPIO 40 | 片选 |
| RST | GPIO 41 | 复位 |
| 分辨率 | 400×300 | 1-bit 黑白 (0=白, 0xFF=黑) |

### SD 卡 (SDMMC 接口)
| 信号 | 引脚 | 说明 |
|------|------|------|
| SDMMC CLK | GPIO 38 | SD 时钟 |
| SDMMC CMD | GPIO 21 | SD 命令线 |
| SDMMC D0 | GPIO 39 | SD 数据线 (1-bit 模式) |
| 挂载点 | `/sdcard` | FAT32 文件系统 |

---

## 4. 音频播放: 关键函数和调用链

### 内嵌音频播放 (canon.pcm)

```
start_play()  →  xTaskCreate(play_task_fn, "play", ...)
                    │
                    ├── CodecPort_GetPcmData(&sz)          ← 获取内嵌 PCM 数据指针
                    ├── CodecPort_SetInfo("es8311", 1, ...) ← esp_codec_dev_open()
                    ├── CodecPort_SetSpeakerVol(s_volume)   ← esp_codec_dev_set_out_vol()
                    └── while 循环:
                            CodecPort_PlayWrite(d+w, c)      ← esp_codec_dev_write()
                    └── 结束/停止:
                            CodecPort_CloseSpeaker()         ← esp_codec_dev_close()
```

关键注意:
- `CodecPort_CloseSpeaker()` 必须在播放结束时调用, 否则下次 `SetInfo` 的 `esp_codec_dev_open` 会直接返回 (无操作)。这是 `esp_codec_dev.c` 的实现限制 (line 153: `if (dev->output_opened) return OK`).
- 音频播放任务独立于主循环, 使用 `s_stop_play` 标志控制停止。

### 照片读取 (.sketch 格式)

```
show_photo(idx)
  → 打开 /sdcard/sketches/full_NNN.sketch
  → fread(s_fs_data, 1, 15000, f)   ← 400×300 1-bit bitmap
  → for y,x: 解析每个 bit → RLCD_SetPixel(x, y, pixel)
  → RLCD_Display()
```

.sketch 文件格式:
- 400×300 像素, 1-bit 黑白 (每个 bit = 1 像素)
- 15000 字节 (400×300/8)
- Bit 7 = 最左像素, Bit 0 = 最右像素
- 行序从上到下, 每行 50 字节

### 音量控制

```
↑ ↓ 摇杆 → s_volume += 5 (0-100)
         → CodecPort_SetSpeakerVol(s_volume)  ← 实时生效
         → 更新 LVGL 标签 "音量: XX"
```

---

## 5. 烧录和使用

### 编译烧录命令

```bash
# 工作目录
cd ~/桌面/esp32rlcd_vendor_base

# 完整编译
platformio run

# 编译 + 烧录
platformio run --target upload

# 串口监视
platformio device monitor -b 115200

# 编译 + 烧录 + 监视
platformio run --target upload && pio device monitor -b 115200
```

### SD 卡准备

图片文件放入 SD 卡: `/sdcard/sketches/full_NNN.sketch`
- `full_000.sketch` 到 `full_NNN.sketch`
- 格式: 400×300 1-bit 黑白位图, 15000 字节
- 已有素材在: `~/桌面/esp32rlcd/sd_photos_clean/`

### 操作方式 (摇杆)

| 页面 | ↑ | ↓ | ← | → | 按一下 | 长按 |
|------|---|---|---|---|--------|------|
| 主菜单 | 上移 | 下移 | — | — | 进入子项 | — |
| 游戏菜单 | 上移 | 下移 | 返回主菜单 | — | 进入子项 | — |
| 音频测试 | 音量+5 | 音量-5 | 返回 | — | 播放/停止 | 返回 |
| 图片测试 | 下一张 | 上一张 | 返回 | — | — | 返回 |

### 开发板连接
- **USB**: 通过 USB-C 连接电脑 (显示为 USB 串口)
- **喇叭**: 板载喇叭 (通过 ES8311 + PA_EN GPIO46 驱动)
- **SD卡**: 插入板载 SD 卡槽
- **摇杆**: 板载 5 方向摇杆 (X/Y/按下)

### 已知问题和踩坑记录

1. **音频无声音**: 检查 ES8311 reg04=0x20 (dac_osr=32), 必须用 `esp_codec_dev` 官方库
2. **PSRAM 冲突**: `DispBuffer` 用 `MALLOC_CAP_SPIRAM`, 但音频缓冲区必须 `MALLOC_CAP_INTERNAL`
3. **SDMMC 初始化**: 在 `CodecPort` 构造函数中通过 `init_codec()`, 或可在 `app_main` 中单独调用 `mount_sd()`
4. **I2C 引脚顺序**: `I2cMasterBus(SCL, SDA, port)` — SCL 在前不是 SDA
5. **Arduino 框架无音频**: ESP32S3 Arduino 预编译的 libbt.a 不含 Classic BT A2DP 符号, 只能用 ESP-IDF

---

## 6. 项目关联与备份

| 目录 | 说明 |
|------|------|
| `~/桌面/esp32rlcd_vendor_base/` | **当前活跃项目** (基于07_Audio_Test的ESP-IDF版本) |
| `~/桌面/esp32rlcd/` | 旧 Arduino 版本 (含完整词库、用户app模块) |
| `~/桌面/esp32rlcd_espidf_20260506_2205/` | ESP-IDF 移植版备份 |
| `~/桌面/esp32rlcd_working_stable_20260507_0800/` | 最简稳定版 (仅显示+音频) |
| `~/桌面/esp32rlcd/sd_photos_clean/` | .sketch 照片素材 (full_000~002等) |
| `~/桌面/07_Audio_Test/` | 厂家原始音频测试工程 (ESP-IDF框架) |

---

## 7. 下次接手 LLM 的快速指南

1. **当前状态**: 菜单系统 + 音频测试 + 图片浏览已工作。单词/算术等模块的代码在 `components/user_app/` 中, 但未被 main.cpp 使用。
2. **添加新功能**: 在 `main/main.cpp` 中: 添加页面枚举 → 编写显示函数 → 在 `rebuild()` 注册 → 在主循环的 switch-case 添加处理逻辑。
3. **关键编译警告**: `-Werror=format-truncation` 可能导致编译失败, snprintf 缓冲区要足够大 (UTF-8 中文每个字 3 字节)。
4. **内存限制**: DRAM 仅 327KB。LVGL draw buffer ×2, s_fs_data (15KB), 音频缓冲区等需严格控制大小。PSRAM 可用但 SPI 冲突可能崩溃。
5. **修改摇杆/音频/显示驱动的引脚**: 在 `board_cfg.txt` 中修改 `Board: S3_RLCD_4_2` 下的配置。
