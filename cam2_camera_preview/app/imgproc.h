/*
 * imgproc.h - 图像处理模块
 * YUYV 格式下：灰度提取 / Sobel 边缘检测
 */

#ifndef IMGPROC_H
#define IMGPROC_H

#include <stdint.h>

/* 滤镜模式 */
typedef enum
{
    FILTER_NORMAL = 0, /* 原始 YUYV → RGB565 (颜色转换预览) */
    FILTER_GRAY = 1,   /* 灰度化 (仅 Y 分量) */
    FILTER_SOBEL = 2,  /* Sobel 边缘检测 */
    FILTER_COUNT
} filter_mode_t;

/* YUYV 一帧 → RGB565 backbuf (行从 (offset_x, offset_y) 开始, 居中显示) */
void yuyv_to_rgb565_center(const uint8_t *yuyv, int src_w, int src_h,
                           uint16_t *dst_buf, int dst_w, int dst_h,
                           int offset_x, int offset_y);

/* YUYV → 灰度 → RGB565 居中显示 (仅 Y 分量) */
void yuyv_to_gray_center(const uint8_t *yuyv, int src_w, int src_h,
                         uint16_t *dst_buf, int dst_w, int dst_h,
                         int offset_x, int offset_y);

/* YUYV → Sobel 边缘 → RGB565 居中显示 */
void yuyv_to_sobel_center(const uint8_t *yuyv, int src_w, int src_h,
                          uint16_t *dst_buf, int dst_w, int dst_h,
                          int offset_x, int offset_y);

#endif
