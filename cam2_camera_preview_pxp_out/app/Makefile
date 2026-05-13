# Makefile - i.MX6ULL 交叉编译
# arm-linux-gnueabihf-gcc -O2 -pthread

CROSS   = /opt/toolchains/arm-linux-gnueabihf-gcc-4.9.4/bin/arm-linux-gnueabihf-
CC      = $(CROSS)gcc
CFLAGS  = -O2 -Wall -pthread -std=gnu99 -D_GNU_SOURCE
LDFLAGS = -pthread

SRCS    = capture_display.c v4l2_utils.c fb_utils.c imgproc.c key_input.c pxp_output_utils.c
OBJS    = $(SRCS:.c=.o)
TARGET  = capture_display

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)
