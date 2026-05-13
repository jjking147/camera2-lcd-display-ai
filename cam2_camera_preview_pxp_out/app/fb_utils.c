/*
 * fb_utils.c - Framebuffer 显示实现
 * 尝试将 bpp 从 32 切换到 16(RGB565)
 */

#include "fb_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

int fb_init(fb_context_t *fb, const char *dev, int target_bpp)
{
    int fd;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    if (!fb || !dev)
        return -1;
    memset(fb, 0, sizeof(*fb));

    fd = open(dev, O_RDWR);
    if (fd < 0)
    {
        perror("open fb");
        return -1;
    }
    fb->fd = fd;

    /* 获取可变信息 */
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0)
    {
        perror("FBIOGET_VSCREENINFO");
        goto err;
    }
    printf("[FB] current: %dx%d, %dbpp\n",
           vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

    /* 尝试切换 bpp */
    if (target_bpp > 0 && vinfo.bits_per_pixel != (unsigned int)target_bpp)
    {
        vinfo.bits_per_pixel = target_bpp;
        if (ioctl(fd, FBIOPUT_VSCREENINFO, &vinfo) < 0)
        {
            fprintf(stderr, "[FB] set %dbpp failed\n", target_bpp);
        }
        else
        {
            if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0)
            {
                perror("FBIOGET_VSCREENINFO");
                goto err;
            }
            printf("[FB] switched to %dbpp\n", vinfo.bits_per_pixel);
        }
    }

    if (vinfo.bits_per_pixel != 16)
    {
        fprintf(stderr, "[FB] WARNING: bpp=%d, expected 16 for RGB565\n",
                vinfo.bits_per_pixel);
    }

    fb->width = vinfo.xres;
    fb->height = vinfo.yres;
    fb->bpp = vinfo.bits_per_pixel;

    /* 获取固定信息 */
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0)
    {
        perror("FBIOGET_FSCREENINFO");
        goto err;
    }
    fb->line_length = finfo.line_length;
    fb->mmap_size = finfo.smem_len;

    fb->mmap_start = mmap(NULL, fb->mmap_size,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, 0);
    if (fb->mmap_start == MAP_FAILED)
    {
        perror("mmap fb");
        goto err;
    }
    printf("[FB] %dx%d,%dbpp, line=%d, mmap=%zu bytes\n",
           fb->width, fb->height, fb->bpp,
           fb->line_length, fb->mmap_size);

    /* 分配 backbuf (软件双缓冲) */
    fb->backbuf_size = fb->width * fb->height * 2; /* 16bit */
    fb->backbuf = calloc(1, fb->backbuf_size);
    if (!fb->backbuf)
        goto err;

    /* 清屏黑色 */
    fb_clear(fb, 0x0000);
    fb_flush(fb);

    return 0;

err:
    fb_close(fb);
    return -1;
}

void fb_clear(fb_context_t *fb, uint16_t color)
{
    if (!fb || !fb->backbuf)
        return;
    for (int i = 0; i < fb->width * fb->height; i++)
        fb->backbuf[i] = color;
}

void fb_flush(fb_context_t *fb)
{
    if (!fb || !fb->mmap_start || !fb->backbuf)
        return;
    /* memcpy 到显存 */
    memcpy(fb->mmap_start, fb->backbuf, fb->backbuf_size);
    /* ARM 上可能需要 cache sync */
}

void fb_close(fb_context_t *fb)
{
    if (!fb)
        return;
    if (fb->backbuf)
    {
        free(fb->backbuf);
        fb->backbuf = NULL;
    }
    if (fb->mmap_start && fb->mmap_start != MAP_FAILED)
    {
        munmap(fb->mmap_start, fb->mmap_size);
        fb->mmap_start = NULL;
    }
    if (fb->fd >= 0)
    {
        close(fb->fd);
        fb->fd = -1;
    }
    printf("[FB] closed\n");
}
