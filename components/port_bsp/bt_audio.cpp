/**
 * @file bt_audio.cpp
 * @brief 外接蓝牙音频模块驱动 (JDY-62 / BK3266)
 *
 * 通信协议: UART 115200 8N1, AT 命令 + 异步通知
 *
 * JDY-62 常用 AT 命令:
 *   AT             → OK           (握手)
 *   AT+VOL?        → +VOL:xx      (查询音量 0-100)
 *   AT+VOL=xx      → OK           (设置音量)
 *   AT+CONN?       → +CONN:x      (查询连接: 0=未连, 1=已连)
 *   AT+NAME        → +NAME:xx     (设备名)
 *   AT+ADDR?       → +ADDR:xx     (MAC 地址)
 *   AT+DISC        → OK           (断开连接)
 *   AT+STATE?      → +STATE:x     (0=待机, 1=配对中, 2=已连, 3=播放)
 *   AT+RT          → 重启模块
 *
 * 异步通知 (模块主动推送):
 *   +CONNECTED:XX:XX:XX:XX:XX:XX  (手机连接)
 *   +DISCONNECTED                  (手机断开)
 *   +PLAY                          (开始播放)
 *   +PAUSE                         (暂停)
 *   +VOL:x                         (手机端调音量)
 */

#include "bt_audio.h"
#include <Arduino.h>
#include <esp_log.h>

static const char *TAG = "BtAudio";

/* ========== 配置 ========== */
#define BT_UART_BAUD        115200
#define BT_RX_BUF_SIZE      256     /* 接收环形缓冲区 */
#define BT_CMD_TIMEOUT_MS   500     /* AT 命令超时 */
#define BT_REFRESH_MS       3000    /* 状态刷新间隔 */

/* ========== 环形缓冲区 ========== */
typedef struct {
    uint8_t buf[BT_RX_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} RingBuf_t;

static RingBuf_t s_rx_buf = {0};

static void ring_put(uint8_t c) {
    uint16_t next = (s_rx_buf.head + 1) % BT_RX_BUF_SIZE;
    if (next != s_rx_buf.tail)
        s_rx_buf.buf[s_rx_buf.head] = c; s_rx_buf.head = next;
}

static int ring_get(void) {
    if (s_rx_buf.tail == s_rx_buf.head) return -1;
    uint8_t c = s_rx_buf.buf[s_rx_buf.tail];
    s_rx_buf.tail = (s_rx_buf.tail + 1) % BT_RX_BUF_SIZE;
    return c;
}

static void ring_flush(void) {
    s_rx_buf.head = s_rx_buf.tail = 0;
}

/* ========== 状态 ========== */
static BtState_t s_state = BT_STATE_DISCONNECTED;
static char s_device_name[32] = "";
static char s_mac_addr[18] = "";
static uint8_t s_module_vol = 50;
static bool s_module_alive = false;
static BtAudio_StateCallback_t s_callback = NULL;

/* ========== UART 对象 ========== */
static HardwareSerial *s_uart = NULL;
static int s_rx_pin = -1;
static int s_tx_pin = -1;

/* 定时刷新 */
static unsigned long s_last_refresh = 0;

/* ========== UART 接收中断 ========== */
static void uart_rx_isr(void) {
    if (!s_uart) return;
    while (s_uart->available()) {
        ring_put(s_uart->read());
    }
}

/* ========== AT 命令同步发送接收 ========== */

/**
 * 发送 AT 命令并等待响应
 * @param cmd       命令字符串 (不含 AT+ 前缀, 如 "VOL?")
 * @param expected  期望响应前缀 (如 "+VOL:" 或 "OK")
 * @param out       输出缓冲区 (接收完整响应行)
 * @param out_len   输出缓冲区大小
 * @return true 如果收到 expected 前缀
 */
static bool at_cmd(const char *cmd, const char *expected, char *out, int out_len)
{
    if (!s_uart) return false;

    ring_flush();

    /* 发送 AT+命令\r\n */
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "AT+%s\r\n", cmd);
    s_uart->write((uint8_t*)buf, n);
    s_uart->flush();

    /* 读取响应 (非阻塞轮询) */
    int pos = 0;
    unsigned long start = millis();
    while (millis() - start < BT_CMD_TIMEOUT_MS) {
        int c = ring_get();
        if (c >= 0) {
            if (c == '\r' || c == '\n') {
                if (pos > 0) {
                    out[pos] = '\0';
                    // 检查是否匹配
                    if (strstr(out, expected)) {
                        return true;
                    }
                    // 如果收到 "OK" 且期望就是 "OK"
                    if (strcmp(expected, "OK") == 0 && strcmp(out, "OK") == 0) {
                        return true;
                    }
                    // 收到 ERROR
                    if (strstr(out, "ERROR") || strstr(out, "FAIL")) {
                        ESP_LOGW(TAG, "AT cmd '%s' failed: %s", cmd, out);
                        return false;
                    }
                    pos = 0;
                }
            } else {
                if (pos < out_len - 1) out[pos++] = c;
            }
        }
        delay(1);
    }

    ESP_LOGW(TAG, "AT cmd '%s' timeout (expected '%s')", cmd, expected);
    return false;
}

/* ========== 异步通知解析 ========== */
static void parse_notification(const char *line)
{
    if (strstr(line, "+CONNECTED:")) {
        s_state = BT_STATE_CONNECTED;
        /* 提取 MAC */
        const char *mac = line + 11;
        strncpy(s_mac_addr, mac, sizeof(s_mac_addr) - 1);
        ESP_LOGI(TAG, "BT connected: %s", s_mac_addr);
        if (s_callback) s_callback(s_state);
    }
    else if (strstr(line, "+DISCONNECTED")) {
        s_state = BT_STATE_DISCONNECTED;
        ESP_LOGI(TAG, "BT disconnected");
        if (s_callback) s_callback(s_state);
    }
    else if (strstr(line, "+PLAY")) {
        s_state = BT_STATE_PLAYING;
        if (s_callback) s_callback(s_state);
    }
    else if (strstr(line, "+PAUSE")) {
        if (s_state == BT_STATE_PLAYING) s_state = BT_STATE_CONNECTED;
        if (s_callback) s_callback(s_state);
    }
    else if (strstr(line, "+VOL:")) {
        int v = atoi(line + 5);
        if (v > 0) s_module_vol = v;
    }
    else if (strstr(line, "+STATE:")) {
        int st = atoi(line + 7);
        if (st == 0) s_state = BT_STATE_DISCONNECTED;
        else if (st == 1) s_state = BT_STATE_CONNECTING;
        else if (st == 2) s_state = BT_STATE_CONNECTED;
        else if (st == 3) s_state = BT_STATE_PLAYING;
    }
}

/* ========== 公开 API ========== */

bool BtAudio_Init(int tx_pin, int rx_pin, int baud)
{
    if (baud <= 0) baud = BT_UART_BAUD;
    s_tx_pin = tx_pin;
    s_rx_pin = rx_pin;

    ESP_LOGI(TAG, "Init BT module UART: TX=%d, RX=%d, baud=%d", tx_pin, rx_pin, baud);

    /* 初始化 HardwareSerial (使用 Serial2) */
    s_uart = &Serial2;
    s_uart->begin(baud, SERIAL_8N1, rx_pin, tx_pin);
    delay(100);

    /* 清空缓冲区 */
    ring_flush();
    while (s_uart->available()) s_uart->read();

    /* 握手测试 */
    char resp[64] = {0};
    if (at_cmd("", "OK", resp, sizeof(resp))) {   // AT (空命令)
        s_module_alive = true;
        ESP_LOGI(TAG, "JDY-62 module OK, resp: %s", resp);
    } else {
        /* 再试一次 */
        delay(100);
        ring_flush();
        if (at_cmd("", "OK", resp, sizeof(resp))) {
            s_module_alive = true;
        } else {
            ESP_LOGW(TAG, "BT module not responding - check wiring");
        }
    }

    if (s_module_alive) {
        /* 查询基本信息 */
        at_cmd("NAME", "+NAME:", resp, sizeof(resp));
        if (strlen(resp) > 6) {
            strncpy(s_device_name, resp + 6, sizeof(s_device_name) - 1);
        }

        at_cmd("ADDR?", "+ADDR:", resp, sizeof(resp));
        if (strlen(resp) > 6) {
            strncpy(s_mac_addr, resp + 6, sizeof(s_mac_addr) - 1);
        }

        at_cmd("VOL?", "+VOL:", resp, sizeof(resp));
        if (strlen(resp) > 5) {
            s_module_vol = atoi(resp + 5);
        }

        ESP_LOGI(TAG, "BT module: name=%s, addr=%s, vol=%d",
                 s_device_name, s_mac_addr, s_module_vol);

        /* 查询连接状态 */
        BtAudio_RefreshState();
    }

    return s_module_alive;
}

BtState_t BtAudio_GetState(void) { return s_state; }
const char* BtAudio_GetDeviceName(void) { return s_device_name; }
const char* BtAudio_GetMac(void) { return s_mac_addr; }
bool BtAudio_IsModuleAlive(void) { return s_module_alive; }

void BtAudio_RefreshState(void)
{
    if (!s_module_alive || !s_uart) return;

    char resp[64] = {0};
    if (at_cmd("STATE?", "+STATE:", resp, sizeof(resp))) {
        int st = atoi(resp + 7);
        switch (st) {
            case 0: s_state = BT_STATE_DISCONNECTED; break;
            case 1: s_state = BT_STATE_CONNECTING; break;
            case 2: s_state = BT_STATE_CONNECTED; break;
            case 3: s_state = BT_STATE_PLAYING; break;
        }
    } else {
        /* 回退到 CONN? 命令 */
        if (at_cmd("CONN?", "+CONN:", resp, sizeof(resp))) {
            s_state = (resp[6] == '1') ? BT_STATE_CONNECTED : BT_STATE_DISCONNECTED;
        }
    }
}

bool BtAudio_SetVolume(uint8_t vol)
{
    if (!s_module_alive) return false;
    if (vol > 100) vol = 100;

    char cmd[16];
    snprintf(cmd, sizeof(cmd), "VOL=%d", vol);
    char resp[16] = {0};
    bool ok = at_cmd(cmd, "OK", resp, sizeof(resp));
    if (ok) s_module_vol = vol;
    return ok;
}

uint8_t BtAudio_GetModuleVolume(void) { return s_module_vol; }

bool BtAudio_Disconnect(void)
{
    if (!s_module_alive) return false;
    char resp[16] = {0};
    return at_cmd("DISC", "OK", resp, sizeof(resp));
}

bool BtAudio_EnterPairing(void)
{
    /* 大部分模块自动配对；如需要可发 AT+CONN 进入可发现模式 */
    return true;
}

void BtAudio_SetCallback(BtAudio_StateCallback_t cb)
{
    s_callback = cb;
}

void BtAudio_Process(void)
{
    if (!s_uart) return;

    /* 处理模块异步通知 */
    static char line_buf[128];
    static int line_pos = 0;
    int c;
    while ((c = ring_get()) >= 0) {
        if (c == '\r' || c == '\n') {
            if (line_pos > 0) {
                line_buf[line_pos] = '\0';
                parse_notification(line_buf);
                line_pos = 0;
            }
        } else {
            if (line_pos < (int)sizeof(line_buf) - 1)
                line_buf[line_pos++] = c;
        }
    }

    /* 定时刷新状态 */
    unsigned long now = millis();
    if (now - s_last_refresh > BT_REFRESH_MS) {
        s_last_refresh = now;
        BtAudio_RefreshState();
    }
}
