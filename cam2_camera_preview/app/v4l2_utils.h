/*
 * v4l2_utils.h - V4L2 采集模块
 * OV5640 YUYV 640x480, mmap 4 buffers
 */

#ifndef V4L2_UTILS_H
#define V4L2_UTILS_H

#include <sys/types.h>
#include <linux/videodev2.h>

/* 默认采集参数 */
#define CAM_DEV "/dev/video1"
#define CAM_WIDTH 640
#define CAM_HEIGHT 480
#define CAM_PIXFMT V4L2_PIX_FMT_YUYV
#define CAM_NBUFS 4

/* V4L2 buffer 封装 */
struct v4l2_buffer_wrap
{
    void *start;
    size_t length;
};

/* 采集器句柄 */
typedef struct
{
    int fd;
    struct v4l2_buffer_wrap *buffers;
    unsigned int nbufs;
    int width;
    int height;
} v4l2_camera_t;

/* 初始化摄像头：open / query / set fmt / reqbufs / mmap / streamon */
int v4l2_camera_init(v4l2_camera_t *cam,
                     const char *dev,
                     int width, int height,
                     unsigned int pixfmt,
                     unsigned int nbufs);

/* 启动采集 */
int v4l2_camera_start(v4l2_camera_t *cam);

/* 停止采集 */
int v4l2_camera_stop(v4l2_camera_t *cam);

/* 释放资源 */
void v4l2_camera_close(v4l2_camera_t *cam);

/* 获取一帧 (阻塞) */
int v4l2_camera_get_frame(v4l2_camera_t *cam, void **data, size_t *size);

/* 归还 buffer */
int v4l2_camera_put_frame(v4l2_camera_t *cam, int index);

#endif
