#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>

#include "button_bsp.h"
#include "user_app.h"
#include "i2c_bsp.h"
#include "codec_bsp.h"

I2cMasterBus I2cbus(14, 13, 0);
CodecPort *codecport = NULL;
EventGroupHandle_t CodecGroups;

void UserApp_AppInit() {
    CodecGroups = xEventGroupCreate();
    Custom_ButtonInit();
    codecport = new CodecPort(I2cbus, "S3_RLCD_4_2");
    codecport->CodecPort_SetInfo("es8311", 1, 16000, 2, 16);
    codecport->CodecPort_SetSpeakerVol(100);
}

void UserApp_UiInit() {
    /* Vendor UI not used - our custom UI is in main.cpp */
}

void UserApp_TaskInit() {
    /* Vendor tasks not needed - main loop handles everything */
}
