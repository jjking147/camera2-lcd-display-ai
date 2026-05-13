/*
 * pxp_utils.h - i.MX6ULL PXP 颜色转换模块
 *
 * 提供一个同步的 YUYV -> RGB565 转换接口，内部通过 V4L2 mem2mem
 * 设备完成硬件转换，外部只需要传入源帧和目标 framebuffer backbuf。
 */

#ifndef PXP_UTILS_H
#define PXP_UTILS_H

#include <stdint.h>
#include <sys/types.h>

#include "v4l2_utils.h"

typedef struct
{
    int fd;
    struct v4l2_buffer_wrap *out_buffers;
    struct v4l2_buffer_wrap *cap_buffers;
    unsigned int out_nbufs;
    unsigned int cap_nbufs;
    int width;
    int height;
} pxp_context_t;

int pxp_init(pxp_context_t *pxp, const char *dev, int width, int height);
int pxp_close(pxp_context_t *pxp);

int pxp_yuyv_to_rgb565_center(pxp_context_t *pxp,
                              const uint8_t *src, size_t src_size,
                              uint16_t *dst_buf, int dst_w, int dst_h,
                              int offset_x, int offset_y);

#endif