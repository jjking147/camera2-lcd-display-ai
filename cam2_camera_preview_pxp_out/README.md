# Camera 项目 2：图像预览 + 图像处理

## 项目概述

在 i.MX6ULL + OV5640 平台上实现 **V4L2 实时预览 + 图像处理**。

- **采集**: V4L2 mmap, OV5640 YUYV 640×480, 4 个 DMA buffer
- **显示**: Framebuffer `/dev/fb0`, 1024×600, RGB565 (16bpp), 居中显示
- **处理**: 灰度化 / Sobel 边缘检测 (纯 C 实现)
- **架构**: 双线程 + ring buffer (8 帧缓冲) + 按键切换滤镜

## 优化架构说明

### 实现路径

- **原始 CPU 路径（基础版）**：摄像头采集 YUYV → CPU 软件 YUYV→RGB565 转换 → 拷贝到 Framebuffer → LCD 显示。
- **PXP 硬件加速路径（进阶版）**：摄像头采集 YUYV → CPU 图像处理（只修改 Y 平面，保持 YUYV 格式）→ 将处理后的 YUYV 送入 PXP 硬件做 YUYV→RGB565 转换并直接输出到 LCD。

### 迭代历程与项目痛点

1. **最初方案：纯 CPU 路径**
   - 采集线程 DQBUF 拿 YUYV，显示线程做 YUYV→RGB565 软件转换 + Sobel/灰度 → 写 FB。  
   - 功耗/发热显著：Cortex-A7 单核几乎被软件颜色转换占满，CPU 占用 ≈ 100%。  
   - 缺点：实时帧率受限，系统响应迟缓，不能增加更多处理逻辑。

2. **尝试 mem2mem PXP 加速**
   - 最初尝试通过 V4L2 mem2mem 方式用 PXP 获取转换后的 RGB565 结果，再写回 framebuffer。  
   - 失败原因：板上 `/dev/video0` 是输出型 PXP 节点，缺少 `V4L2_CAP_VIDEO_M2M`，无法在用户态读回转换结果，只能直接显示。

3. **最终方案：CPU 处理 + PXP 输出**
   - 在 CPU 上完成图像处理（灰度/Sobel/原样），保持 YUYV 输出；  
   - 将处理后的 YUYV 帧送入 PXP 硬件管线，由 PXP 做 YUYV→RGB565 并直接输出到 LCD；  
   - 去除了 CPU 侧的 YUYV→RGB565 软件转换和庞大的 Framebuffer memcpy，CPU 占用率降低了 **30%~40%**；  
   - 帧率保持稳定（≈28-30 FPS），系统响应显著改善，为未来叠加 OSD、增加更多滤镜留出 CPU 余量。

## 文件结构

```
cam2_camera_preview_pxp_out/
├── app/
│   ├── capture_display.c       # 主程序 (多线程 + PXP 输出)
│   ├── v4l2_utils.c/h          # V4L2 采集模块
│   ├── fb_utils.c/h            # Framebuffer 显示模块
│   ├── imgproc.c/h             # 图像处理模块 (含 YUYV 处理)
│   ├── key_input.c/h           # 按键模块 (input-event)
│   ├── pxp_output_utils.c/h    # PXP output-only 驱动接口
│   └── Makefile                # 交叉编译
├── scripts/
│   └── convert.sh              # YUV→JPG 辅助脚本
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
# 1. 确认设备
cat /sys/class/video4linux/video1/name    # 应显示 mx6s-csi

# 2. 运行 (自动尝试 PXP，失败回退 CPU)
./capture_display

# 按 GPIO 按键切换滤镜: NORMAL -> GRAY -> SOBEL
# Ctrl+C 退出
```

## 滤镜说明

| 按键次数 | 模式   | 说明                                 |
| -------- | ------ | ------------------------------------ |
| 0        | NORMAL | YUYV 原样 + PXP YUYV→RGB565 硬件显示 |
| 1        | GRAY   | YUYV Y 分量提取 + PXP 输出           |
| 2        | SOBEL  | 3×3 Sobel 边缘检测 + PXP 输出        |

若 PXP 不可用，程序自动回退到 CPU 路径 (YUYV→RGB565→FB)。

## 技术要点

- **V4L2 mmap**: 4 个 DMA buffer, 零拷贝采集
- **PXP output-only**: 将处理后的 YUYV 送往 PXP 硬件管线，由 PXP 做颜色空间转换并直连 LCD
- **CPU 仅做 YUYV 处理**: 硬件颜色转换省去软件 YUYV→RGB565，CPU 占用降低 30%~40%
- **Ring Buffer**: 8 帧环形缓冲, 采集/处理解耦, 避免丢帧
- **双线程**: 采集线程 + 处理显示线程, 并行工作
- **Sobel 3×3**: 纯 C 手写卷积, 体现算法能力
- **按键去抖**: 50ms 软件去抖
- **FPS 统计**: 每 100 帧输出实时帧率

## 调优方向 (加分项)

- NEON 指令优化 YUYV 处理
- 自适应对比度拉伸 (CLAHE)
- OSD 叠加 (显示 FPS / 滤镜名称 / CPU 占用)
- 触摸屏选取 ROI 区域

## 交叉编译工具链路径

```
arm-linux-gnueabihf-gcc
```
