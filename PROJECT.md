# ESP32-RLCD 智元掌机 — 项目完整文档 v2.0

## 1. 项目定位

基于 ESP32-S3-RLCD-4.2 开发板的掌上学习机（单词/短语/算术/宠物），LVGL 菜单 + 直接 RLCD 渲染混合架构。

---

## 2. 项目结构

```
~/桌面/esp32rlcd_vendor_base/
├── main/
│   ├── main.cpp              # 主程序 (1423行)：菜单+学习+宠物+音频
│   ├── ying_image.h           # 鹰图 200x150 1-bit 位图（菜单右侧装饰）
│   ├── phrase_data.h          # 短语数据（英文+中文）
│   ├── user_config.h          # 用户配置宏
│   └── CMakeLists.txt
├── components/
│   ├── port_bsp/              # 板级支持包
│   │   ├── display_bsp.cpp/h  # RLCD 400x300 1-bit 显示驱动 (SPI)
│   │   ├── codec_bsp.cpp/h    # ES8311 音频驱动（基于 esp_codec_dev）
│   │   ├── i2c_bsp.cpp/h      # I2C 总线 (SCL=14, SDA=13)
│   │   ├── joystick_bsp.c/h   # 摇杆驱动 ADC (GP1/GP2) + SW(GP3)
│   │   ├── button_bsp.c/h     # 4个按钮 SW1~SW4 (GP17/18/19/20)
│   │   ├── audio_bsp.cpp/h    # 旧版音频驱动（未使用）
│   │   └── pcm/canon.pcm      # 内嵌测试音频 (1.9MB, 24kHz 16bit)
│   ├── app_bsp/               # 应用层
│   │   ├── lvgl_bsp.cpp/h     # LVGL 移植层
│   │   ├── font_cn_16.c       # 中文字库 16px (1288汉字)
│   │   └── font_cn_24.c       # 中文字库 24px
│   ├── user_app/              # 功能模块（main.cpp已直接使用部分）
│   │   ├── snake_game.cpp/h   # 贪吃蛇游戏
│   │   ├── arithmetic.cpp/h   # 算术练习（幼儿园~三年级）
│   │   ├── vocab_loader.cpp/h # 词库加载
│   │   ├── sd_audio_player.cpp/h # SD卡音频
│   │   ├── sketch_player.cpp/h   # 素描画/照片
│   │   ├── sm2_scheduler.cpp/h   # SM-2 记忆曲线
│   │   ├── socrates_*.cpp/h      # 苏格拉底问答
│   │   ├── tts_player.cpp/h      # TTS播放
│   │   ├── sd_setup.cpp/h        # SD卡设置
│   │   ├── ui_manager.cpp/h      # 旧版UI（不使用）
│   │   └── word_data.h           # 单词数据
│   ├── esp_codec_dev/         # Espressif 官方音频编解码器库
│   └── ExternLib/codec_board/ # 板级配置
│       ├── board_cfg.txt      # S3_RLCD_4_2 引脚映射
│       ├── codec_init.c       # I2S+SDMMC+Codec初始化
│       └── board_cfg_data.c   # 嵌入式配置数据（替代EMBED_TXTFILES）
├── managed_components/        # lvgl 8.3.11（含 gifdec）
├── sdkconfig.defaults         # ESP-IDF默认配置
├── sdkconfig.esp32-s3-devkitc-1 # 板级配置
├── platformio.ini             # 编译配置
├── partitions.csv             # 分区表
└── CMakeLists.txt             # 顶层CMake
```

---

## 3. 菜单系统 (main.cpp)

### 页面
| 枚举 | 菜单项 | 功能 |
|------|--------|------|
| PAGE_MAIN | 主菜单 | 6项：单词/短语/数学/宠物/游戏/设置 |
| PAGE_WORD | 单词学习 | 50词4组，发音+释义切换 |
| PAGE_PHRASE | 短语学习 | 多组短语，发音+释义 |
| PAGE_MATH | 数学练习 | TODO |
| PAGE_PET | 宠物 | 猫GIF动画+喂食/玩耍/休息/清洁 |
| PAGE_GAME | 游戏 | 贪吃蛇/俄罗斯方块 (TODO) |
| PAGE_SETTINGS | 设置 | 音频测试(canon.pcm)/图片测试 |
| PAGE_SOUNDTEST | 音频测试 | 播放SD卡PCM+音量调节 |
| PAGE_PICTEST | 图片测试 | 显示SD卡sketch照片 |

### 操作
- 摇杆↑↓/SW1(GP17)↑/SW3(GP19)↓：导航
- 摇杆确认/SW4(GP20)：进入
- 摇杆←/SW2(GP18)：返回（宠物页除外，宠物页SW2返回）
- 摇杆→：切换释义显示

---

## 4. 宠物模块

### 代码位置
- 定义 & GIF加载：main.cpp L220~L613
- 显示函数：show_pet() L890~L953
- 主循环渲染：L1395~L1398
- 动作逻辑：pet_do_action() L308~L350

### 属性
| 属性 | 变量 | 范围 | 衰减 |
|------|------|------|------|
| 饱食 | s_pet_full | 0-100 | 每60秒-1 |
| 心情 | s_pet_happy | 0-100 | 每60秒-1(~2) |
| 体力 | s_pet_energy | 0-100 | 每60秒-1 |
| 清洁 | s_pet_clean | 0-100 | 每60秒-1 |
| 等级 | s_pet_level | 1+ | 每20exp升级 |
| 积分 | s_pet_points | 0+ | 宠物粮票 |

### 积分来源
- 单词学习按发音：+1分 (L1290)
- 短语学习按发音：+2分 (L1328)

### 动作
| 动作 | 消耗 | 效果 |
|------|------|------|
| 喂食 | -3分 | 饱食+25, 心情+5 |
| 玩耍 | -2分 | 心情+20, 体力-8, 清洁-3 |
| 休息 | 免费 | 体力+25, 饱食-3 |
| 清洁 | -1分 | 清洁+25, 心情+3 |

### GIF加载流程
```
draw_pet_gif() → pet_load_gif_from_sd()
  → fopen("/sdcard/cat_cropped_small.gif")
  → gd_open_gif_data() [gifdec通过lv_mem_alloc分配]
  → gd_get_frame() + gd_render_frame() 逐帧解码
  → 缩放到160x112 → 二值化(lum<150) → 存为1-bit帧缓存
  → 主循环每20ms切换帧(120ms间隔) → RLCD_SetPixel直接渲染
```

### 显示布局
- GIF区域：171,69 (160x112) — 右侧居中
- 属性面板：左侧纵列 (12,86起) 饱食/心情/体力/清洁
- 动作图标：底部居中横排 (140,270) 4个图标

### 素材文件 (项目根目录)
| 文件 | 用途 |
|------|------|
| cat_cropped.gif | 原始猫GIF (615KB, 320x284, 58帧) |
| cat_cropped_small.gif | 缩小版 (69KB, 160x112, 29帧) ← **SD卡使用** |
| cat_cropped_opt.gif | 优化版 (265KB, 270x190) |
| black_cat_feed.gif | 喂食动画素材 |
| black_cat_play.gif | 玩耍动画素材 |
| black_cat_rest.gif | 休息动画素材 |
| black_cat_clean.gif | 清洁动画素材 |
| pet_eagle_preview.png | 宠物鹰预览图 |

---

## 5. 硬件引脚

### 摇杆 (joystick_bsp.c)
| 信号 | 引脚 | ADC通道 | 说明 |
|------|------|---------|------|
| X(上下) | GP1 | ADC1_CH0 | 中心值~1878, 死区300 |
| Y(左右) | GP2 | ADC1_CH1 | 中心值~1773, 死区300 |
| SW | GP3 | GPIO输入 | INPUT_PULLUP, 低=按下 |

**注意**: 硬件上GP1实际对应上下, GP2对应左右（代码内已交换）

### 额外按钮 (button_bsp.c)
| 按钮 | 引脚 | 映射 |
|------|------|------|
| SW1 | GP17 | ↑ 上 |
| SW2 | GP18 | ← 返回/退出 |
| SW3 | GP19 | ↓ 下 |
| SW4 | GP20 | 确认/进入 |

### 音频 (ES8311)
| 信号 | 引脚 | 说明 |
|------|------|------|
| I2C SCL | 14 | ES8311时钟 |
| I2C SDA | 13 | ES8311数据 |
| I2S MCLK | 16 | 4.096MHz (256×16kHz) |
| I2S BCLK | 9 | 位时钟 |
| I2S WS | 45 | 帧时钟 |
| I2S DOUT | 8 | DAC数据 |
| PA_EN | 46 | 功放使能(高=响) |
| ES8311地址 | 0x18 | 7-bit |

### 显示 (RLCD SPI)
| 信号 | 引脚 |
|------|------|
| MOSI | 12 |
| SCLK | 11 |
| DC | 5 |
| CS | 40 |
| RST | 41 |
| 分辨率 | 400×300 1-bit |

### SD卡 (SDMMC 1-bit)
| 信号 | 引脚 |
|------|------|
| CLK | 38 |
| CMD | 21 |
| D0 | 39 |
| 挂载点 | /sdcard (FAT32) |

---

## 6. 音频系统

### 播放流程 (SD卡PCM)
```
start_play() → xTaskCreate(play_task_fn)
  → CodecPort_SetInfo("es8311", 1, 24000, 2, 16)
  → CodecPort_SetSpeakerVol(vol)
  → while循环: CodecPort_PlayWrite(chunk, len)
  → 结束: CodecPort_CloseSpeaker()
```

### 测试音频
- 菜单: 设置→音频测试 (canon.pcm)
- 需要SD卡有 `/sdcard/canon.pcm` (或 `/sdcard/audio/test.pcm`)
- SD卡音频目录: `~/桌面/esp32rlcd_sd_card_audio/`

### 关键修复
- ES8311 reg04=0x20 (dac_osr=32, 匹配4.096MHz/16kHz)
- 必须用 `esp_codec_dev` 库, Arduino框架无音频
- MCLK必须由LEDC提供(Arduino框架下mck_io_num设了会死机)

---

## 7. 图像系统

### 鹰图 (主菜单装饰)
- 文件: `main/ying_image.h`
- 尺寸: 200x150 1-bit
- 位置: 主菜单右侧 (195, 65)
- 渲染: 直接RLCD写入(绕过LVGL), 每次LVGL刷新后重绘

### 照片 (sketch格式)
- SD卡路径: `/sdcard/sketches/full_NNN.sketch`
- 格式: 400x300 1-bit, 15000字节
- 素材: `~/桌面/esp32rlcd/sd_card_sketches/` (100张)
- 渲染: 照片模式(s_photo_mode)跳过LVGL flush

---

## 8. WiFi

- SSID: tcvdog
- 密码: gucheng1215
- 初始化: `app_main()` 中 `esp_wifi_start()` + `esp_wifi_connect()`
- 状态: 自动连接, 暂无UI显示 (可通过串口日志查看)

---

## 9. 字库管理

### 生成流程
```bash
# 1. 扫描源码提取汉字
python3 scan_chars.py  # 当前1288汉字

# 2. 从NotoSansCJK提取子集TTF
pyftsubset NotoSansCJK-Medium.ttc --text-file=chars.txt --output=noto_subset.ttf

# 3. 生成LVGL字体
npx lv_font_conv \
  --font DejaVuSans.ttf --range 0x20-0x7E \
  --symbols "—←↑→↓↔∞≤─▲►▼◄✓✗⚠。0123456789" \
  --font noto_subset.ttf --symbols "$CJK" \
  --size 16 --bpp 1 --format lvgl \
  --lv-font-name font_cn_16 -o font_cn_16.c

# 4. 修复include路径
sed -i 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|' font_cn_16.c
```

### 文件位置
- `components/app_bsp/font_cn_16.c` — 16px字库 (~80KB Flash)
- `components/app_bsp/font_cn_24.c` — 24px字库

---

## 10. 内存布局

| 区域 | 大小 | 用途 |
|------|------|------|
| DRAM | 327KB | 静态~107KB (33%), 可用~220KB |
| PSRAM | 8MB | 已配置(Octal), 平台检测可能不识别 |
| Flash | 16MB | 固件~1.35MB (18%), 剩14MB |
| LVGL池 | 128KB | lv_mem_alloc用 (对象/样式/gifdec帧缓存) |
| LVGL draw buf | 2×8KB | 绘制缓冲 |

### 关键分配
- s_fs_data (照片): 15KB, MALLOC_CAP_INTERNAL
- GIF文件数据: 69KB (cat_cropped_small.gif), malloc
- gifdec帧缓存: ~72KB (4×160×112), lv_mem_alloc
- 1-bit帧缓存: 29帧×2.2KB = ~64KB, malloc

---

## 11. 编译与烧录

### 编译命令
```bash
cd ~/桌面/esp32rlcd_vendor_base
pio run                          # 编译
pio run --target upload          # 编译+烧录(PIO方式)
```

### USB Serial/JTAG 烧录问题
- ESP32-S3板载USB Serial/JTAG (PID 0x1001) **无DTR/RTS**
- 不能直接用 `pio run --target upload` (会卡在连接)
- 必须手动进下载模式: **按住BOOT → 插USB(或按RESET)**
- 使用esptool直接烧录:
```bash
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  --before no-reset --after hard-reset write-flash \
  0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

### 后台自动烧录
```bash
while [ ! -e /dev/ttyACM0 ]; do sleep 0.3; done; \
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  --before no-reset --after hard-reset write-flash \
  0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```
运行后**按住BOOT插USB**即可自动烧录。

### esptool补丁
`reset.py`已修改:
- `__call__` 增加 `errno.EPROTO` (71) 捕获 → USB JTAG Serial跳过DTR/RTS
- `USBJTAGSerialReset.reset()` 改为5秒等待手动BOOT+RST

---

## 12. SD卡内容

### 音频文件
`~/桌面/esp32rlcd_sd_card_audio/`
```
canon.pcm              # 测试音频 (192KB, 24kHz/16bit/stereo)
words/                 # 51个单词PCM (apple.pcm, banana.pcm...)
phrases/               # 102个短语PCM (phrase_001~102.pcm)
```

### 照片文件
`~/桌面/esp32rlcd/sd_card_sketches/`
```
full_000~099.sketch    # 全屏照片 (100张, 400x300 1-bit)
sketch_000~049.sketch  # 小图标 (50个)
```

### 词汇数据
`~/桌面/esp32rlcd/sd_card_vocab/`
```
pet.json, ket.json     # 词库JSON
pet_s1~s8.json         # 分节词库
phrases.json           # 短语数据
socrates.json          # 苏格拉底问题
```

---

## 13. 关键踩坑记录

1. **PlatformIO EMBED_TXTFILES 不兼容**: 使用自定义C数组替代 (`board_cfg_data.c`)
2. **gifdec用lv_mem_alloc**: 不是malloc, LV_MEM_SIZE需≥128KB
3. **RTS/DTR不连GPIO0**: USB Serial/JTAG的DTR不控制GPIO0, 无法串口进入下载模式
4. **I2C引脚顺序**: `I2cMasterBus(SCL, SDA, port)` — SCL在前
5. **字库include路径**: 生成的`font_cn_16.c`含`#include "lvgl/lvgl.h"`需改为`"lvgl.h"`
6. **SDMMC引脚**: CLK=38, CMD=21, D0=39, 1-bit模式
7. **ES8311 reg04**: 必须=0x20 (dac_osr=32), 匹配4.096MHz MCLK
