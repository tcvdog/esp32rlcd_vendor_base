# ESP32-RLCD 智元掌机

基于 **ESP32-S3-RLCD-4.2** 的掌上学习机，LVGL 菜单 + 直接 RLCD 渲染混合架构。集单词学习、短语跟读、算术练习、电子宠物、小游戏于一体。

![硬件](docs/hardware.jpg)

---

## 功能

| 模块 | 说明 |
|------|------|
| **单词学习** | 50 词 4 组，发音 + 释义切换，SM-2 记忆曲线复习 |
| **短语学习** | 多组日常短语，跟读 + 释义 |
| **算术练习** | 幼儿园~三年级难度，随机出题 |
| **电子宠物** | 猫 GIF 动画，4 属性（饱食/心情/体力/清洁）+ 喂食/玩耍/休息/清洁 |
| **游戏** | 贪吃蛇（已实现）|
| **设置** | 音频测试、图片浏览 |

积分系统：单词学习 +1 分，短语学习 +2 分，用于宠物系统。

---

## 硬件要求

| 部件 | 型号 |
|------|------|
| 主控 | ESP32-S3 (16MB Flash + 8MB PSRAM) |
| 屏幕 | RLCD 4.2" 400×300 1-bit 反射屏 (SPI) |
| 音频 | ES8311 音频编解码器 |
| 存储 | MicroSD 卡 (FAT32) |
| 输入 | 摇杆 ×1 + 按钮 ×4 |

### 引脚定义

**摇杆**
| 信号 | GPIO |
|------|------|
| X（上下） | GP1 (ADC1_CH0) |
| Y（左右） | GP2 (ADC1_CH1) |
| SW（按压） | GP3 |

**按钮**
| 按钮 | GPIO | 功能 |
|------|------|------|
| SW1 | GP17 | ↑ 上 |
| SW2 | GP18 | ← 返回/退出 |
| SW3 | GP19 | ↓ 下 |
| SW4 | GP20 | ✓ 确认/进入 |

**其他外设**
| 外设 | 接口 | 关键引脚 |
|------|------|----------|
| ES8311 | I2C (SCL=14, SDA=13) + I2S | MCLK=16, BCLK=9, WS=45, DOUT=8, PA_EN=46 |
| RLCD | SPI (MOSI=12, SCLK=11) | DC=5, CS=40, RST=41 |
| SD卡 | SDMMC 1-bit | CLK=38, CMD=21, D0=39 |

---

## 快速开始

### 环境要求

- [PlatformIO](https://platformio.org/) (VS Code 插件或 CLI)
- ESP-IDF v5.x (PlatformIO 自动管理)

### 编译

```bash
cd esp32rlcd_vendor_base
pio run
```

### 烧录

> **注意**：ESP32-S3 板载 USB Serial/JTAG **无 DTR/RTS**，不支持自动复位进下载模式。

1. **按住 BOOT 按钮** → **插入 USB**（或按 RESET）
2. 进入下载模式后：

```bash
pio run --target upload
```

或使用 esptool 直接烧录：

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  --before no-reset --after hard-reset write-flash \
  0x0 .pio/build/esp32-s3-devkitc-1/bootloader.bin \
  0x8000 .pio/build/esp32-s3-devkitc-1/partitions.bin \
  0x10000 .pio/build/esp32-s3-devkitc-1/firmware.bin
```

### SD 卡内容

将以下目录复制到 SD 卡根目录：

```
sdcard/
├── canon.pcm                  # 测试音频
├── words/                     # 单词音频 PCM (51个)
├── phrases/                   # 短语音频 PCM (102个)
├── sketches/                  # 全屏照片 400×300 (100张)
└── vocab/                     # 词库 JSON 数据
```

素材文件位于 `~/桌面/esp32rlcd_sd_card_audio/` 和 `~/桌面/esp32rlcd/sd_card_sketches/`。

---

## 项目结构

```
├── main/
│   ├── main.cpp               # 主程序（菜单 + 学习 + 宠物 + 音频）
│   ├── ying_image.h           # 鹰图 200×150 1-bit 位图（菜单装饰）
│   └── phrase_data.h          # 短语数据
├── components/
│   ├── port_bsp/              # 板级支持包（显示/音频/摇杆/按钮/I2C）
│   ├── app_bsp/               # 应用层（LVGL 移植、中文字库）
│   ├── user_app/              # 功能模块（贪吃蛇、算术、音频播放、照片、SM-2）
│   ├── esp_codec_dev/         # 音频编解码器库
│   └── ExternLib/codec_board/ # 板级配置
├── managed_components/        # LVGL 8.3.11（含 gifdec）
├── sdkconfig.defaults         # ESP-IDF 默认配置
├── sdkconfig.esp32-s3-devkitc-1 # 板级配置
└── platformio.ini             # 编译配置
```

详细文档见 [PROJECT.md](PROJECT.md)。

---

## 构建详情

- **字库**：NotoSansCJK 子集，16px/24px，1-bit LVGL 格式
- **内存**：DRAM ~107KB 静态使用，PSRAM 8MB，LVGL 池 128KB
- **分区**：16MB Flash，OTA 双分区

---

## License

MIT
