/*
 * key_input.h - 按键检测模块
 * 用于切换滤镜模式
 */

#ifndef KEY_INPUT_H
#define KEY_INPUT_H

/* 初始化按键 (GPIO 输入, 用 /sys/class/gpio) */
int key_init(int gpio_num);

/* 非阻塞检测按键是否按下 (检测下降沿) */
int key_pressed(int gpio_num);

/* 释放 */
void key_close(int gpio_num);

#endif
