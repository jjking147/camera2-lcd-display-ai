/*
 * v4l2_utils.c - V4L2 mmap 采集实现
 */

#include "v4l2_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

int v4l2_camera_init(v4l2_camera_t *cam,
                     const char *dev,
                     int width, int height,
                     unsigned int pixfmt,
                     unsigned int nbufs)
{
    int fd;
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;

    if (!cam || !dev)
        return -1;
    memset(cam, 0, sizeof(*cam));

    fd = open(dev, O_RDWR, 0);
    if (fd < 0)
    {
        perror("open camera");
        return -1;
    }
    cam->fd = fd;

    /* 查询设备能力 */
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
    {
        perror("VIDIOC_QUERYCAP");
        goto err;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        fprintf(stderr, "%s is not a capture device\n", dev);
        goto err;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING))
    {
        fprintf(stderr, "%s does not support streaming\n", dev);
        goto err;
    }

    /* 设置格式 */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixfmt;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;    // 任意场，一般是行扫描
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) // 又是尝试设置格式了
    {
        perror("VIDIOC_S_FMT");
        goto err;
    }
    // 最终匹配格式
    cam->width = fmt.fmt.pix.width;
    cam->height = fmt.fmt.pix.height;
    printf("[V4L2] %s: %dx%d, pixelformat=0x%x (%c%c%c%c)\n",
           dev, cam->width, cam->height,
           fmt.fmt.pix.pixelformat,
           (fmt.fmt.pix.pixelformat >> 0) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 24) & 0xFF);

    /* 申请 buffer */
    memset(&req, 0, sizeof(req));
    req.count = nbufs;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) // 也是一个匹配过程，会赋值真实的缓冲区个数
    {
        perror("VIDIOC_REQBUFS");
        goto err;
    }
    cam->nbufs = req.count; // 这里是真实值
    printf("[V4L2] requested %u buffers, got %u\n", nbufs, cam->nbufs);

    cam->buffers = calloc(cam->nbufs, sizeof(*cam->buffers)); // 先创建好放内存的大小和偏移量的空间
    if (!cam->buffers)
        goto err;
    // 申请buf并且保存好每个buf的大小和偏移量
    for (unsigned int i = 0; i < cam->nbufs; i++)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            perror("VIDIOC_QUERYBUF");
            goto err;
        }
        cam->buffers[i].length = buf.length;
        cam->buffers[i].start = mmap(NULL, buf.length,
                                     PROT_READ | PROT_WRITE,
                                     MAP_SHARED, fd, buf.m.offset);
        if (cam->buffers[i].start == MAP_FAILED)
        {
            perror("mmap");
            goto err;
        }
    }
    return 0;

err:
    v4l2_camera_close(cam);
    return -1;
}

int v4l2_camera_start(v4l2_camera_t *cam)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    // 入队之前分配好的空闲buf
    for (unsigned int i = 0; i < cam->nbufs; i++)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(cam->fd, VIDIOC_QBUF, &buf) < 0)
        {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }
    // 入队完了之后就开始输入视频流
    if (ioctl(cam->fd, VIDIOC_STREAMON, &type) < 0)
    {
        perror("VIDIOC_STREAMON");
        return -1;
    }
    printf("[V4L2] streaming started\n");
    return 0;
}

int v4l2_camera_stop(v4l2_camera_t *cam)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cam->fd, VIDIOC_STREAMOFF, &type) < 0)
    {
        perror("VIDIOC_STREAMOFF");
        return -1;
    }
    printf("[V4L2] streaming stopped\n");
    return 0;
}

// 做一些unmap的工作
void v4l2_camera_close(v4l2_camera_t *cam)
{
    if (!cam || cam->fd < 0)
        return;
    v4l2_camera_stop(cam);
    if (cam->buffers)
    {
        for (unsigned int i = 0; i < cam->nbufs; i++)
        {
            if (cam->buffers[i].start && cam->buffers[i].start != MAP_FAILED)
                munmap(cam->buffers[i].start, cam->buffers[i].length);
        }
        free(cam->buffers);
        cam->buffers = NULL;
    }
    close(cam->fd);
    cam->fd = -1;
    printf("[V4L2] closed\n");
}

int v4l2_camera_get_frame(v4l2_camera_t *cam, void **data, size_t *size) // 把某个可读的缓冲区的地址的大小拿到手，并且返回索引
{
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cam->fd, VIDIOC_DQBUF, &buf) < 0) // 把可读的buf信息记录下来
    {
        perror("VIDIOC_DQBUF");
        return -1;
    }
    // 把data指向目前可读的缓冲区buf的地址,把size指向这个缓冲区的大小
    *data = cam->buffers[buf.index].start;
    *size = buf.bytesused;
    /* 把 index 存到 data 前一个 uint32_t (小 hack，方便 put_frame) */
    /* 更干净的做法：单独维护一个当前 index，这里在调用者侧记录 */
    /* 我们用一个静态变量记录最后一个 dequeue 的 index */
    return (int)buf.index;
}

int v4l2_camera_put_frame(v4l2_camera_t *cam, int index) // 把这个index的缓冲区还给内核
{
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    if (ioctl(cam->fd, VIDIOC_QBUF, &buf) < 0)
    {
        perror("VIDIOC_QBUF");
        return -1;
    }
    return 0;
}
