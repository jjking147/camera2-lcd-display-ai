/*
 * fb_utils.h - Framebuffer 显示模块
 * /dev/fb0, 1024x600 RGB565 (16bpp), 双缓冲
 */

#ifndef FB_UTILS_H
#define FB_UTILS_H

#include <stdint.h>
#include <sys/types.h>

/* framebuffer 句柄 */
typedef struct
{
    int fd;
    void *mmap_start;
    size_t mmap_size;
    int width;       /* 面板全宽 */
    int height;      /* 面板全高 */
    int bpp;         /* bits per pixel */
    int line_length; /* 一行字节 */
    /* 双缓冲: 用 memcpy 到显存或者两个 off-screen buffer */
    uint16_t *backbuf; /* 后台缓冲区 (用户绘制) */
    size_t backbuf_size;
} fb_context_t;

/* 初始化 framebuffer, target_bpp 建议 16 */
int fb_init(fb_context_t *fb, const char *dev, int target_bpp);

/* 清屏 */
void fb_clear(fb_context_t *fb, uint16_t color);

/* 将 backbuf 刷新到显存 (全屏或指定区域) */
void fb_flush(fb_context_t *fb);

/* 释放 */
void fb_close(fb_context_t *fb);

/* 辅助: 在 backbuf 指定 (x,y) 画一个 RGB565 像素 */
static inline void fb_put_pixel(fb_context_t *fb, int x, int y, uint16_t c)
{
    if (!fb || !fb->backbuf)
        return;
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height)
        return;
    fb->backbuf[y * fb->width + x] = c;
}

#endif
