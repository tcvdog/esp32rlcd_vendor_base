#pragma once
#include "lvgl.h"
#include "socrates_content.h"
#include "ui_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

void SocratesUI_Init(void);
void SocratesUI_SwitchPage(AppPage_t page);
bool SocratesUI_HandleJoystick(int action);
void SocratesUI_SetQuestion(const SocraticQuestion_t *q);
void SocratesUI_SetMethod(const MemoryMethod_t *m);

#ifdef __cplusplus
}
#endif
