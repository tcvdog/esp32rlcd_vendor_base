#ifndef JOYSTICK_BSP_H
#define JOYSTICK_BSP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JOY_ACTION_NONE = 0,
    JOY_ACTION_UP,
    JOY_ACTION_DOWN,
    JOY_ACTION_LEFT,
    JOY_ACTION_RIGHT,
    JOY_ACTION_PRESS,
    JOY_ACTION_LONG_PRESS,
} JoystickAction_t;

void Joystick_Init(void);
JoystickAction_t Joystick_GetAction(void);

#ifdef __cplusplus
}
#endif

#endif
