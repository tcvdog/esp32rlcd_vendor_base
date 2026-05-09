#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include "codec_bsp.h"
#include "i2c_bsp.h"

extern const uint8_t music_pcm_start[] asm("_binary_canon_pcm_start");
extern const uint8_t music_pcm_end[]   asm("_binary_canon_pcm_end");

CodecPort::CodecPort(I2cMasterBus& i2cbus,const char *strName) :
i2cbus_(i2cbus) 
{
    set_codec_board_type(strName);
    codec_init_cfg_t codec_cfg = {};
    codec_cfg.in_mode          = CODEC_I2S_MODE_TDM;
    codec_cfg.out_mode         = CODEC_I2S_MODE_TDM;
    codec_cfg.in_use_tdm       = false;
    codec_cfg.reuse_dev        = false;
    ESP_ERROR_CHECK(init_codec(&codec_cfg));
    playback = get_playback_handle();
    record   = get_record_handle();

    i2c_master_bus_handle_t I2cMasterBus = i2cbus_.Get_I2cBusHandle();
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = Es8311Address;
    dev_cfg.scl_speed_hz    = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(I2cMasterBus, &dev_cfg, &I2c_DevEs8311));

    dev_cfg.device_address  = Es7210Address;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(I2cMasterBus, &dev_cfg, &I2c_DevEs7210));
}

CodecPort::~CodecPort() {
}

void CodecPort::Codec_SetCodecReg(const char *str, uint8_t reg, uint8_t data) {
    if (!strcmp(str, "es8311"))
        i2cbus_.i2c_write_buff(I2c_DevEs8311, reg, &data, 1);
    if (!strcmp(str, "es7210"))
        i2cbus_.i2c_write_buff(I2c_DevEs7210, reg, &data, 1);
}

uint8_t CodecPort::Codec_GetCodecReg(const char *str, uint8_t reg) {
    uint8_t data = 0x00;
    if (!strcmp(str, "es8311"))
        i2cbus_.i2c_read_buff(I2c_DevEs8311, reg, &data, 1);
    if (!strcmp(str, "es7210"))
        i2cbus_.i2c_read_buff(I2c_DevEs7210, reg, &data, 1);
    return data;
}

void CodecPort::CodecPort_SetSpeakerVol(int vol) {
	esp_codec_dev_set_out_vol(playback, vol);
}

void CodecPort::CodecPort_SetMicGain(float db_value) {
	esp_codec_dev_set_in_gain(record, db_value);
}

void CodecPort::CodecPort_CloseSpeaker(void) {
	esp_codec_dev_close(playback);
}

void CodecPort::CodecPort_CloseMic(void) {
	esp_codec_dev_close(record);
}

int CodecPort::CodecPort_PlayWrite(void *ptr,int ptr_len) {
	return esp_codec_dev_write(playback, ptr, ptr_len);
}

void CodecPort::CodecPort_SetInfo(const char *strName,int open_en,int sample_rate,int channel,int bits_per_sample) {
	esp_codec_dev_sample_info_t fs = {};
    	fs.sample_rate = sample_rate;
    	fs.channel = channel;
    	fs.bits_per_sample = bits_per_sample;
	if(open_en) {
		if(!strcmp(strName,"es8311")) {
			esp_codec_dev_open(playback, &fs);
		} else if(!strcmp(strName,"es7210")) {
			esp_codec_dev_open(record, &fs);
		} else {
			esp_codec_dev_open(playback, &fs);
  			esp_codec_dev_open(record, &fs);  
		}
	}
}

uint8_t *CodecPort::CodecPort_GetPcmData(uint32_t *len) {
	*len = (music_pcm_end - music_pcm_start);
	return (uint8_t *)music_pcm_start;
}

// end of file