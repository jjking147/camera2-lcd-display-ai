/*
 * key_input.c - 按键检测实现
 * 通过 /dev/input/eventX 读取 gpio-keys 事件 (非阻塞)
 */

#include "key_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

int key_init(const char *input_dev)
{
    int fd = open(input_dev, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
    {
        perror("open input device");
        return -1;
    }
    printf("[KEY] %s opened\n", input_dev);
    return fd;
}

int key_pressed(int fd)
{
    struct input_event ev;
    int pressed = 0;
    while (1)
    {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n < (ssize_t)sizeof(ev))
            break;
        if (ev.type == EV_KEY && ev.code == KEY_VOLUMEDOWN)
        {
            pressed = ev.value; /* 1=按下, 0=释放 */
        }
    }
    return pressed;
}

void key_close(int fd)
{
    if (fd >= 0)
    {
        close(fd);
        printf("[KEY] closed\n");
    }
}
