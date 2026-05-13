/*
 * pxp_utils.c - i.MX6ULL PXP 同步转换实现
 *
 * 运行路径：
 *   src YUYV frame -> PXP OUTPUT buffer -> PXP CAPTURE buffer(RGB565)
 *   -> copy to framebuffer backbuf
 */

#include "pxp_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef V4L2_PIX_FMT_RGB565
#define V4L2_PIX_FMT_RGB565 v4l2_fourcc('R', 'G', 'B', 'P')
#endif

#ifndef V4L2_CAP_VIDEO_M2M
#define V4L2_CAP_VIDEO_M2M 0x00004000
#endif

#ifndef V4L2_CAP_VIDEO_M2M_MPLANE
#define V4L2_CAP_VIDEO_M2M_MPLANE 0x00008000
#endif

#ifndef V4L2_CAP_VIDEO_OUTPUT
#define V4L2_CAP_VIDEO_OUTPUT 0x00000002
#endif

#ifndef V4L2_CAP_VIDEO_OUTPUT_OVERLAY
#define V4L2_CAP_VIDEO_OUTPUT_OVERLAY 0x00000200
#endif

static void pxp_reset(pxp_context_t *pxp)
{
    if (!pxp)
        return;
    pxp->fd = -1;
    pxp->out_buffers = NULL;
    pxp->cap_buffers = NULL;
    pxp->out_nbufs = 0;
    pxp->cap_nbufs = 0;
    pxp->width = 0;
    pxp->height = 0;
}

static int pxp_reqbufs(int fd, enum v4l2_buf_type type, unsigned int count)
{
    struct v4l2_requestbuffers req;

    memset(&req, 0, sizeof(req));
    req.type = type;
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

static int pxp_map_buffers(int fd, enum v4l2_buf_type type,
                           struct v4l2_buffer_wrap *buffers,
                           unsigned int count)
{
    for (unsigned int i = 0; i < count; i++)
    {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = type;
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

static void pxp_unmap_buffers(struct v4l2_buffer_wrap *buffers, unsigned int count)
{
    if (!buffers)
        return;

    for (unsigned int i = 0; i < count; i++)
    {
        if (buffers[i].start && buffers[i].start != MAP_FAILED)
            munmap(buffers[i].start, buffers[i].length);
    }
}

static int pxp_queue_capture_buffers(int fd, unsigned int count)
{
    for (unsigned int i = 0; i < count; i++)
    {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0)
            return -1;
    }
    return 0;
}

static int pxp_stream_on(int fd)
{
    enum v4l2_buf_type type;

    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0)
        return -1;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0)
        return -1;

    return 0;
}

static void pxp_stream_off(int fd)
{
    enum v4l2_buf_type type;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(fd, VIDIOC_STREAMOFF, &type);
}

int pxp_init(pxp_context_t *pxp, const char *dev, int width, int height)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    int out_count;
    int cap_count;

    if (!pxp || !dev)
    {
        errno = EINVAL;
        return -1;
    }

    pxp_reset(pxp);

    pxp->fd = open(dev, O_RDWR);
    if (pxp->fd < 0)
        return -1;

    if (ioctl(pxp->fd, VIDIOC_QUERYCAP, &cap) < 0)
    {
        perror("[PXP] VIDIOC_QUERYCAP");
        goto fail;
    }

    printf("[PXP] card=%s, driver=%s, caps=0x%x, device_caps=0x%x\n",
           cap.card, cap.driver, cap.capabilities, cap.device_caps);

    if ((cap.capabilities & V4L2_CAP_VIDEO_OUTPUT) &&
        !(cap.capabilities & V4L2_CAP_VIDEO_M2M) &&
        !(cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE))
    {
        fprintf(stderr,
                "[PXP] device is output-only; this node cannot provide a capture buffer for memory conversion\n");
        errno = ENOTSUP;
        goto fail;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_M2M) &&
        !(cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE))
    {
        errno = ENODEV;
        perror("[PXP] not a M2M device");
        goto fail;
    }

    if (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)
    {
        fprintf(stderr, "[PXP] device is M2M_MPLANE, current single-planar path is not supported yet\n");
        errno = ENOTSUP;
        goto fail;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    if (ioctl(pxp->fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        perror("[PXP] VIDIOC_S_FMT OUTPUT");
        goto fail;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    if (ioctl(pxp->fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        perror("[PXP] VIDIOC_S_FMT CAPTURE");
        goto fail;
    }

    out_count = pxp_reqbufs(pxp->fd, V4L2_BUF_TYPE_VIDEO_OUTPUT, 1);
    if (out_count < 0)
    {
        perror("[PXP] VIDIOC_REQBUFS OUTPUT");
        goto fail;
    }
    cap_count = pxp_reqbufs(pxp->fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, 1);
    if (cap_count < 0)
    {
        perror("[PXP] VIDIOC_REQBUFS CAPTURE");
        goto fail;
    }

    pxp->out_buffers = calloc((size_t)out_count, sizeof(*pxp->out_buffers));
    pxp->cap_buffers = calloc((size_t)cap_count, sizeof(*pxp->cap_buffers));
    if (!pxp->out_buffers || !pxp->cap_buffers)
        goto fail;

    pxp->out_nbufs = (unsigned int)out_count;
    pxp->cap_nbufs = (unsigned int)cap_count;
    pxp->width = width;
    pxp->height = height;

    if (pxp_map_buffers(pxp->fd, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                        pxp->out_buffers, pxp->out_nbufs) < 0)
    {
        perror("[PXP] map OUTPUT buffers");
        goto fail;
    }
    if (pxp_map_buffers(pxp->fd, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                        pxp->cap_buffers, pxp->cap_nbufs) < 0)
    {
        perror("[PXP] map CAPTURE buffers");
        goto fail;
    }

    if (pxp_queue_capture_buffers(pxp->fd, pxp->cap_nbufs) < 0)
    {
        perror("[PXP] QBUF CAPTURE");
        goto fail;
    }

    if (pxp_stream_on(pxp->fd) < 0)
    {
        perror("[PXP] STREAMON");
        goto fail;
    }

    return 0;

fail:
    pxp_close(pxp);
    return -1;
}

int pxp_close(pxp_context_t *pxp)
{
    if (!pxp)
        return 0;

    if (pxp->fd >= 0)
        pxp_stream_off(pxp->fd);

    if (pxp->out_buffers)
    {
        pxp_unmap_buffers(pxp->out_buffers, pxp->out_nbufs);
        free(pxp->out_buffers);
    }
    if (pxp->cap_buffers)
    {
        pxp_unmap_buffers(pxp->cap_buffers, pxp->cap_nbufs);
        free(pxp->cap_buffers);
    }

    if (pxp->fd >= 0)
        close(pxp->fd);

    pxp_reset(pxp);
    return 0;
}

static int pxp_dqbuf(int fd, enum v4l2_buf_type type, unsigned int *index)
{
    struct v4l2_buffer buf;

    memset(&buf, 0, sizeof(buf));
    buf.type = type;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0)
        return -1;
    if (index)
        *index = buf.index;
    return 0;
}

static int pxp_qbuf_output(int fd, unsigned int index, size_t bytesused)
{
    struct v4l2_buffer buf;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    buf.bytesused = (unsigned int)bytesused;
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0)
        return -1;
    return 0;
}

static int pxp_qbuf_capture(int fd, unsigned int index)
{
    struct v4l2_buffer buf;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0)
        return -1;
    return 0;
}

int pxp_yuyv_to_rgb565_center(pxp_context_t *pxp,
                              const uint8_t *src, size_t src_size,
                              uint16_t *dst_buf, int dst_w, int dst_h,
                              int offset_x, int offset_y)
{
    unsigned int out_idx = 0;
    unsigned int cap_idx = 0;
    size_t row_bytes;
    size_t copy_bytes;
    const uint8_t *cap_src;
    uint16_t *dst_row;

    if (!pxp || pxp->fd < 0 || !src || !dst_buf)
    {
        errno = EINVAL;
        return -1;
    }

    if (pxp->out_nbufs == 0 || pxp->cap_nbufs == 0)
    {
        errno = EINVAL;
        return -1;
    }

    copy_bytes = src_size;
    if (copy_bytes > pxp->out_buffers[0].length)
        copy_bytes = pxp->out_buffers[0].length;
    memcpy(pxp->out_buffers[0].start, src, copy_bytes);

    if (pxp_qbuf_output(pxp->fd, 0, copy_bytes) < 0)
    {
        perror("[PXP] QBUF OUTPUT");
        return -1;
    }

    if (pxp_dqbuf(pxp->fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, &cap_idx) < 0)
    {
        perror("[PXP] DQBUF CAPTURE");
        return -1;
    }
    if (pxp_dqbuf(pxp->fd, V4L2_BUF_TYPE_VIDEO_OUTPUT, &out_idx) < 0)
    {
        perror("[PXP] DQBUF OUTPUT");
        return -1;
    }

    cap_src = (const uint8_t *)pxp->cap_buffers[cap_idx].start;
    row_bytes = (size_t)pxp->width * 2;
    for (int y = 0; y < pxp->height; y++)
    {
        int dst_y = offset_y + y;
        if (dst_y < 0 || dst_y >= dst_h)
            continue;
        dst_row = dst_buf + (size_t)dst_y * (size_t)dst_w + offset_x;
        memcpy(dst_row, cap_src + (size_t)y * row_bytes, row_bytes);
    }

    if (pxp_qbuf_capture(pxp->fd, cap_idx) < 0)
    {
        perror("[PXP] re-QBUF CAPTURE");
        return -1;
    }

    (void)out_idx;
    return 0;
}