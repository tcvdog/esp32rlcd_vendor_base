/*
 * board_cfg_data.c — 替换 EMBED_TXTFILES (PlatformIO 兼容)
 * 内容来自 board_cfg.txt
 */
#include <stddef.h>

__attribute__((used, section(".rodata")))
const char board_cfg_txt_data[] =
    "# support in, out, in_out type\n"
    "# support i2c_port, i2s_port settings\n"
    "# support pa_gain, i2c_addr setting\n"
    "\n"
    "Board: C6_AMOLED_1_43\n"
    "i2c: {sda: 18, scl: 8}\n"
    "i2s: {bclk: 21, ws: 22, dout: 23, din: 20, mclk: 19}\n"
    "out: {codec: ES8311, pa: -1, use_mclk: 1, pa_gain:6}\n"
    "in: {codec: ES7210}\n"
    "\n"
    "Board: S3_Korvo_V2\n"
    "i2c: {sda: 17, scl: 18}\n"
    "i2s: {mclk: 16, bclk: 9, ws: 45, din: 10, dout: 8}\n"
    "out: {codec: ES8311, pa: 48, pa_gain: 6, use_mclk: 1, pa_gain:6}\n"
    "in: {codec: ES7210}\n"
    "\n"
    "Board: S3_LCD_3_49\n"
    "i2c: {sda: 47, scl: 48}\n"
    "i2s: {mclk: 7, bclk: 15, ws: 46, din: 6, dout: 45}\n"
    "out: {codec: ES8311, pa: -1, pa_gain: 6, use_mclk: 1, pa_gain:6}\n"
    "in: {codec: ES7210}\n"
    "\n"
    "Board: S3_RLCD_4_2\n"
    "i2c: {sda: 13, scl: 14}\n"
    "i2s: {mclk: 16, bclk: 9, ws: 45, din: 10, dout: 8}\n"
    "out: {codec: ES8311, pa: 46, pa_gain: 6, use_mclk: 1, pa_gain:6}\n"
    "in: {codec: ES7210}\n"
    "\n";
const size_t board_cfg_txt_size = sizeof(board_cfg_txt_data) - 1;  // exclude null terminator
