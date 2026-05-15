/*
 * pxp_output_utils.c - PXP output-only 显示实现
 *
 * PXP 只做 DMA 搬运，不做 CSC。调用者需提供 RGB565 数据。
 * PXP 输出格式必须匹配 framebuffer (1024x600 RGB565)，
 * 否则 pxp_show_buf 切换 framebuffer 基地址后 stride 不匹配。
 */

#include "pxp_output_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static void pxp_output_reset(pxp_output_t *pxp)
{
    if (!pxp)
        return;
    pxp->fd = -1;
    pxp->out_buffers = NULL;
    pxp->out_nbufs = 0;
    pxp->width = 0;
    pxp->height = 0;
    pxp->bytesperline = 0;
    pxp->sizeimage = 0;
    pxp->next_index = 0;
    pxp->queued = 0;
    pxp->streaming = 0;
}

static int pxp_output_reqbufs(int fd, unsigned int count)
{
    struct v4l2_requestbuffers req;

    memset(&req, 0, sizeof(req));
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = count;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0)
        return -1;
    if (req.count == 0)
    {
        errno = EINVAL;
        return -1;
    }
    return (int)req.count;
}

static int pxp_output_map_buffers(int fd, struct v4l2_buffer_wrap *buffers,
                                  unsigned int count)
{
    for (unsigned int i = 0; i < count; i++)
    {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0)
            return -1;
        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED)
            return -1;
    }
    return 0;
}

static void pxp_output_unmap_buffers(struct v4l2_buffer_wrap *buffers,
                                     unsigned int count)
{
    if (!buffers)
        return;
    for (unsigned int i = 0; i < count; i++)
    {
        if (buffers[i].start && buffers[i].start != MAP_FAILED)
            munmap(buffers[i].start, buffers[i].length);
    }
}

static int pxp_output_stream_on(int fd)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    int ret = ioctl(fd, VIDIOC_STREAMON, &type);
    printf("[PXP-OUT] STREAMON ret=%d%s\n", ret, ret < 0 ? " (FAILED)" : " (OK)");
    if (ret < 0)
        return -1;
    return 0;
}

static void pxp_output_stream_off(int fd)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(fd, VIDIOC_STREAMOFF, &type);
}

int pxp_output_init(pxp_output_t *pxp, const char *dev, int width, int height)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_crop crop;
    unsigned int output_index = 0;
    int out_count;

    if (!pxp || !dev)
    {
        errno = EINVAL;
        return -1;
    }

    pxp_output_reset(pxp);

    pxp->fd = open(dev, O_RDWR);
    if (pxp->fd < 0)
        return -1;

    if (ioctl(pxp->fd, VIDIOC_QUERYCAP, &cap) < 0)
    {
        perror("[PXP-OUT] VIDIOC_QUERYCAP");
        goto fail;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT))
    {
        fprintf(stderr, "[PXP-OUT] device is not VIDEO_OUTPUT\n");
        errno = ENODEV;
        goto fail;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING))
    {
        fprintf(stderr, "[PXP-OUT] device does not support streaming\n");
        errno = ENODEV;
        goto fail;
    }

    if (ioctl(pxp->fd, VIDIOC_S_OUTPUT, &output_index) < 0)
    {
        perror("[PXP-OUT] VIDIOC_S_OUTPUT");
        goto fail;
    }

    memset(&crop, 0, sizeof(crop));
    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY;
    crop.c.left = 0;
    crop.c.top = 0;
    crop.c.width = width;
    crop.c.height = height;
    if (ioctl(pxp->fd, VIDIOC_S_CROP, &crop) < 0)
        perror("[PXP-OUT] VIDIOC_S_CROP");

    /* 设置 PXP 输出格式: RGB565, 尺寸匹配 framebuffer */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = 1024;
    fmt.fmt.pix.height = 600;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(pxp->fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        perror("[PXP-OUT] VIDIOC_S_FMT");
        goto fail;
    }

    /* 读回 PXP 实际协商的格式 */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(pxp->fd, VIDIOC_G_FMT, &fmt) < 0)
    {
        perror("[PXP-OUT] VIDIOC_G_FMT");
        goto fail;
    }

    printf("[PXP-OUT] negotiated: %dx%d, pixfmt=0x%x (%c%c%c%c), bpl=%d, size=%d\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height,
           fmt.fmt.pix.pixelformat,
           (fmt.fmt.pix.pixelformat >> 0) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 24) & 0xFF,
           fmt.fmt.pix.bytesperline,
           fmt.fmt.pix.sizeimage);

    pxp->width = fmt.fmt.pix.width;
    pxp->height = fmt.fmt.pix.height;
    pxp->bytesperline = (int)fmt.fmt.pix.bytesperline;
    pxp->sizeimage = fmt.fmt.pix.sizeimage;

    out_count = pxp_output_reqbufs(pxp->fd, 1);
    if (out_count < 0)
    {
        perror("[PXP-OUT] VIDIOC_REQBUFS");
        goto fail;
    }

    pxp->out_nbufs = (unsigned int)out_count;
    pxp->out_buffers = calloc(pxp->out_nbufs, sizeof(*pxp->out_buffers));
    if (!pxp->out_buffers)
        goto fail;

    if (pxp_output_map_buffers(pxp->fd, pxp->out_buffers, pxp->out_nbufs) < 0)
    {
        perror("[PXP-OUT] map buffers");
        goto fail;
    }

    return 0;

fail:
    pxp_output_close(pxp);
    return -1;
}

int pxp_output_close(pxp_output_t *pxp)
{
    if (!pxp)
        return 0;

    if (pxp->fd >= 0 && pxp->streaming)
        pxp_output_stream_off(pxp->fd);

    if (pxp->out_buffers)
    {
        pxp_output_unmap_buffers(pxp->out_buffers, pxp->out_nbufs);
        free(pxp->out_buffers);
    }

    if (pxp->fd >= 0)
        close(pxp->fd);

    pxp_output_reset(pxp);
    return 0;
}

static int pxp_output_dqbuf(int fd, unsigned int *index)
{
    struct v4l2_buffer buf;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0)
    {
        perror("[PXP-OUT] DQBUF");
        return -1;
    }
    if (index)
        *index = buf.index;
    return 0;
}

static int pxp_output_qbuf(int fd, unsigned int index, size_t bytesused)
{
    struct v4l2_buffer buf;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    buf.bytesused = (unsigned int)bytesused;
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0)
    {
        perror("[PXP-OUT] QBUF");
        return -1;
    }
    return 0;
}

int pxp_output_put_frame(pxp_output_t *pxp, const uint8_t *rgb565, int width, int height)
{
    unsigned int idx;
    size_t row_bytes;
    size_t bytesused;

    if (!pxp || pxp->fd < 0 || !rgb565)
    {
        errno = EINVAL;
        return -1;
    }

    if (pxp->out_nbufs == 0)
    {
        errno = EINVAL;
        return -1;
    }

    if (pxp->queued >= pxp->out_nbufs)
    {
        if (pxp_output_dqbuf(pxp->fd, &idx) < 0)
            return -1;
    }
    else
    {
        idx = pxp->next_index++ % pxp->out_nbufs;
        pxp->queued++;
    }

    row_bytes = (size_t)width * 2;
    bytesused = (size_t)width * (size_t)height * 2;
    if (pxp->sizeimage > 0)
        bytesused = pxp->sizeimage;
    if (pxp->bytesperline > 0)
        bytesused = (size_t)pxp->bytesperline * (size_t)height;
    if (pxp->bytesperline > 0 && (size_t)pxp->bytesperline > row_bytes)
    {
        for (int y = 0; y < height; y++)
        {
            uint8_t *dst = (uint8_t *)pxp->out_buffers[idx].start +
                           (size_t)y * (size_t)pxp->bytesperline;
            memcpy(dst, rgb565 + (size_t)y * row_bytes, row_bytes);
        }
    }
    else
    {
        memcpy(pxp->out_buffers[idx].start, rgb565, (size_t)width * (size_t)height * 2);
    }
    if (pxp_output_qbuf(pxp->fd, idx, bytesused) < 0)
        return -1;

    if (!pxp->streaming)
    {
        /* 读取 framebuffer 物理地址 (STREAMON 前) */
        FILE *fp = fopen("/sys/class/graphics/fb0/phys_addr", "r");
        if (fp) {
            char buf[32];
            if (fgets(buf, sizeof(buf), fp))
                printf("[PXP-OUT] fb0 phys_addr before STREAMON: %s", buf);
            fclose(fp);
        }

        if (pxp_output_stream_on(pxp->fd) < 0)
        {
            perror("[PXP-OUT] STREAMON");
            return -1;
        }
        pxp->streaming = 1;

        /* 读取 framebuffer 物理地址 (STREAMON 后) */
        fp = fopen("/sys/class/graphics/fb0/phys_addr", "r");
        if (fp) {
            char buf[32];
            if (fgets(buf, sizeof(buf), fp))
                printf("[PXP-OUT] fb0 phys_addr after  STREAMON: %s", buf);
            fclose(fp);
        }
    }

    return 0;
}