/*
 * key_input.h - 按键检测模块
 * 通过 /dev/input/eventX 读取 gpio-keys 事件
 */

#ifndef KEY_INPUT_H
#define KEY_INPUT_H

/* 初始化按键 (/dev/input/eventX) */
int key_init(const char *input_dev);

/* 非阻塞检测按键是否按下，1=按下，0=未按下 */
int key_pressed(int fd);

/* 释放 */
void key_close(int fd);

#endif
