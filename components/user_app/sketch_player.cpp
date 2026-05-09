/**
 * @file sketch_player.cpp
 * @brief 素描画渲染引擎实现
 *
 * 设计要点:
 * - SD 卡上存储 64x64 1-bit 黑白位图 (512 字节/幅)
 * - 通过 LVGL canvas 渲染到屏幕上
 * - 支持自动生成占位素描 (基于单词索引的几何图案)
 * - 目录名: /sdcard/sketches/
 * - 文件名: sketch_NNN.sketch (NNN = 单词索引, 三位十进制)
 */
#include "sketch_player.h"
#include "sd_audio_player.h"   // SdAudio_IsAvailable()
#include "display_bsp.h"       // RlcdPort
#include <esp_log.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

static const char *TAG = "Sketch";

/* 当前显示的素描位图 (最后一次加载的) */
static SketchBitmap_t s_current_sketch = {0};
static bool s_has_sketch = false;

/* 照片模式: 阻止 LVGL 覆盖照片 */
static bool s_photo_mode = false;

/* 全屏照片模式 */
static bool s_is_fullscreen = false;
/* 当前全屏照片的单词索引 */
static int s_fs_word_idx = -1;
/* 全屏照片数据缓冲区 (堆分配, 页面存在时保留) */
static uint8_t *s_fs_data = NULL;

/** 预分配全屏照片缓冲区 (应在 Lvgl_PortInit 之前调用) */
void Sketch_AllocPhotoBuffer(void)
{
    if (!s_fs_data) {
        s_fs_data = (uint8_t*)malloc(PHOTO_FS_FILE_SIZE);
        if (s_fs_data) {
            ESP_LOGI(TAG, "Photo buffer pre-allocated (%d bytes)", PHOTO_FS_FILE_SIZE);
        } else {
            ESP_LOGW(TAG, "Photo buffer pre-allocation failed!");
        }
    }
}

/* RLCD 显示器的外部引用 (定义在 lvgl_demo.cpp) */
extern DisplayPort RlcdPort;

/* =================================================================
 * Canvas 缓冲区
 *
 * 300x200 INDEXED_1BIT = 7500 字节, 静态分配在 BSS 段.
 * 所有 canvas 共享同一个缓冲区 (同一时间只有一个 canvas 可见).
 * ================================================================= */
static LV_ATTRIBUTE_LARGE_RAM_ARRAY
uint8_t s_disp_buf[SKETCH_DISP_BUF_SIZE] = {0};

/* =================================================================
 * 内部辅助
 * ================================================================= */

/* =================================================================
 * API 实现
 * ================================================================= */

void Sketch_Init(void)
{
    if (!SdAudio_IsAvailable()) {
        ESP_LOGW(TAG, "SD not available, sketches disabled");
        return;
    }

    /* 检查目录是否存在 */
    struct stat st;
    if (stat(SKETCH_SD_DIR, &st) != 0) {
        if (mkdir(SKETCH_SD_DIR, 0755) != 0 && errno != EEXIST) {
            ESP_LOGE(TAG, "Failed to create %s (errno=%d)",
                     SKETCH_SD_DIR, errno);
            return;
        }
        ESP_LOGI(TAG, "Created %s", SKETCH_SD_DIR);
    }

    /* 扫描统计已有文件 */
    DIR *dir = opendir(SKETCH_SD_DIR);
    if (dir) {
        int count = 0;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strstr(entry->d_name, ".sketch")) count++;
        }
        closedir(dir);
        ESP_LOGI(TAG, "Found %d sketch files in %s", count, SKETCH_SD_DIR);
    }
}

bool Sketch_LoadFromFile(const char *path, SketchBitmap_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }

    size_t read = fread(out->data, 1, SKETCH_FILE_SIZE, f);
    fclose(f);

    if (read != SKETCH_FILE_SIZE) {
        ESP_LOGW(TAG, "Short read on %s (%zu/%d)", path, read, SKETCH_FILE_SIZE);
        return false;
    }

    return true;
}

void Sketch_RenderToCanvas(lv_obj_t *canvas, const SketchBitmap_t *bitmap, bool invert)
{
    if (!canvas || !bitmap) return;

    /* 直接操作 INDEXED_1BIT 缓冲区: 0=白, 1=黑 */
    memset(s_disp_buf, invert ? 0xFF : 0x00, SKETCH_DISP_BUF_SIZE);

    /* 将 80x80 素描缩放至 300x200 居中显示 */
    /* 3x 缩放 = 240x240, 偏移: x=(300-240)/2=30, y=(200-240)/2=-20 (顶上裁剪40px) */
    /* 实际从 y=0 开始绘制, 上部裁剪 20px */
    int scale = 3;
    int off_x = (SKETCH_DISP_W - SKETCH_WIDTH * scale) / 2;
    /* 不裁剪, 让图片从顶部 y=0 开始 */
    int off_y = 0;

    for (int sy = 0; sy < SKETCH_HEIGHT; sy++) {
        for (int sx = 0; sx < SKETCH_WIDTH; sx++) {
            int byte_idx = sy * (SKETCH_WIDTH / 8) + (sx / 8);
            int bit_idx  = 7 - (sx & 7);
            if (bitmap->data[byte_idx] & (1 << bit_idx)) {
                /* 画 3x3 像素块 */
                for (int dy = 0; dy < scale; dy++) {
                    int py = sy * scale + dy;
                    if (py >= SKETCH_DISP_H) break;
                    for (int dx = 0; dx < scale; dx++) {
                        int px = off_x + sx * scale + dx;
                        if (px >= SKETCH_DISP_W) break;
                        int dst_byte = py * (SKETCH_DISP_W / 8) + (px / 8);
                        int dst_bit  = 7 - (px & 7);
                        s_disp_buf[dst_byte] |= (1 << dst_bit);
                    }
                }
            }
        }
    }

    /* 通知 LVGL canvas 已更新 */
    lv_obj_invalidate(canvas);
}

void Sketch_GeneratePlaceholder(int word_idx, SketchBitmap_t *out)
{
    memset(out->data, 0, SKETCH_FILE_SIZE);

    /* 使用单词索引作为随机种子，生成简单几何图案 */
    int seed = word_idx * 7 + 13;
    int shape = seed % 4;

    /*
     * 定义 4 种简单形状:
     *   0 = 圆
     *   1 = 三角形
     *   2 = 五边形
     *   3 = 星形
     */

    /* 辅助: 在 (cx,cy) 画一个半径为 r 的圆 */
    if (shape == 0) {
        int cx = 32, cy = 32, r = 28;
        for (int y = 0; y < SKETCH_HEIGHT; y++) {
            for (int x = 0; x < SKETCH_WIDTH; x++) {
                int dx = x - cx;
                int dy = y - cy;
                int d = dx * dx + dy * dy;
                int r2 = r * r;
                if (d >= (r - 2) * (r - 2) && d <= (r + 1) * (r + 1)) {
                    int byte_idx = y * (SKETCH_WIDTH / 8) + (x / 8);
                    int bit_idx  = 7 - (x & 7);
                    out->data[byte_idx] |= (1 << bit_idx);
                }
            }
        }
    }

    /* 画三角形 */
    if (shape == 1) {
        /* 三角形顶点: (32,4), (4,60), (60,60) */
        int pts[][2] = {{32,4}, {4,60}, {60,60}};
        for (int edge = 0; edge < 3; edge++) {
            int x0 = pts[edge][0], y0 = pts[edge][1];
            int x1 = pts[(edge + 1) % 3][0], y1 = pts[(edge + 1) % 3][1];
            int dx = abs(x1 - x0), dy = abs(y1 - y0);
            int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
            int err = dx - dy;
            int x = x0, y = y0;
            while (1) {
                if (x >= 0 && x < SKETCH_WIDTH && y >= 0 && y < SKETCH_HEIGHT) {
                    int byte_idx = y * (SKETCH_WIDTH / 8) + (x / 8);
                    int bit_idx  = 7 - (x & 7);
                    out->data[byte_idx] |= (1 << bit_idx);
                }
                if (x == x1 && y == y1) break;
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x += sx; }
                if (e2 < dx)  { err += dx; y += sy; }
            }
        }
    }

    /* 画五边形 */
    if (shape == 2) {
        int pts[5][2];
        for (int i = 0; i < 5; i++) {
            float angle = i * 72.0f * 3.14159f / 180.0f - 90.0f * 3.14159f / 180.0f;
            pts[i][0] = (int)(32 + 26 * cos((double)angle) + 0.5f);
            pts[i][1] = (int)(32 + 26 * sin((double)angle) + 0.5f);
        }
        for (int edge = 0; edge < 5; edge++) {
            int x0 = pts[edge][0], y0 = pts[edge][1];
            int x1 = pts[(edge + 1) % 5][0], y1 = pts[(edge + 1) % 5][1];
            int dx = abs(x1 - x0), dy = abs(y1 - y0);
            int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
            int err = dx - dy;
            int x = x0, y = y0;
            while (1) {
                if (x >= 0 && x < SKETCH_WIDTH && y >= 0 && y < SKETCH_HEIGHT) {
                    int byte_idx = y * (SKETCH_WIDTH / 8) + (x / 8);
                    int bit_idx  = 7 - (x & 7);
                    out->data[byte_idx] |= (1 << bit_idx);
                }
                if (x == x1 && y == y1) break;
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x += sx; }
                if (e2 < dx)  { err += dx; y += sy; }
            }
        }
    }

    /* 画星形 */
    if (shape == 3) {
        /* 5 角星 */
        int outer[5][2], inner[5][2];
        for (int i = 0; i < 5; i++) {
            float angle_o = i * 72.0f * 3.14159f / 180.0f - 90.0f * 3.14159f / 180.0f;
            outer[i][0] = (int)(32 + 28 * cos((double)angle_o) + 0.5f);
            outer[i][1] = (int)(32 + 28 * sin((double)angle_o) + 0.5f);
            float angle_i = (i * 72.0f + 36.0f) * 3.14159f / 180.0f - 90.0f * 3.14159f / 180.0f;
            inner[i][0] = (int)(32 + 12 * cos((double)angle_i) + 0.5f);
            inner[i][1] = (int)(32 + 12 * sin((double)angle_i) + 0.5f);
        }
        /* 画外角→内角→外角的连线 */
        for (int i = 0; i < 5; i++) {
            int line_pts[2][2];
            line_pts[0][0] = outer[i][0];
            line_pts[0][1] = outer[i][1];
            line_pts[1][0] = inner[i][0];
            line_pts[1][1] = inner[i][1];
            for (int seg = 0; seg < 2; seg++) {
                int x0 = line_pts[0][0], y0 = line_pts[0][1];
                int x1 = line_pts[1][0], y1 = line_pts[1][1];
                int dx = abs(x1 - x0), dy = abs(y1 - y0);
                int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
                int err = dx - dy;
                int x = x0, y = y0;
                while (1) {
                    if (x >= 0 && x < SKETCH_WIDTH && y >= 0 && y < SKETCH_HEIGHT) {
                        int byte_idx = y * (SKETCH_WIDTH / 8) + (x / 8);
                        int bit_idx  = 7 - (x & 7);
                        out->data[byte_idx] |= (1 << bit_idx);
                    }
                    if (x == x1 && y == y1) break;
                    int e2 = 2 * err;
                    if (e2 > -dy) { err -= dy; x += sx; }
                    if (e2 < dx)  { err += dx; y += sy; }
                }
                /* 内角 → 下一个外角 */
                line_pts[0][0] = inner[i][0];
                line_pts[0][1] = inner[i][1];
                line_pts[1][0] = outer[(i + 1) % 5][0];
                line_pts[1][1] = outer[(i + 1) % 5][1];
            }
        }
    }
}

SketchSource_t Sketch_Load(int word_idx, lv_obj_t *canvas)
{
    SketchBitmap_t bitmap;
    s_is_fullscreen = false;
    s_fs_word_idx = -1;

    /* 1. 确保缓冲区已分配 (只分配一次, 后续重用) */
    if (!s_fs_data) {
        s_fs_data = (uint8_t*)malloc(PHOTO_FS_FILE_SIZE);
        if (!s_fs_data) {
            ESP_LOGE(TAG, "No mem for photo buffer!");
        }
    }

    /* 2. 先尝试全屏照片 /sdcard/sketches/full_NNN.sketch */
    if (SdAudio_IsAvailable() && s_fs_data) {
        char fs_path[48];
        snprintf(fs_path, sizeof(fs_path), "%s/full_%03d.sketch",
                 SKETCH_SD_DIR, word_idx);
        FILE *f = fopen(fs_path, "rb");
        if (f) {
            size_t read = fread(s_fs_data, 1, PHOTO_FS_FILE_SIZE, f);
            fclose(f);
            if (read == PHOTO_FS_FILE_SIZE) {
                s_is_fullscreen = true;
                s_fs_word_idx = word_idx;
                /* 立即渲染到屏幕 */
                for (int y = 0; y < PHOTO_FS_H; y++) {
                    for (int x = 0; x < PHOTO_FS_W; x++) {
                        int bi = y * (PHOTO_FS_W / 8) + (x / 8);
                        int bit = 7 - (x & 7);
                        int pixel = (s_fs_data[bi] >> bit) & 1;
                        RlcdPort.RLCD_SetPixel(x, y,
                                                        pixel ? 0 : 0xFF);
                    }
                }
                RlcdPort.RLCD_Display();
                ESP_LOGI(TAG, "Fullscreen photo %d loaded", word_idx);
                return SKETCH_SOURCE_SD;
            }
            ESP_LOGW(TAG, "Short read on %s", fs_path);
        }
    }

    /* 3. 尝试小素描 /sdcard/sketches/sketch_NNN.sketch */
    if (SdAudio_IsAvailable()) {
        char path[48];
        snprintf(path, sizeof(path), "%s/sketch_%03d.sketch",
                 SKETCH_SD_DIR, word_idx);
        if (Sketch_LoadFromFile(path, &bitmap)) {
            s_current_sketch = bitmap;
            s_has_sketch = true;
            return SKETCH_SOURCE_SD;
        }
    }

    /* 4. 生成占位素描 */
    Sketch_GeneratePlaceholder(word_idx, &bitmap);
    s_current_sketch = bitmap;
    s_has_sketch = true;
    return SKETCH_SOURCE_GENERATED;
}

lv_obj_t* Sketch_CreateCanvas(lv_obj_t *parent)
{
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, s_disp_buf,
                         SKETCH_DISP_W, SKETCH_DISP_H,
                         LV_IMG_CF_INDEXED_1BIT);
    lv_canvas_set_palette(canvas, 0, lv_color_white());
    lv_canvas_set_palette(canvas, 1, lv_color_black());
    memset(s_disp_buf, 0, SKETCH_DISP_BUF_SIZE);
    return canvas;
}

void Sketch_DrawNoSketch(lv_obj_t *canvas)
{
    if (!canvas) return;
    /* 直接清空缓冲区 */
    memset(s_disp_buf, 0, SKETCH_DISP_BUF_SIZE);
    /* 画一个简单的 "X" 在左上角 (白底黑线) */
    int r = 15, cx = 40, cy = 40;
    for (int i = -r; i <= r; i++) {
        int x1 = cx + i, y1 = cy + i;
        int x2 = cx + i, y2 = cy - i;
        for (int t = 0; t < 3; t++) {
            int px1 = x1, py1 = y1 + t - 1;
            int px2 = x2, py2 = y2 + t - 1;
            if (px1 >= 0 && px1 < SKETCH_DISP_W && py1 >= 0 && py1 < SKETCH_DISP_H) {
                int bi = py1 * (SKETCH_DISP_W / 8) + (px1 / 8);
                s_disp_buf[bi] |= (1 << (7 - (px1 & 7)));
            }
            if (px2 >= 0 && px2 < SKETCH_DISP_W && py2 >= 0 && py2 < SKETCH_DISP_H) {
                int bi = py2 * (SKETCH_DISP_W / 8) + (px2 / 8);
                s_disp_buf[bi] |= (1 << (7 - (px2 & 7)));
            }
        }
    }
    lv_obj_invalidate(canvas);
}

void Sketch_ClearCanvas(lv_obj_t *canvas)
{
    if (!canvas) return;
    memset(s_disp_buf, 0, SKETCH_DISP_BUF_SIZE);
    lv_obj_invalidate(canvas);
}

void Sketch_ClearActive(void)
{
    s_has_sketch = false;
    s_is_fullscreen = false;
    s_fs_word_idx = -1;
    s_photo_mode = false;  /* 退出照片模式 */
    /* 保留 s_fs_data 缓冲区, 下次复用不重新分配 */
    if (!s_is_fullscreen) {
        memset(s_disp_buf, 0, SKETCH_DISP_BUF_SIZE);
    }
}

void Sketch_SetPhotoMode(bool on)
{
    s_photo_mode = on;
}

bool Sketch_IsPhotoMode(void)
{
    return s_photo_mode;
}

uint32_t Sketch_GetCanvasBufferSize(void)
{
    return SKETCH_DISP_BUF_SIZE;
}

uint8_t* Sketch_GetCanvasBuffer(void)
{
    return s_disp_buf;
}

void Sketch_OverlayOnDisplay(void)
{
    /* 全屏模式: 直接用保留的缓冲区渲染 */
    if (s_is_fullscreen && s_fs_data) {
        for (int y = 0; y < PHOTO_FS_H; y++) {
            for (int x = 0; x < PHOTO_FS_W; x++) {
                int bi = y * (PHOTO_FS_W / 8) + (x / 8);
                int bit = 7 - (x & 7);
                int pixel = (s_fs_data[bi] >> bit) & 1;
                RlcdPort.RLCD_SetPixel(x, y,
                                                pixel ? 0 : 0xFF);
            }
        }
        return;
    }

    /* 只在有素描数据时绘制 */
    if (!s_has_sketch) return;

    /* 缩放 80x80 → 300x200, 放置左下角 */
    int disp_x0 = 0;      /* 左边界 */
    int disp_y0 = 100;    /* 从 y=100 开始 (底部 200px) */

    for (int dy = 0; dy < 200; dy++) {
        int sy = dy * SKETCH_HEIGHT / 200;
        if (sy >= SKETCH_HEIGHT) sy = SKETCH_HEIGHT - 1;
        for (int dx = 0; dx < 300; dx++) {
            int sx = dx * SKETCH_WIDTH / 300;
            if (sx >= SKETCH_WIDTH) sx = SKETCH_WIDTH - 1;
            int src_byte = sy * (SKETCH_WIDTH / 8) + (sx / 8);
            int src_bit  = 7 - (sx & 7);
            int pixel_on = (s_current_sketch.data[src_byte] >> src_bit) & 1;
            RlcdPort.RLCD_SetPixel(disp_x0 + dx, disp_y0 + dy,
                                             pixel_on ? 0 : 0xFF);
        }
    }
    /* 不在此处调用 RLCD_Display，由 Sketch_PostFlush 统一调用 */
}
