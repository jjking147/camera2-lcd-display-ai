/*
 * capture_display.c - 主程序
 * V4L2 实时预览 + 图像处理 + 按键切换滤镜
 *
 * 架构:
 *   采集线程: V4L2 dequeue → 拷贝到 ring buffer → enqueue
 *   处理显示线程: 从 ring buffer 取帧 → 图像处理 → 写 fb backbuf → flush
 *   按键: 非阻塞轮询切换滤镜模式
 *
 * 编译:
 *   arm-linux-gnueabihf-gcc -O2 -pthread -o capture_display *.c
 */

#include "v4l2_utils.h"
#include "fb_utils.h"
#include "imgproc.h"
#include "key_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

/* ========== 参数配置 ========== */
#define CAPTURE_W 640
#define CAPTURE_H 480
#define DISPLAY_W 1024
#define DISPLAY_H 600
#define KEY_INPUT_DEV "/dev/input/event2" /* gpio-keys 设备节点，按需修改 */
#define RING_SIZE 8                       /* ring buffer 帧数 */

/* ========== Ring Buffer ========== */
typedef struct
{
    uint8_t *data[RING_SIZE];
    size_t size[RING_SIZE];
    int head; /* 采集线程写入位置 */
    int tail; /* 处理线程读取位置 */
    int count;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} ring_buffer_t;

static ring_buffer_t ring;

static int ring_init(ring_buffer_t *rb)
{
    memset(rb, 0, sizeof(*rb));
    for (int i = 0; i < RING_SIZE; i++)
    {
        rb->data[i] = malloc(CAPTURE_W * CAPTURE_H * 2);
        if (!rb->data[i])
            return -1;
    }
    pthread_mutex_init(&rb->lock, NULL);
    pthread_cond_init(&rb->cond, NULL);
    return 0;
}

static void ring_free(ring_buffer_t *rb)
{
    for (int i = 0; i < RING_SIZE; i++)
    {
        free(rb->data[i]);
        rb->data[i] = NULL;
    }
    pthread_mutex_destroy(&rb->lock);
    pthread_cond_destroy(&rb->cond);
}

static int ring_put(ring_buffer_t *rb, const void *data, size_t size)
{
    pthread_mutex_lock(&rb->lock);
    if (rb->count >= RING_SIZE)
    {
        /* discard oldest */
        rb->tail = (rb->tail + 1) % RING_SIZE;
        rb->count--;
    }
    memcpy(rb->data[rb->head], data, size);
    rb->size[rb->head] = size;
    rb->head = (rb->head + 1) % RING_SIZE;
    rb->count++;
    pthread_cond_signal(&rb->cond);
    pthread_mutex_unlock(&rb->lock);
    return 0;
}

static int ring_get(ring_buffer_t *rb, void *data, size_t *size)
{
    pthread_mutex_lock(&rb->lock);
    while (rb->count == 0)
    {
        pthread_cond_wait(&rb->cond, &rb->lock);
    }
    memcpy(data, rb->data[rb->tail], rb->size[rb->tail]);
    *size = rb->size[rb->tail];
    rb->tail = (rb->tail + 1) % RING_SIZE;
    rb->count--;
    pthread_mutex_unlock(&rb->lock);
    return 0;
}

/* ========== 全局状态 ========== */
static volatile int quit = 0;
static volatile int filter_mode = FILTER_NORMAL;

/* ========== 信号处理 ========== */
static void sig_handler(int sig)
{
    (void)sig;
    quit = 1;
}

/* ========== 采集线程 ========== */
static void *capture_thread(void *arg)
{
    v4l2_camera_t *cam = (v4l2_camera_t *)arg;
    int frame_count = 0;

    while (!quit)
    {
        void *data = NULL;
        size_t size = 0;
        int idx = v4l2_camera_get_frame(cam, &data, &size);
        if (idx < 0)
        {
            if (quit)
                break;
            usleep(10000);
            continue;
        }

        /* 拷贝到 ring buffer */
        ring_put(&ring, data, size);

        /* 归还 V4L2 buffer */
        v4l2_camera_put_frame(cam, idx);

        frame_count++;
        if (frame_count % 100 == 0)
            printf("[CAP] %d frames\n", frame_count);
    }
    printf("[CAP] thread exit, total %d frames\n", frame_count);
    return NULL;
}

/* ========== 处理 + 显示线程 ========== */
static void *display_thread(void *arg)
{
    fb_context_t *fb = (fb_context_t *)arg;
    uint8_t *frame = malloc(CAPTURE_W * CAPTURE_H * 2);
    if (!frame)
        return NULL;
    size_t frame_size;

    int offset_x = (DISPLAY_W - CAPTURE_W) / 2;
    int offset_y = (DISPLAY_H - CAPTURE_H) / 2;

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    int display_count = 0;
    int last_filter = filter_mode;

    printf("[DISP] offset=(%d,%d), thread start\n", offset_x, offset_y);

    while (!quit)
    {
        ring_get(&ring, frame, &frame_size);
        (void)frame_size;

        /* 每帧前清空绘制区域 (避免残留) */
        for (int y = offset_y; y < offset_y + CAPTURE_H && y < DISPLAY_H; y++)
        {
            memset(&fb->backbuf[y * fb->width + offset_x], 0, CAPTURE_W * 2);
        }

        int mode = filter_mode;

        switch (mode)
        {
        case FILTER_NORMAL:
            yuyv_to_rgb565_center(frame, CAPTURE_W, CAPTURE_H,
                                  fb->backbuf, fb->width, fb->height,
                                  offset_x, offset_y);
            break;
        case FILTER_GRAY:
            yuyv_to_gray_center(frame, CAPTURE_W, CAPTURE_H,
                                fb->backbuf, fb->width, fb->height,
                                offset_x, offset_y);
            break;
        case FILTER_SOBEL:
            yuyv_to_sobel_center(frame, CAPTURE_W, CAPTURE_H,
                                 fb->backbuf, fb->width, fb->height,
                                 offset_x, offset_y);
            break;
        default:
            break;
        }

        if (mode != last_filter)
        {
            printf("[DISP] filter switched: %d → %d\n", last_filter, mode);
            last_filter = mode;
        }

        fb_flush(fb);
        display_count++;

        /* 每 100 帧输出 FPS */
        if (display_count % 100 == 0)
        {
            clock_gettime(CLOCK_MONOTONIC, &ts_end);
            double elapsed = (ts_end.tv_sec - ts_start.tv_sec) +
                             (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9;
            printf("[DISP] %d frames, %.1f FPS\n", display_count,
                   display_count / elapsed);
        }
    }

    free(frame);
    printf("[DISP] thread exit, %d displayed\n", display_count);
    return NULL;
}

/* ========== 按键轮询 (在主线程) ========== */
static void key_poll_loop(int fd)
{
    static int prev = 0;
    while (!quit)
    {
        int now = key_pressed(fd);
        if (now && !prev)
        {
            filter_mode = (filter_mode + 1) % FILTER_COUNT;
            const char *names[] = {"NORMAL", "GRAY", "SOBEL"};
            printf("[KEY] filter => %s\n", names[filter_mode]);
        }
        prev = now;
        usleep(50000); /* 50ms 去抖 */
    }
}

/* ========== 主函数 ========== */
int main(void)
{
    v4l2_camera_t cam;
    fb_context_t fb;
    pthread_t cap_tid, disp_tid;
    int key_fd = -1;

    printf("========================================\n");
    printf("  i.MX6ULL Camera Preview + ImageProc\n");
    printf("  Capture: %dx%d YUYV\n", CAPTURE_W, CAPTURE_H);
    printf("  Display: %dx%d FB center\n", DISPLAY_W, DISPLAY_H);
    printf("========================================\n");

    /* 信号 */
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* 初始化 */
    if (fb_init(&fb, "/dev/fb0", 16) < 0)
    {
        fprintf(stderr, "FB init failed\n");
        return 1;
    }

    if (v4l2_camera_init(&cam, CAM_DEV, CAPTURE_W, CAPTURE_H,
                         CAM_PIXFMT, CAM_NBUFS) < 0)
    {
        fprintf(stderr, "Camera init failed\n");
        fb_close(&fb);
        return 1;
    }

    if (ring_init(&ring) < 0)
    {
        fprintf(stderr, "Ring buffer init failed\n");
        v4l2_camera_close(&cam);
        fb_close(&fb);
        return 1;
    }

    /* 启动按键 */
    key_fd = key_init(KEY_INPUT_DEV);
    if (key_fd < 0)
        fprintf(stderr, "[KEY] open %s failed, key disabled\n", KEY_INPUT_DEV);

    /* 启动采集 */
    v4l2_camera_start(&cam);

    /* 创建线程 */
    pthread_create(&cap_tid, NULL, capture_thread, &cam);
    pthread_create(&disp_tid, NULL, display_thread, &fb);

    /* 主线程处理按键 */
    printf("[MAIN] Press key on %s to switch filter. Ctrl+C to exit.\n", KEY_INPUT_DEV);
    printf("[MAIN] Filters: 0=NORMAL 1=GRAY 2=SOBEL\n");
    if (key_fd >= 0)
        key_poll_loop(key_fd);

    /* 等待线程退出 */
    pthread_join(cap_tid, NULL);
    pthread_join(disp_tid, NULL);

    /* 清理 */
    key_close(key_fd);
    v4l2_camera_close(&cam);
    ring_free(&ring);
    fb_close(&fb);

    printf("[MAIN] exit cleanly.\n");
    return 0;
}
