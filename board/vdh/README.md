# Hi3798MV100 VDH 视频解码

EC6108V9C 的 VDH V300R001 可以在本项目 Linux 6.12.114 armhf 上通过 V4L2 Request API 解码本地视频。安装后在桌面打开视频文件，或运行 `histb-play "/srv/tf/影片.mp4"`。播放器先检查视频流，再选择 VDH 或 FFmpeg。MP4、MKV、WebM 是容器，实际解码方式取决于其中的视频编码。

| 视频流 | 在这台盒子上的播放路径 |
| --- | --- |
| H.264 Baseline、Main、High；H.265/HEVC Main | 已验证 8 位 4:2:0、逐行、每帧单 slice、I/P 帧、单个短期参考帧。当前编码尺寸上限 1920×1088，含可见画面 1920×1080。 |
| B 帧、多参考帧、10 位、超出尺寸、加权预测等 | 能从流信息辨认的直接交给 FFplay。解码过程中 VDH 拒绝的流由 FFplay 从头重播。HEVC tiles、WPP、自定义缩放矩阵也走软件。 |
| MPEG-1/2、MPEG-4 Part 2、VC-1、VP6/VP8、AVS/AVS+、MVC、H.263 基线 | 芯片资料列有这些格式。本模块尚无相应 V4L2 硬件后端，本地播放通过 FFmpeg，具体 profile 取决于已安装的 FFmpeg。 |
| VP9、AV1 | 海思规格表未列为硬解格式。已用两段 WebM 在盒子上验证 FFplay 软件播放。 |

芯片资料给出的整体上限是 4K×2K@30。当前模块只开放上表中通过验证的 H.264 和 HEVC 子集，4K 视频交给软件，流畅度取决于码流和 CPU。HDMI 输出模式独立于视频解码尺寸。VDH 负责解码，CPU 将其分块图像整理为线性 NV12，Mali450 负责桌面图形。[海思规格表，第 1 页](https://www.silicondevice.com/file.upload/images/Gid1563Pdf_Hi3798MV100.pdf)

## SSH 安装与回退

这是给现有 6.12.114 桌面的独立更新。以前下载的根文件系统和内核归档需另行安装此目录。关闭正在使用 VDH 的播放器，在管理电脑上将仓库中的目录上传。以下以 Linux 管理端为例，`BOX` 填已核对主机公钥的 SSH 地址或别名。

```sh
BOX=root@盒子的局域网IP
ssh "$BOX" 'uname -r'
git archive --format=tar.gz --output=mv100-vdh.tar.gz HEAD board/vdh
scp mv100-vdh.tar.gz "$BOX":/tmp/
ssh "$BOX" 'mkdir -p /root/histb-updates && tar xzf /tmp/mv100-vdh.tar.gz -C /root/histb-updates && bash /root/histb-updates/board/vdh/install.sh'
```

安装器校验板型、内核 ABI、模块 SHA-256 和两张 CABAC 初始化表。默认按固定版本和散列下载表，再通过盒子已配置的签名 Ubuntu 软件源安装 FFmpeg 和 GStreamer 依赖。受限网络可在管理电脑执行 `python3 board/vdh/firmware.py tables`，用 SCP 上传该目录，在盒子运行 `bash /root/histb-updates/board/vdh/install.sh --from-dir /上传路径/tables`。离线环境另需提前准备对应发行版和架构的播放器依赖。具备 MV100 SDK 时可用 `python3 board/vdh/firmware.py tables --sdk "$MV100_SDK_ROOT"` 生成同一组表。

驱动装入当前内核的 `updates/` 目录。安装器在 GStreamer 检出两个解码器后才启用开机自载。安装和卸载都可以通过有线 DHCP 和原有 SSH 公钥操作，写入范围是运行中 Ubuntu 的文件系统。卸载时先退出播放器，在 SSH 执行下列命令。

```sh
bash /root/histb-updates/board/vdh/uninstall.sh
systemctl is-active ssh
```

卸载会清除本模块、自载配置、初始化表以及板级播放器入口。已安装的 Ubuntu 多媒体包仍可用于普通软件播放。

## 重建与验证

先按项目板级说明构建相同配置的内核，保留构建目录的 `Module.symvers`。在 Linux 上安装 ARM 交叉工具链和 kmod 后运行以下命令。模块绑定 `6.12.114-hi3798-mv100-exp` 的 ABI。升级内核需重新构建和验证。[内核构建说明](../linux-6.12/README.md)

```sh
bash board/vdh/build.sh "$KERNEL_SOURCE" "$KERNEL_OUT" /var/tmp/mv100-vdh-module
modinfo /var/tmp/mv100-vdh-module/mv100-vdh.ko
```

安装自建模块时，上传到盒子并在执行安装器前指定 `VDH_MODULE=/上传路径/mv100-vdh.ko`。同一工具链和内核输出在两个独立模块构建目录得到相同 SHA-256。[预编译模块散列](prebuilt/SHA256SUMS)

在盒子上关闭其他视频播放，运行自检。脚本按指定尺寸编码两段 24 帧测试片，用 FFmpeg 软件解码生成本次参考值，通过两个独立的 GStreamer 硬件解码器输出 NV12，再比较全片散列和硬件帧数。可选尺寸为 320x240、1280x720、1920x1080、1920x1088。

```sh
cd /root/histb-updates/board/vdh
bash verify.sh 320x240
bash verify.sh 1920x1080
cat /sys/devices/platform/mv100-vdh/{decoded_frames,rejected_frames}
```

Chromium 的 GPU 页面绘制走 Lima。Debian Chromium 150 及单独试验的 Raspberry Pi OS Chromium 156 在同一段 H.264 上都选择了内置 `FFmpegVideoDecoder`，VDH 帧数没有增长。浏览器升级后，播放时在 `chrome://media-internals` 查看实际解码器并核对上述计数。[真机结果](../../evidence/vdh-6.12.114.md)

硬件消息布局参考 MV100 对应的 VFMW 配置，同系列其他 SoC 的解码器资料作为对照。模块只在匹配 MV100 设备树的机器上注册；参考图像和消息缓冲按流分配，结束播放时释放。[MV100 SDK](https://github.com/glinuz/hi3798mv100/tree/master/HiSTBLinuxV100R005C00SPC041B020/source/msp/drv/vfmw/vfmw_v4.0) · [HiVXE 驱动说明](https://github.com/HiSilicon-Development/HiVXE-2.0)
