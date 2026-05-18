#include <stdio.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "button_bsp.h"

static const char *TAG = "Buttons";

/* 按钮引脚定义 */
#define BTN_SW1_PIN  GPIO_NUM_17
#define BTN_SW2_PIN  GPIO_NUM_18
#define BTN_SW3_PIN  GPIO_NUM_19
#define BTN_SW4_PIN  GPIO_NUM_20

/* 消抖时间 */
#define DEBOUNCE_MS  50

static bool s_sw1_last = true, s_sw2_last = true, s_sw3_last = true, s_sw4_last = true;
static bool s_sw1_press = false, s_sw2_press = false, s_sw3_press = false, s_sw4_press = false;
static int64_t s_sw1_time = 0, s_sw2_time = 0, s_sw3_time = 0, s_sw4_time = 0;

static bool read_pin(gpio_num_t pin) {
    return gpio_get_level(pin) != 0;
}

static void debounce(gpio_num_t pin, bool *last, bool *press, int64_t *time) {
    bool cur = read_pin(pin);
    int64_t now = esp_timer_get_time() / 1000;

    if (cur != *last) {
        // 边沿变化, 记录时间
        *last = cur;
        *time = now;
    } else if (!cur) {
        // 持续为低(按下), 且消抖时间到
        if (!*press && (now - *time > DEBOUNCE_MS)) {
            *press = true;
        }
    } else {
        // 松开(高电平), 清除按下标志, 允许下一次触发
        *press = false;
    }
}

void Custom_ButtonInit(void)
{
    ESP_LOGI(TAG, "Init buttons: SW1=GP17, SW2=GP18, SW3=GP19, SW4=GP20");

    gpio_config_t cfg = {};
    cfg.intr_type = GPIO_INTR_DISABLE;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pin_bit_mask = (1ULL << BTN_SW1_PIN) | (1ULL << BTN_SW2_PIN) |
                       (1ULL << BTN_SW3_PIN) | (1ULL << BTN_SW4_PIN);
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&cfg);

    /* 读取初始状态 */
    s_sw1_last = read_pin(BTN_SW1_PIN);
    s_sw2_last = read_pin(BTN_SW2_PIN);
    s_sw3_last = read_pin(BTN_SW3_PIN);
    s_sw4_last = read_pin(BTN_SW4_PIN);
}

ButtonState_t Buttons_GetState(void)
{
    debounce(BTN_SW1_PIN, &s_sw1_last, &s_sw1_press, &s_sw1_time);
    debounce(BTN_SW2_PIN, &s_sw2_last, &s_sw2_press, &s_sw2_time);
    debounce(BTN_SW3_PIN, &s_sw3_last, &s_sw3_press, &s_sw3_time);
    debounce(BTN_SW4_PIN, &s_sw4_last, &s_sw4_press, &s_sw4_time);

    ButtonState_t st = {
        .sw1 = s_sw1_press,
        .sw2 = s_sw2_press,
        .sw3 = s_sw3_press,
        .sw4 = s_sw4_press,
    };
    return st;
}

bool Button_WasPressed(int pin)
{
    bool *press = NULL;
    switch (pin) {
        case 17: press = &s_sw1_press; break;
        case 18: press = &s_sw2_press; break;
        case 19: press = &s_sw3_press; break;
        case 20: press = &s_sw4_press; break;
        default: return false;
    }
    bool ret = *press;
    *press = false;  // 消费掉
    return ret;
}
