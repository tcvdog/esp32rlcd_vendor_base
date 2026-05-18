#ifndef BUTTON_BSP_H
#define BUTTON_BSP_H

#include <freertos/FreeRTOS.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 4个独立按键的状态 */
typedef struct {
    bool sw1;  // GP17
    bool sw2;  // GP18
    bool sw3;  // GP19
    bool sw4;  // GP20
} ButtonState_t;

void Custom_ButtonInit(void);
ButtonState_t Buttons_GetState(void);
bool Button_WasPressed(int pin);  // 返回指定引脚是否被按下过(单次触发)

#ifdef __cplusplus
}
#endif

#endif
