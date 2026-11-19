/* Use the SDK's display UAPI with the kernel that owns the HDMI transmitter. */
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "drv_hdmi_ioctl.h"
#include "drv_disp_ioctl.h"

int main(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "auto") && strcmp(argv[1], "probe") &&
                      strcmp(argv[1], "1440") && strcmp(argv[1], "1080"))) return 2;
    int disp = open("/dev/hi_disp", O_RDWR);
    int hdmi = open("/dev/hi_hdmi", O_RDWR);
    DISP_FORMAT_S mode = {.enDisp = HI_DRV_DISPLAY_1};
    if (disp < 0 || ioctl(disp, CMD_DISP_GET_FORMAT, &mode) < 0) {
        perror("display get format");
        return 1;
    }
    HDMI_SINK_CAPABILITY_S cap = {.enHdmi = HI_UNF_HDMI_ID_0};
    int ok = hdmi >= 0 && ioctl(hdmi, CMD_HDMI_SINK_CAPABILITY, &cap) == 0;
    int qhd = ok && cap.SinkCap.bSupportFormat[HI_UNF_ENC_FMT_VESA_2560X1440_60_RB];
    fprintf(stderr, "sink capability valid=%d native=%d QHD60RB=%d active=%d\n",
            ok, cap.SinkCap.enNativeFormat, qhd, mode.enFormat);
    if (!strcmp(argv[1], "probe")) return 0;
    int use_qhd = !strcmp(argv[1], "1440") || (!strcmp(argv[1], "auto") && qhd);
    HI_DRV_DISP_FMT_E want = use_qhd ? HI_DRV_DISP_FMT_VESA_2560X1440_60_RB : HI_DRV_DISP_FMT_1080P_60;
    if (mode.enFormat != want) {
        mode.enFormat = want;
        mode.enStereo = DISP_STEREO_NONE;
        if (ioctl(disp, CMD_DISP_SET_FORMAT, &mode) != 0) { perror("display set format"); return 1; }
    }
    if (ioctl(disp, CMD_DISP_GET_FORMAT, &mode) || mode.enFormat != want) {
        fprintf(stderr, "display format read-back mismatch\n");
        return 1;
    }
    DISP_VIRTSCREEN_S v = {.enDisp = HI_DRV_DISPLAY_1};
    if (ioctl(disp, CMD_DISP_GET_VIRTSCREEN, &v) || v.stVirtScreen.s32Width != 1920 || v.stVirtScreen.s32Height != 1080) {
        v.stVirtScreen.s32X = v.stVirtScreen.s32Y = 0;
        v.stVirtScreen.s32Width = 1920;
        v.stVirtScreen.s32Height = 1080;
        if (ioctl(disp, CMD_DISP_SET_VIRTSCREEN, &v)) { perror("display virtual screen"); return 1; }
    }
    printf("HDMI %d x %d, desktop 1920 x 1080\n", use_qhd ? 2560 : 1920, use_qhd ? 1440 : 1080);
    return 0;
}
