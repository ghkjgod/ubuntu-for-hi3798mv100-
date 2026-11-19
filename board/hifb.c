/* Keep the HIFB graphics plane alive while Xorg owns its framebuffer. */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "hifb.h"

static void checked(int result, const char *operation)
{
    if (result < 0) {
        perror(operation);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "1080") && strcmp(argv[1], "1440"))) {
        fprintf(stderr, "usage: hifb-plane 1080|1440\n");
        return 2;
    }
    const unsigned width = !strcmp(argv[1], "1440") ? 2560 : 1920;
    const unsigned height = width == 2560 ? 1440 : 1080;
    const int fd = open("/dev/fb0", O_RDWR);
    checked(fd, "/dev/fb0");
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo v;
    checked(ioctl(fd, FBIOGET_FSCREENINFO, &fix), "FBIOGET_FSCREENINFO");
    checked(ioctl(fd, FBIOGET_VSCREENINFO, &v), "FBIOGET_VSCREENINFO");
    fprintf(stderr, "HIFB %.16s %ux%u-%u stride=%u bytes=%u\n",
            fix.id, v.xres, v.yres, v.bits_per_pixel, fix.line_length, fix.smem_len);
    if (fix.smem_len && (unsigned long long)width * height * 4 > fix.smem_len) {
        fprintf(stderr, "Insufficient scanout memory for a 32-bit frame\n");
        return 1;
    }
    v.xres = v.xres_virtual = width;
    v.yres = v.yres_virtual = height;
    v.xoffset = v.yoffset = 0;
    v.bits_per_pixel = 32;
    v.red = (struct fb_bitfield){16,8,0};
    v.green = (struct fb_bitfield){8,8,0};
    v.blue = (struct fb_bitfield){0,8,0};
    v.transp = (struct fb_bitfield){24,8,0};
    v.activate = FB_ACTIVATE_NOW;
    checked(ioctl(fd, FBIOPUT_VSCREENINFO, &v), "FBIOPUT_VSCREENINFO");
    HIFB_ALPHA_S alpha = {0};
    checked(ioctl(fd, FBIOGET_ALPHA_HIFB, &alpha), "get layer alpha");
    alpha.bAlphaEnable = HI_TRUE;
    alpha.bAlphaChannel = HI_FALSE;
    alpha.u8GlobalAlpha = 255;
    checked(ioctl(fd, FBIOPUT_ALPHA_HIFB, &alpha), "set layer alpha");
    HI_BOOL show = HI_TRUE;
    checked(ioctl(fd, FBIOPUT_SHOW_HIFB, &show), "show graphics plane");
    checked(ioctl(fd, FBIOGET_VSCREENINFO, &v), "read active geometry");
    checked(ioctl(fd, FBIOGET_FSCREENINFO, &fix), "read active stride");
    fprintf(stderr, "Active HIFB %ux%u-%u stride=%u bytes=%u\n",
            v.xres, v.yres, v.bits_per_pixel, fix.line_length, fix.smem_len);
    if (v.xres != width || v.yres != height || v.bits_per_pixel != 32)
        return 1;
    for (;;) pause();
}
