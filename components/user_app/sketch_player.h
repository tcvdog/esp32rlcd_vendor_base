/**
 * @file sketch_player.h
 * @brief 单词掌机 — 素描画渲染引擎
 *
 * 在单词卡片右侧显示一幅小素描画，直观展示单词的语境。
 * 素描画格式: 64x64 像素, 1-bit 黑白位图
 *   存储在 SD卡: /sdcard/sketches/sketch_NNN.sketch
 *   每个文件 512 字节 (64*64/8)
 *
 * 工具: tools/convert_to_sketch.py — 将 PNG 图片转换为 .sketch 格式
 *
 * 后端: 使用 LVGL canvas 渲染
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 常量 ========== */

/** 素描位图宽度/高度（像素） */
#define SKETCH_WIDTH     80
#define SKETCH_HEIGHT    80

/** 单色位图文件大小 (80*80/8 = 800 字节) */
#define SKETCH_FILE_SIZE (SKETCH_WIDTH * SKETCH_HEIGHT / 8)

/** SD 卡上素描目录路径 */
#define SKETCH_SD_DIR    "/sdcard/sketches"

/** 全屏照片尺寸 (400x300, 覆盖整个 RLCD) */
#define PHOTO_FS_W       400
#define PHOTO_FS_H       300
#define PHOTO_FS_FILE_SIZE (PHOTO_FS_W * PHOTO_FS_H / 8)  /* = 15000 字节 */

/** 显示区大小 (INDEXED_1BIT, 仅 ~7.5KB) */
#define SKETCH_DISP_W    300
#define SKETCH_DISP_H    200
#define SKETCH_DISP_BUF_SIZE (SKETCH_DISP_W * SKETCH_DISP_H / 8)

/* ========== 数据结构 ========== */

/**
 * 运行时 1-bit 素描位图
 * 在 flash 中也可定义，用于内嵌默认素描
 */
typedef struct {
    uint8_t data[SKETCH_FILE_SIZE];
} SketchBitmap_t;

/**
 * 素描数据路由: 优先 SD 卡文件，回退到内嵌默认
 */
typedef enum {
    SKETCH_SOURCE_NONE = 0,     // 无素描
    SKETCH_SOURCE_SD,           // 来自 SD 卡文件
    SKETCH_SOURCE_EMBEDDED,     // 来自内嵌位图
    SKETCH_SOURCE_GENERATED,    // 自动生成占位图
} SketchSource_t;

/* ========== API ========== */

/**
 * 初始化素描系统
 * 创建 /sdcard/sketches/ 目录（如果不存在）
 * 应在 SdAudio_Init() 之后调用
 */
void Sketch_Init(void);

/**
 * 为指定单词索引加载素描画
 * @param word_idx  单词索引 (0 ~ N-1)
 * @param out_canvas 输出: 如果 canvas 指针非空，将素描渲染到此 canvas 对象上
 * @return 素描来源
 */
SketchSource_t Sketch_Load(int word_idx, lv_obj_t *canvas);

/**
 * 创建一个 300x200 INDEXED_1BIT 的 LVGL canvas 用于显示素描
 * @param parent 父对象
 * @return 创建的 canvas 对象
 */
lv_obj_t* Sketch_CreateCanvas(lv_obj_t *parent);

/**
 * 在 canvas 上渲染 1-bit 位图 (自动缩放 80x80→300x200)
 * @param canvas LVGL canvas 对象
 * @param bitmap 1-bit 素描位图数据 (512 字节)
 * @param invert 是否反色
 */
void Sketch_RenderToCanvas(lv_obj_t *canvas, const SketchBitmap_t *bitmap, bool invert);

/**
 * 读取 SD 卡上的 .sketch 文件
 * @param path 文件路径
 * @param out  输出位图
 * @return true 读取成功
 */
bool Sketch_LoadFromFile(const char *path, SketchBitmap_t *out);

/**
 * 生成一个基于单词索引的占位素描 (简单的几何图案)
 * 用于没有素描文件时的后备显示
 * @param word_idx 单词索引
 * @param out      输出位图
 */
void Sketch_GeneratePlaceholder(int word_idx, SketchBitmap_t *out);

/**
 * 在 canvas 上绘制一个"无素描"图标 (灰色叉号)
 */
void Sketch_DrawNoSketch(lv_obj_t *canvas);

/**
 * 清除当前显示的素描/照片 (切换页面时调用)
 */
void Sketch_ClearActive(void);

/**
 * 设置/取消照片模式: 照片模式下 LVGL 不写入 RLCD 像素,
 * 仅显示 Sketch_Load 渲染的照片
 */
void Sketch_SetPhotoMode(bool on);

/**
 * 查询是否在照片模式
 */
bool Sketch_IsPhotoMode(void);

/**
 * 在主循环中调用：在 LVGL 刷完一帧后将素描叠加到 RLCD 显示缓冲区
 * 应在 lv_timer_handler() 之后调用
 */
void Sketch_OverlayOnDisplay(void);

/** 预分配全屏照片缓冲区 (应在 Lvgl_PortInit 之前调用) */
void Sketch_AllocPhotoBuffer(void);

/**
 * 获取共享 canvas 缓冲区指针
 * 用于在创建 lv_canvas 时指定缓冲区
 * @return 指向 64x64 RGB565 缓冲区的指针 (8192 字节)
 */
uint8_t* Sketch_GetCanvasBuffer(void);

/**
 * 清除 canvas (填充白色)
 */
void Sketch_ClearCanvas(lv_obj_t *canvas);

#ifdef __cplusplus
}
#endif
