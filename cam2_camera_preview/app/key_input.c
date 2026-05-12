/*
 * key_input.c - GPIO 按键检测实现
 * 通过 /sys/class/gpio 操作
 */

#include "key_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* 导出 GPIO */
static int gpio_export(int gpio)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/gpio/export");
    int fd = open(path, O_WRONLY);
    if (fd < 0)
    {
        perror("gpio export");
        return -1;
    }
    char num[16];
    int len = snprintf(num, sizeof(num), "%d", gpio);
    if (write(fd, num, len) < 0)
    {
        /* 可能已经导出 */
    }
    close(fd);
    return 0;
}

static int gpio_unexport(int gpio)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/gpio/unexport");
    int fd = open(path, O_WRONLY);
    if (fd < 0)
        return -1;
    char num[16];
    int len = snprintf(num, sizeof(num), "%d", gpio);
    write(fd, num, len);
    close(fd);
    return 0;
}

static int gpio_set_direction(int gpio, const char *dir)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0)
    {
        perror("gpio_set_direction");
        return -1;
    }
    write(fd, dir, strlen(dir));
    close(fd);
    return 0;
}

int key_init(int gpio_num)
{
    gpio_export(gpio_num);
    usleep(100000); /* 等 sysfs 创建 */
    gpio_set_direction(gpio_num, "in");
    printf("[KEY] GPIO %d initialized\n", gpio_num);
    return 0;
}

int key_pressed(int gpio_num)
{
    char path[128];
    char val = '1';
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio_num);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    if (read(fd, &val, 1) < 1)
    {
        close(fd);
        return 0;
    }
    close(fd);
    /* 按下 = '0' (低电平), 弹起 = '1' */
    return (val == '0');
}

void key_close(int gpio_num)
{
    gpio_unexport(gpio_num);
    printf("[KEY] GPIO %d released\n", gpio_num);
}
