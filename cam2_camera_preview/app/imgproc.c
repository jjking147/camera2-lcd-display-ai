/*
 * imgproc.c - YUYV 图像处理实现
 * 包括：YUYV→RGB565, 灰度化, Sobel 边缘检测
 */

#include "imgproc.h"
#include <string.h>
#include <stdlib.h>

/* ========== 辅助函数 ========== */

/* YUYV → RGB565 (单个像素对: Y0,U,Y1,V → 2个RGB565像素) */
static inline void yuyv_to_rgb565_pixelpair(uint8_t y0, uint8_t u,
                                            uint8_t y1, uint8_t v,
                                            uint16_t *p0, uint16_t *p1)
{
    int c = (int)y0 - 16;
    int d = (int)u - 128;
    int e = (int)v - 128;

    int r = (298 * c + 409 * e + 128) >> 8;
    int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int b = (298 * c + 516 * d + 128) >> 8;

    /* clamp */
    if (r < 0)
        r = 0;
    if (r > 255)
        r = 255;
    if (g < 0)
        g = 0;
    if (g > 255)
        g = 255;
    if (b < 0)
        b = 0;
    if (b > 255)
        b = 255;
    *p0 = (uint16_t)(((r << 8) & 0xF800) | ((g << 3) & 0x07E0) | ((b >> 3) & 0x001F));

    c = (int)y1 - 16;
    r = (298 * c + 409 * e + 128) >> 8;
    g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    b = (298 * c + 516 * d + 128) >> 8;
    if (r < 0)
        r = 0;
    if (r > 255)
        r = 255;
    if (g < 0)
        g = 0;
    if (g > 255)
        g = 255;
    if (b < 0)
        b = 0;
    if (b > 255)
        b = 255;
    *p1 = (uint16_t)(((r << 8) & 0xF800) | ((g << 3) & 0x07E0) | ((b >> 3) & 0x001F));
}

/* 仅提取 Y 分量 → RGB565 灰度 */
static inline uint16_t gray_to_rgb565(uint8_t y)
{
    /* R=G=B=Y → RGB565 */
    return (uint16_t)(((y << 8) & 0xF800) | ((y << 3) & 0x07E0) | ((y >> 3) & 0x001F));
}

/* ========== 滤镜 0: YUYV → RGB565 居中显示 ========== */
void yuyv_to_rgb565_center(const uint8_t *yuyv, int src_w, int src_h,
                           uint16_t *dst_buf, int dst_w, int dst_h,
                           int offset_x, int offset_y)
{
    if (!yuyv || !dst_buf)
        return;
    int src_stride = src_w * 2; /* YUYV: 2字节/像素 */
    (void)dst_h;

    for (int y = 0; y < src_h; y++)
    {
        int dst_row = offset_y + y;
        if (dst_row < 0 || dst_row >= dst_h)
            continue;
        const uint8_t *src_row = yuyv + y * src_stride;
        uint16_t *dst_row_ptr = dst_buf + dst_row * dst_w + offset_x;

        for (int x = 0; x < src_w; x += 2)
        {
            int dst_x = offset_x + x;
            if (dst_x >= 0 && dst_x + 1 < dst_w)
            {
                yuyv_to_rgb565_pixelpair(
                    src_row[x * 2],     /* Y0 */
                    src_row[x * 2 + 1], /* U  */
                    src_row[x * 2 + 2], /* Y1 */
                    src_row[x * 2 + 3], /* V  */
                    &dst_row_ptr[x],
                    &dst_row_ptr[x + 1]);
            }
        }
    }
}

/* ========== 滤镜 1: 灰度化 ========== */
void yuyv_to_gray_center(const uint8_t *yuyv, int src_w, int src_h,
                         uint16_t *dst_buf, int dst_w, int dst_h,
                         int offset_x, int offset_y)
{
    if (!yuyv || !dst_buf)
        return;
    int src_stride = src_w * 2;
    (void)dst_h;

    for (int y = 0; y < src_h; y++)
    {
        int dst_row = offset_y + y;
        if (dst_row < 0 || dst_row >= dst_h)
            continue;
        const uint8_t *src_row = yuyv + y * src_stride;
        uint16_t *dst_row_ptr = dst_buf + dst_row * dst_w + offset_x;

        for (int x = 0; x < src_w; x++)
        {
            int dst_x = offset_x + x;
            if (dst_x >= 0 && dst_x < dst_w)
            {
                uint8_t gray = src_row[x * 2]; /* Y 分量 */
                dst_row_ptr[x] = gray_to_rgb565(gray);
            }
        }
    }
}

/* ========== 滤镜 2: Sobel 边缘检测 ========== */
void yuyv_to_sobel_center(const uint8_t *yuyv, int src_w, int src_h,
                          uint16_t *dst_buf, int dst_w, int dst_h,
                          int offset_x, int offset_y)
{
    if (!yuyv || !dst_buf)
        return;
    int src_stride = src_w * 2;
    (void)dst_h;

    /* 先提取 Y 平面到临时 buffer */
    uint8_t *gray = malloc(src_w * src_h);
    if (!gray)
        return;

    for (int y = 0; y < src_h; y++)
    {
        for (int x = 0; x < src_w; x++)
        {
            gray[y * src_w + x] = yuyv[y * src_stride + x * 2];
        }
    }

    /* 3x3 Sobel 算子 */
    for (int y = 1; y < src_h - 1; y++)
    {
        int dst_row = offset_y + y;
        if (dst_row < 0 || dst_row >= dst_h)
            continue;
        uint16_t *dst_row_ptr = dst_buf + dst_row * dst_w + offset_x;

        for (int x = 1; x < src_w - 1; x++)
        {
            int dst_x = offset_x + x;
            if (dst_x < 0 || dst_x >= dst_w)
                continue;

            /* Gx */
            int gx =
                -1 * gray[(y - 1) * src_w + (x - 1)] +
                0 * gray[(y - 1) * src_w + x] +
                1 * gray[(y - 1) * src_w + (x + 1)] +
                -2 * gray[y * src_w + (x - 1)] +
                0 * gray[y * src_w + x] +
                2 * gray[y * src_w + (x + 1)] +
                -1 * gray[(y + 1) * src_w + (x - 1)] +
                0 * gray[(y + 1) * src_w + x] +
                1 * gray[(y + 1) * src_w + (x + 1)];

            /* Gy */
            int gy =
                -1 * gray[(y - 1) * src_w + (x - 1)] +
                -2 * gray[(y - 1) * src_w + x] +
                -1 * gray[(y - 1) * src_w + (x + 1)] +
                0 * gray[y * src_w + (x - 1)] +
                0 * gray[y * src_w + x] +
                0 * gray[y * src_w + (x + 1)] +
                1 * gray[(y + 1) * src_w + (x - 1)] +
                2 * gray[(y + 1) * src_w + x] +
                1 * gray[(y + 1) * src_w + (x + 1)];

            int mag;
            if (gx < 0)
                gx = -gx;
            if (gy < 0)
                gy = -gy;
            mag = gx + gy; /* 近似 sqrt(gx^2+gy^2) */
            if (mag > 255)
                mag = 255;

            dst_row_ptr[x] = gray_to_rgb565((uint8_t)mag);
        }
    }

    free(gray);
}
