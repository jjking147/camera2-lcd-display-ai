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

/* ========== UYVY → RGB565 居中显示 ========== */
void uyvy_to_rgb565_center(const uint8_t *uyvy, int src_w, int src_h,
                           uint16_t *dst_buf, int dst_w, int dst_h,
                           int offset_x, int offset_y)
{
    if (!uyvy || !dst_buf)
        return;
    int src_stride = src_w * 2; /* UYVY: 2字节/像素 */
    (void)dst_h;

    for (int y = 0; y < src_h; y++)
    {
        int dst_row = offset_y + y;
        if (dst_row < 0 || dst_row >= dst_h)
            continue;
        const uint8_t *src_row = uyvy + y * src_stride;
        uint16_t *dst_row_ptr = dst_buf + dst_row * dst_w + offset_x;

        for (int x = 0; x < src_w; x += 2)
        {
            int dst_x = offset_x + x;
            if (dst_x >= 0 && dst_x + 1 < dst_w)
            {
                /* UYVY: [U, Y0, V, Y1] → pixelpair(Y0, U, Y1, V) */
                yuyv_to_rgb565_pixelpair(
                    src_row[x * 2 + 1], /* Y0 */
                    src_row[x * 2],     /* U  */
                    src_row[x * 2 + 3], /* Y1 */
                    src_row[x * 2 + 2], /* V  */
                    &dst_row_ptr[x],
                    &dst_row_ptr[x + 1]);
            }
        }
    }
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

void yuyv_copy(const uint8_t *src, uint8_t *dst, int w, int h)
{
    if (!src || !dst)
        return;
    memcpy(dst, src, (size_t)w * (size_t)h * 2);
}

void yuyv_to_gray_yuyv(const uint8_t *src, uint8_t *dst, int w, int h)
{
    if (!src || !dst)
        return;

    size_t pixels = (size_t)w * (size_t)h;
    for (size_t i = 0; i < pixels; i += 2)
    {
        size_t idx = i * 2;
        dst[idx] = src[idx];         /* Y0 */
        dst[idx + 1] = 128;          /* U */
        dst[idx + 2] = src[idx + 2]; /* Y1 */
        dst[idx + 3] = 128;          /* V */
    }
}

void yuyv_to_sobel_yuyv(const uint8_t *src, uint8_t *dst, int w, int h)
{
    if (!src || !dst)
        return;

    int stride = w * 2;
    uint8_t *gray = malloc((size_t)w * (size_t)h);
    if (!gray)
        return;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            gray[y * w + x] = src[y * stride + x * 2];
        }
    }

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x += 2)
        {
            uint8_t y0 = 0;
            uint8_t y1 = 0;

            if (y > 0 && y < h - 1 && x > 0 && x < w - 1)
            {
                int gx =
                    -1 * gray[(y - 1) * w + (x - 1)] +
                    0 * gray[(y - 1) * w + x] +
                    1 * gray[(y - 1) * w + (x + 1)] +
                    -2 * gray[y * w + (x - 1)] +
                    0 * gray[y * w + x] +
                    2 * gray[y * w + (x + 1)] +
                    -1 * gray[(y + 1) * w + (x - 1)] +
                    0 * gray[(y + 1) * w + x] +
                    1 * gray[(y + 1) * w + (x + 1)];

                int gy =
                    -1 * gray[(y - 1) * w + (x - 1)] +
                    -2 * gray[(y - 1) * w + x] +
                    -1 * gray[(y - 1) * w + (x + 1)] +
                    0 * gray[y * w + (x - 1)] +
                    0 * gray[y * w + x] +
                    0 * gray[y * w + (x + 1)] +
                    1 * gray[(y + 1) * w + (x - 1)] +
                    2 * gray[(y + 1) * w + x] +
                    1 * gray[(y + 1) * w + (x + 1)];

                if (gx < 0)
                    gx = -gx;
                if (gy < 0)
                    gy = -gy;
                int mag = gx + gy;
                if (mag > 255)
                    mag = 255;
                y0 = (uint8_t)mag;
            }

            if (y > 0 && y < h - 1 && (x + 1) > 0 && (x + 1) < w - 1)
            {
                int gx =
                    -1 * gray[(y - 1) * w + x] +
                    0 * gray[(y - 1) * w + (x + 1)] +
                    1 * gray[(y - 1) * w + (x + 2)] +
                    -2 * gray[y * w + x] +
                    0 * gray[y * w + (x + 1)] +
                    2 * gray[y * w + (x + 2)] +
                    -1 * gray[(y + 1) * w + x] +
                    0 * gray[(y + 1) * w + (x + 1)] +
                    1 * gray[(y + 1) * w + (x + 2)];

                int gy =
                    -1 * gray[(y - 1) * w + x] +
                    -2 * gray[(y - 1) * w + (x + 1)] +
                    -1 * gray[(y - 1) * w + (x + 2)] +
                    0 * gray[y * w + x] +
                    0 * gray[y * w + (x + 1)] +
                    0 * gray[y * w + (x + 2)] +
                    1 * gray[(y + 1) * w + x] +
                    2 * gray[(y + 1) * w + (x + 1)] +
                    1 * gray[(y + 1) * w + (x + 2)];

                if (gx < 0)
                    gx = -gx;
                if (gy < 0)
                    gy = -gy;
                int mag = gx + gy;
                if (mag > 255)
                    mag = 255;
                y1 = (uint8_t)mag;
            }

            size_t idx = (size_t)(y * w + x) * 2;
            dst[idx] = y0;
            dst[idx + 1] = 128;
            dst[idx + 2] = y1;
            dst[idx + 3] = 128;
        }
    }

    free(gray);
}

void yuyv_to_uyvy_inplace(uint8_t *buf, int w, int h)
{
    if (!buf)
        return;
    size_t total = (size_t)w * (size_t)h * 2;
    for (size_t i = 0; i + 3 < total; i += 4)
    {
        uint8_t y0 = buf[i];
        uint8_t u = buf[i + 1];
        uint8_t y1 = buf[i + 2];
        uint8_t v = buf[i + 3];
        buf[i] = u;
        buf[i + 1] = y0;
        buf[i + 2] = v;
        buf[i + 3] = y1;
    }
}
