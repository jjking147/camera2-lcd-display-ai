#!/bin/bash
# convert.sh - 辅助脚本: 将 YUYV 原始帧转为 PNG/JPG
# 用法: ./convert.sh input.yuv output.jpg 640 480
# 需要: ffmpeg 或 python3 + pillow

if [ $# -lt 2 ]; then
    echo "Usage: $0 <input.yuv> <output.jpg> [width=640] [height=480]"
    exit 1
fi

INPUT="$1"
OUTPUT="$2"
WIDTH="${3:-640}"
HEIGHT="${4:-480}"

# 用 ffmpeg
if command -v ffmpeg &> /dev/null; then
    ffmpeg -f rawvideo -pix_fmt yuyv422 -s ${WIDTH}x${HEIGHT} \
           -i "$INPUT" -frames:v 1 "$OUTPUT"
    echo "Saved: $OUTPUT"
else
    echo "ffmpeg not found, falling back to python..."
    python3 -c "
import sys, struct
from PIL import Image

w, h = $WIDTH, $HEIGHT
data = open('$INPUT', 'rb').read()
img = Image.frombytes('YCbCr', (w, h), data, 'raw', 'YUYV')
img = img.convert('RGB')
img.save('$OUTPUT')
print('Saved:', '$OUTPUT')
"
fi
