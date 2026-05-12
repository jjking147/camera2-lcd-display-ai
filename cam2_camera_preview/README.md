# Camera 项目 2：图像预览 + 图像处理

## 项目概述

在 i.MX6ULL + OV5640 平台上实现 **V4L2 实时预览 + 图像处理**。

- **采集**: V4L2 mmap, OV5640 YUYV 640×480, 4 个 DMA buffer
- **显示**: Framebuffer `/dev/fb0`, 1024×600, RGB565 (16bpp), 居中显示
- **处理**: 灰度化 / Sobel 边缘检测 (纯 C 实现)
- **架构**: 双线程 + ring buffer (8 帧缓冲) + 按键切换滤镜

## 文件结构

```
cam2_camera_preview/
├── app/
│   ├── capture_display.c    # 主程序 (多线程架构)
│   ├── v4l2_utils.c/h       # V4L2 采集模块
│   ├── fb_utils.c/h         # Framebuffer 显示模块
│   ├── imgproc.c/h          # 图像处理模块
│   ├── key_input.c/h        # GPIO 按键模块
│   └── Makefile             # 交叉编译
├── scripts/
│   └── convert.sh           # YUV→JPG 辅助脚本
├── vscode/
│   └── c_cpp_properties.json
└── README.md
```

## 编译

```bash
# 开发机上
cd app
make clean && make
```

工具链: `arm-linux-gnueabihf-gcc`

## 板上运行

```bash
# 1. 加载驱动
modprobe ov5640_camera
modprobe ov5640_camera_int

# 2. 确认设备
cat /sys/class/video4linux/video1/name    # 应显示 mx6s-csi

# 3. 配置 Framebuffer 为 16bpp
fbset -depth 16
# 或 echo 16 > /sys/class/graphics/fb0/bits_per_pixel

# 4. 运行
./capture_display
# 按 GPIO0 按键切换滤镜: NORMAL -> GRAY -> SOBEL -> NORMAL
# Ctrl+C 退出
```

## 滤镜说明

| 按键次数 | 模式   | 说明                    |
| -------- | ------ | ----------------------- |
| 0        | NORMAL | YUYV → RGB565 彩色预览  |
| 1        | GRAY   | 仅提取 Y 分量, 灰度显示 |
| 2        | SOBEL  | 3×3 Sobel 边缘检测      |

## 技术要点

- **V4L2 mmap**: 4 个 DMA buffer, 零拷贝采集
- **Framebuffer RGB565**: YUYV(16bit) → RGB565(16bit) 直接转换, 无额外开销
- **Ring Buffer**: 8 帧环形缓冲, 采集/处理解耦, 避免丢帧
- **双线程**: 采集线程 + 处理显示线程, 并行工作
- **Sobel 3×3**: 纯 C 手写卷积, 体现算法能力
- **按键去抖**: 50ms 软件去抖
- **FPS 统计**: 每 100 帧输出实时帧率

## 调优方向 (加分项)

- NEON 指令优化 YUYV→RGB565 转换
- 自适应对比度拉伸 (CLAHE)
- OSD 叠加 (显示 FPS / 滤镜名称)
- 触摸屏选取 ROI 区域

## 交叉编译工具链路径

```
arm-linux-gnueabihf-gcc
```
