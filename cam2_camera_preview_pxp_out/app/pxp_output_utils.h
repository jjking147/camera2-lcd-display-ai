/*
 * pxp_output_utils.h - PXP output-only 显示模块
 * 只使用 VIDEO_OUTPUT 队列，将 RGB565 数据送到 PXP，由 PXP DMA 输出到 LCD。
 * PXP 不做色彩空间转换，调用者需自行将 YUYV 转为 RGB565。
 */

#ifndef PXP_OUTPUT_UTILS_H
#define PXP_OUTPUT_UTILS_H

#include <stdint.h>
#include <sys/types.h>

#include "v4l2_utils.h"

typedef struct
{
    int fd;
    struct v4l2_buffer_wrap *out_buffers;
    unsigned int out_nbufs;
    int width;
    int height;
    int bytesperline;
    size_t sizeimage;
    unsigned int next_index;
    unsigned int queued;
    int streaming;
} pxp_output_t;

int pxp_output_init(pxp_output_t *pxp, const char *dev, int width, int height);
int pxp_output_close(pxp_output_t *pxp);
int pxp_output_put_frame(pxp_output_t *pxp, const uint8_t *rgb565, int width, int height);

#endif