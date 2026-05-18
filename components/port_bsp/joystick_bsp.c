/*
 * joystick_bsp.c - 摇杆驱动 (ESP-IDF ADC)
 * X=GP1 (ADC1_CH0), Y=GP2 (ADC1_CH1), SW=GP3 (INPUT_PULLUP)
 */
#include "joystick_bsp.h"
#include <driver/adc.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "Joy";

#define JOY_X_CH    ADC1_CHANNEL_0   /* GP1 */
#define JOY_Y_CH    ADC1_CHANNEL_1   /* GP2 */
#define JOY_SW_PIN  GPIO_NUM_3

#define DEADZONE    300
#define CENTER_X    1878
#define CENTER_Y    1773
#define LONG_PRESS_MS 800

static uint32_t s_press_start = 0;
static bool s_last_sw = true;
static bool s_long_press_reported = false;
static int s_last_dir = 0;  // 0=中心, ±1=上下, ±2=左右 — 防止方向重复触发

void Joystick_Init(void)
{
    ESP_LOGI(TAG, "Joystick init (X=GP1/ADC1_CH0, Y=GP2/ADC1_CH1, SW=GP3)");

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(JOY_X_CH, ADC_ATTEN_DB_12);
    adc1_config_channel_atten(JOY_Y_CH, ADC_ATTEN_DB_12);

    gpio_config_t sw_cfg = {};
    sw_cfg.pin_bit_mask = (1ULL << JOY_SW_PIN);
    sw_cfg.mode = GPIO_MODE_INPUT;
    sw_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    sw_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    sw_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&sw_cfg);

    s_last_sw = gpio_get_level(JOY_SW_PIN);
    ESP_LOGI(TAG, "Center X=%d, Y=%d", adc1_get_raw(JOY_X_CH), adc1_get_raw(JOY_Y_CH));
}

JoystickAction_t Joystick_GetAction(void)
{
    int x = adc1_get_raw(JOY_X_CH);
    int y = adc1_get_raw(JOY_Y_CH);
    bool sw = gpio_get_level(JOY_SW_PIN);
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    int dx = x - CENTER_X;
    int dy = y - CENTER_Y;

    /* Button handling */
    if (!sw && s_last_sw) {
        /* Just pressed */
        s_press_start = now;
        s_long_press_reported = false;
    }
    if (!sw && !s_last_sw) {
        /* Held down */
        if (!s_long_press_reported && (now - s_press_start > LONG_PRESS_MS)) {
            s_long_press_reported = true;
            s_last_sw = sw;
            return JOY_ACTION_LONG_PRESS;
        }
    }
    if (sw && !s_last_sw) {
        /* Released */
        s_last_sw = sw;
        if (!s_long_press_reported) {
            s_last_sw = sw;
            return JOY_ACTION_PRESS;
        }
    }
    s_last_sw = sw;

    /* Direction — 硬件 X轴(GP1)=上下, Y轴(GP2)=左右, 已交换 */
    /* 方向反转: X左右对调(dx符号反), Y上下对调(dy符号反) */
    if (abs(dx) > DEADZONE || abs(dy) > DEADZONE) {
        int dir;
        if (abs(dx) > abs(dy)) {
            dir = (dx > 0) ? 1 : -1;   // 1=UP, -1=DOWN (GP1=上下, 反向后)
        } else {
            dir = (dy > 0) ? 2 : -2;   // 2=LEFT, -2=RIGHT (GP2=左右, 反向后)
        }
        if (dir != s_last_dir) {
            s_last_dir = dir;
            if (dir == 1) return JOY_ACTION_UP;
            if (dir == -1) return JOY_ACTION_DOWN;
            if (dir == 2) return JOY_ACTION_LEFT;
            if (dir == -2) return JOY_ACTION_RIGHT;
        }
    } else {
        s_last_dir = 0;
    }

    return JOY_ACTION_NONE;
}

/* 九宫格位置: 摇杆方向映射到0-8
 * 0=左上(1), 1=上(2), 2=右上(3)
 * 3=左(4),   4=中(5), 5=右(6)
 * 6=左下(7), 7=下(8), 8=右下(9)
 * -1=中位(未超出死区)
 */
int Joystick_GetNumpadPos(void)
{
    int x = adc1_get_raw(JOY_X_CH);
    int y = adc1_get_raw(JOY_Y_CH);
    int dx = x - CENTER_X;
    int dy = y - CENTER_Y;

    if (abs(dx) < DEADZONE && abs(dy) < DEADZONE) return -1;

    int col = (dx < -DEADZONE/2) ? 0 : (dx > DEADZONE/2) ? 2 : 1;
    int row = (dy < -DEADZONE/2) ? 0 : (dy > DEADZONE/2) ? 2 : 1;
    return row * 3 + col;
}
