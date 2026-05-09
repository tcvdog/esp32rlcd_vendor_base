/**
 * @file audio_bsp.cpp
 * @brief ES8311 音频驱动 + I2S u-law 播放
 *
 * ES8311 初始化序列完全从 esp_codec_dev 源代码提取 (es8311.c):
 *   es8311_open() → es8311_config_sample() → es8311_start()
 * MCLK=4.096MHz (LEDC), FS=16kHz
 *
 * 硬件验证: 厂家 07_Audio_Test (ESP-IDF) 成功播放 canon.pcm
 * 引脚: MCLK=16, BCLK=9, WS=45, DOUT=8, PA_EN=46
 * I2C: SDA=13, SCL=14, addr=0x18
 */

#include "audio_bsp.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <Wire.h>

static const char *TAG = "Audio";
#define I2S_NUM             I2S_NUM_0
#define I2S_SAMPLE_RATE     16000
#define I2S_BITS            I2S_BITS_PER_SAMPLE_16BIT
#define ES8311_I2C_ADDR     0x18
static uint8_t s_volume = 80;

/* ===== u-law table & non-blocking state (unchanged) ===== */
static const int16_t s_ulaw_table[256] = {
      0,   16,   32,   48,   64,   80,   96,  112,
    128,  144,  160,  176,  192,  208,  224,  240,
    256,  272,  288,  304,  320,  336,  352,  368,
    384,  400,  416,  432,  448,  464,  480,  496,
    512,  544,  576,  608,  640,  672,  704,  736,
    768,  800,  832,  864,  896,  928,  960,  992,
   1024, 1088, 1152, 1216, 1280, 1344, 1408, 1472,
   1536, 1600, 1664, 1728, 1792, 1856, 1920, 1984,
   2048, 2176, 2304, 2432, 2560, 2688, 2816, 2944,
   3072, 3200, 3328, 3456, 3584, 3712, 3840, 3968,
   4096, 4352, 4608, 4864, 5120, 5376, 5632, 5888,
   6144, 6400, 6656, 6912, 7168, 7424, 7680, 7936,
   8192, 8704, 9216, 9728,10240,10752,11264,11776,
  12288,12800,13312,13824,14336,14848,15360,15872,
  16384,17408,18432,19456,20480,21504,22528,23552,
  24576,25600,26624,27648,28672,29696,30720,31744,
  32767,32128,31488,30848,30208,29568,28928,28288,
  27648,27008,26368,25728,25088,24448,23808,23168,
  22528,21888,21248,20608,19968,19328,18688,18048,
  17408,16768,16128,15488,14848,14208,13568,12928,
  12288,11648,11008,10368, 9728, 9088, 8448, 7808,
   7168, 6528, 5888, 5248, 4608, 3968, 3328, 2688,
   2048, 1728, 1408, 1088,  768,  448,  128, -128,
   -448, -768,-1088,-1408,-1728,-2048,-2688,-3328,
  -3968,-4608,-5248,-5888,-6528,-7168,-7808,-8448,
  -9088,-9728,-10368,-11008,-11648,-12288,-12928,-13568,
 -14208,-14848,-15488,-16128,-16768,-17408,-18048,-18688,
 -19328,-19968,-20608,-21248,-21888,-22528,-23168,-23808,
 -24448,-25088,-25728,-26368,-27008,-27648,-28288,-28928,
 -29568,-30208,-30848,-31488,-32128,-32767,-32128,-31488,
 -30848,-30208,-29568,-28928,-28288,-27648,-27008,-26368,
 -25728,-25088,-24448,-23808,-23168,-22528,-21888,-21248,
};

typedef struct {
    bool playing; const uint8_t *ulaw_data;
    uint32_t ulaw_size, ulaw_pos;
} AudioNonBlockState_t;
static AudioNonBlockState_t s_nb_state = {0};

/* ===== I2C ===== */
static bool es8311_write_reg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(reg); Wire.write(val);
    return Wire.endTransmission() == 0;
}
static uint8_t es8311_read_reg(uint8_t reg) {
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(reg); Wire.endTransmission(false);
    Wire.requestFrom(ES8311_I2C_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

/* ===== MCLK via LEDC ===== */
static void setup_mclk(void) {
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_1_BIT;
    t.timer_num = LEDC_TIMER_0;
    t.freq_hz = 4096000;
    t.clk_cfg = LEDC_USE_APB_CLK;
    ledc_timer_config(&t);
    ledc_channel_config_t ch = {};
    ch.gpio_num = PIN_I2S_MCLK;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = LEDC_CHANNEL_0;
    ch.intr_type = LEDC_INTR_DISABLE;
    ch.timer_sel = LEDC_TIMER_0;
    ch.duty = 1;
    ch.hpoint = 0;
    ledc_channel_config(&ch);
}

/* ===== ES8311 初始化 (工厂测试版) ===== */
/* 完全来自 test_factory_audio.cpp 验证过的序列
 * MCLK=4MHz(LEDC)-4.096MHz(理想), FS=16kHz, I2S 16-bit 左对齐
 */
static void es8311_init_codec(void)
{
    Serial.printf("[%s] ES8311 init (factory sequence)...\n", TAG);

    uint8_t ver = es8311_read_reg(0xFF);
    Serial.printf("[%s] ES8311 ver=0x%02X\n", TAG, ver);
    if (ver == 0xFF || ver == 0x00) {
        Serial.printf("[%s] WARNING: ES8311 not responding!\n", TAG);
        return;
    }

    es8311_write_reg(0x00, 0x1F); delay(10);
    es8311_write_reg(0x01, 0x30); delay(5);
    es8311_write_reg(0x02, 0x00);
    es8311_write_reg(0x03, 0x10);
    es8311_write_reg(0x04, 0x20);  /* dac_osr=32 */
    es8311_write_reg(0x05, 0x00);
    es8311_write_reg(0x06, 0x03);  /* bclk_div=4 */
    es8311_write_reg(0x07, 0x00);
    es8311_write_reg(0x08, 0xFF);  /* lrck=256 */
    es8311_write_reg(0x09, 0x01);  /* I2S 16bit left-justified slave */
    es8311_write_reg(0x0A, 0x01);
    es8311_write_reg(0x0B, 0x10);  /* VMID on */
    es8311_write_reg(0x0C, 0x10);  /* DAC power */
    delay(5);
    es8311_write_reg(0x0D, 0x01);  /* charge pump */
    es8311_write_reg(0x0E, 0x02);  /* LDO */
    es8311_write_reg(0x10, 0x02);  /* internal clock */
    es8311_write_reg(0x11, 0x10);  /* ADC */
    es8311_write_reg(0x12, 0x00);  /* normal */
    es8311_write_reg(0x14, 0x1A);  /* HP_SEL=LP, DACL+R */
    es8311_write_reg(0x15, 0x40);  /* ADC unmute */
    es8311_write_reg(0x31, 0x00);  /* DAC unmute */
    es8311_write_reg(0x32, 0x3F);  /* volume max */
    es8311_write_reg(0x37, 0x08);  /* vendor */
    es8311_write_reg(0x44, 0x08);  /* GPIO (×2) */
    delay(1);
    es8311_write_reg(0x44, 0x08);
    es8311_write_reg(0x45, 0x00);

    /* 再读回验证 */
    uint8_t r31 = es8311_read_reg(0x31);
    uint8_t r32 = es8311_read_reg(0x32);
    uint8_t r14 = es8311_read_reg(0x14);
    Serial.printf("[%s] REGS after: 14=%02X 31=%02X 32=%02X\n", TAG, r14, r31, r32);
}

/* ===== 初始化入口 ===== */
void Audio_Init(void) {
    Serial.printf("[%s] Audio init start\n", TAG);
    Wire.begin((int)PIN_I2C_SDA, (int)PIN_I2C_SCL, 400000UL);
    delay(10);
    pinMode(PIN_PA_ENABLE, OUTPUT);
    digitalWrite(PIN_PA_ENABLE, HIGH);
    delay(10);
    setup_mclk();
    delay(20);

    /* 先配置 ES8311（MCLK 已有），再启动 I2S */
    es8311_init_codec();

    /* I2S 驱动: 左对齐(MSB), 16kHz, 16-bit, 立体声 */
    i2s_config_t i2s_cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_MSB,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 3,
        .dma_buf_len = 300,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
    };
    i2s_driver_install(I2S_NUM, &i2s_cfg, 0, NULL);
    i2s_pin_config_t pin = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = PIN_I2S_BCLK,
        .ws_io_num = PIN_I2S_WS,
        .data_out_num = PIN_I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };
    i2s_set_pin(I2S_NUM, &pin);
    i2s_zero_dma_buffer(I2S_NUM);
    // Don't start I2S yet - we'll start after writing test data
    vTaskDelay(pdMS_TO_TICKS(10));

    Audio_SetVolume(s_volume);

    /* 强制重设采样率 */
    i2s_set_sample_rates(I2S_NUM, I2S_SAMPLE_RATE);

    /* 启动 I2S */
    i2s_start(I2S_NUM);
    vTaskDelay(pdMS_TO_TICKS(5));

    /* I2S 快速测试 - 写入非零数据检验 DMA */
    int16_t test_buf[256];
    for (int i = 0; i < 256; i++) test_buf[i] = (i < 128) ? 10000 : -10000;
    size_t bw = 0;
    esp_err_t err = i2s_write(I2S_NUM, test_buf, sizeof(test_buf), &bw, pdMS_TO_TICKS(50));
    Serial.printf("[%s] I2S test: err=%d, written=%u\n", TAG, err, (unsigned)bw);
    if (bw == 0) {
        delay(10);
        i2s_write(I2S_NUM, test_buf, sizeof(test_buf), &bw, pdMS_TO_TICKS(50));
        Serial.printf("[%s] I2S retry: err=%d, written=%u\n", TAG, err, (unsigned)bw);
    }
    Serial.printf("[%s] init done\n", TAG);
}

void Audio_Deinit(void) {
    i2s_driver_uninstall(I2S_NUM);
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    digitalWrite(PIN_PA_ENABLE, LOW);
}

/* ===== 播放函数 (与之前相同) ===== */
void Audio_PlayPCM(const int16_t *data, uint32_t samples) {
    size_t bw = 0;
    uint32_t total = samples * sizeof(int16_t) * 2, remaining = total;
    const int16_t *src = data;
    int retry = 0;
    while (remaining > 0) {
        size_t to_write = (remaining > 1024) ? 1024 : remaining;
        int16_t buf[512];
        uint32_t half = (to_write / sizeof(int16_t)) / 2;
        for (uint32_t i = 0; i < half; i++) {
            int32_t s = (src[i] * s_volume) / 100;
            if (s > 32767) s = 32767; if (s < -32768) s = -32768;
            buf[i*2] = (int16_t)s; buf[i*2+1] = (int16_t)s;
        }
        esp_err_t err = i2s_write(I2S_NUM, buf, to_write, &bw, pdMS_TO_TICKS(10));
        if (err != ESP_OK) break;
        if (bw == 0) { if (++retry > 50) break; delay(5); continue; }
        retry = 0; remaining -= bw; src += bw / (sizeof(int16_t)*2);
    }
}

void Audio_PlayULaw(const uint8_t *d, uint32_t len) {
    #define UC 512
    int16_t pcm[UC]; uint32_t pos = 0;
    while (pos < len) {
        uint32_t c = (len - pos > UC) ? UC : (len - pos);
        for (uint32_t i = 0; i < c; i++) pcm[i] = s_ulaw_table[d[pos+i]];
        Audio_PlayPCM(pcm, c); pos += c;
    }
}

#define NB_ULAW_CHUNK 256
void Audio_StartULaw(const uint8_t *d, uint32_t len) {
    if (!d || !len) return;
    if (s_nb_state.playing) i2s_zero_dma_buffer(I2S_NUM);
    s_nb_state.ulaw_data = d; s_nb_state.ulaw_size = len;
    s_nb_state.ulaw_pos = 0; s_nb_state.playing = true;
}
void Audio_Process(void) {
    if (!s_nb_state.playing) return;
    uint32_t rem = s_nb_state.ulaw_size - s_nb_state.ulaw_pos;
    if (!rem) { s_nb_state.playing = false; return; }
    uint32_t c = (rem > NB_ULAW_CHUNK) ? NB_ULAW_CHUNK : rem;
    int16_t buf[NB_ULAW_CHUNK * 2];
    for (uint32_t i = 0; i < c; i++) {
        int32_t s = s_ulaw_table[s_nb_state.ulaw_data[s_nb_state.ulaw_pos + i]];
        s = (s * s_volume) / 100;
        if (s > 32767) s = 32767; if (s < -32768) s = -32768;
        buf[i*2] = (int16_t)s; buf[i*2+1] = (int16_t)s;
    }
    size_t bw = 0;
    i2s_write(I2S_NUM, buf, c * sizeof(int16_t) * 2, &bw, pdMS_TO_TICKS(5));
    if (bw > 0) { s_nb_state.ulaw_pos += bw / (sizeof(int16_t)*2); }
}

void Audio_PlayPCM_DMA(const int16_t *d, uint32_t s) { Audio_PlayPCM(d, s); }
void Audio_WaitDone(void) { int w = 0; while (s_nb_state.playing && w < 500) { Audio_Process(); delay(5); w++; } }
void Audio_Stop(void) { i2s_zero_dma_buffer(I2S_NUM); s_nb_state.playing = false; s_nb_state.ulaw_pos = 0; s_nb_state.ulaw_size = 0; s_nb_state.ulaw_data = NULL; }
bool Audio_IsPlaying(void) { return s_nb_state.playing; }

void Audio_SetVolume(uint8_t vol) {
    if (vol > 100) vol = 100; s_volume = vol;
    uint8_t r = (vol >= 95) ? 0x18 : (vol == 0) ? 0x3F : (0x3F - (uint8_t)((uint32_t)vol * 39 / 100));
    if (r < 0x18) r = 0x18; if (r > 0x3F) r = 0x3F;
    es8311_write_reg(0x32, r);
}
uint8_t Audio_GetVolume(void) { return s_volume; }
