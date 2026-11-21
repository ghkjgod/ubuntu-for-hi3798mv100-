# Chromium 网页 H.264 硬解

本更新适配 EC6108V9C 的 Ubuntu armhf、Linux `6.12.114-hi3798-mv100-exp` 与现有 Lima 浏览器环境。Xfce 的 Chromium 图标默认启动普通 X11 窗口。下文的可选网页硬解模式使用独立的 Chromium 156。网页自身的 `<video>` 和 Media Source Extensions 播放器使用新增的 stateful V4L2 节点。Xfce 下按需打开一个 1280×720 的 Weston 窗口，让 Chromium 的 Wayland 后端把 NV12 图像交给 Mali450 合成。关闭最后一个浏览器进程后，Weston 自动退出。

下表记录 Chromium 专项验收时的驱动版本。后续 H.264 B 帧扩展另在 wiliwili 上做了真机验证。[wiliwili 验收](../wiliwili/README.md)

| 网页视频 | 已验证的行为 |
| --- | --- |
| H.264/AVC | 最高实测 1920×1080，编码尺寸最多 1920×1088。8 位 4:2:0、逐行、I/P、单个短期参考帧、每帧一个 slice。通过的样片报告 `V4L2VideoDecoder`。 |
| 含 B 帧或多参考帧的 H.264 | 已验证 MDN Flower 原片在网页内切回 `FFmpegVideoDecoder` 并播完。部分流只有开始解码后才会暴露不支持的语法，回退取决于浏览器。 |
| HEVC、VP9、AV1 | 网页驱动只向 Chromium 注册 H.264。其他编码的播放能力取决于浏览器内置解码器。本项目另有本地播放器可调用 HEVC Main 硬解。 |

### 现有盒子通过 SSH 安装

前提是已经运行项目的 6.12.114 eMMC 桌面，装好了 Chromium 的 Lima/EGL 更新，且已有带公钥的 SSH 登录。请先正常退出 Chromium，避免在复制用户配置时写入书签等资料。管理端需要 bash、curl、git 与 SSH。盒子需能访问其 Ubuntu 签名软件源，或已预装 Weston 和板级播放器依赖。预留约 600 MiB 的可用空间用于浏览器文件和临时软件包，另预留现有浏览器配置的大小。

```bash
BOX=root@盒子的局域网IP
bash board/vdh/chromium/prepare.sh ./web-debs
python3 board/vdh/firmware.py ./vdh-tables
git archive --format=tar.gz --output=web-vdh.tar.gz HEAD board/vdh overlay/usr/local/bin/histb-chromium tests/resources/chromium-vdh.html
ssh "$BOX" 'mkdir -p /root/histb-updates /root/histb-updates/incoming'
scp -r web-vdh.tar.gz web-debs vdh-tables "$BOX":/root/histb-updates/incoming/
ssh "$BOX" 'set -e; cd /root/histb-updates; tar xzf incoming/web-vdh.tar.gz; bash board/vdh/install.sh --from-dir incoming/vdh-tables; bash board/vdh/chromium/install.sh incoming/web-debs'
```

安装器保留已有启动方式。新镜像默认 X11。需要启用 Weston 网页硬解时，先退出 Chromium，再通过 SSH 执行 `install -m 644 /dev/null /etc/histb/chromium-vdh.enabled`；下次点击原图标即使用硬解入口。

版本固定为 Raspberry Pi OS Bookworm armhf 的 `1:156.0.8078.79-1~deb12u1+rpt1`。三个包的哈希取自其签名索引。管理端和盒子分别校验哈希，盒子再核对包名、版本和架构。它们独立安装在 eMMC 的 `/opt/histb-webvideo`，使用现有 Debian 运行库。首次安装将旧用户配置复制到 `/home/desktop/.config/chromium-vdh`。两个浏览器之后各自保存设置。桌面仍使用独立用户运行 Chromium。[包校验清单](SHA256SUMS) · [Raspberry Pi OS 软件源](https://archive.raspberrypi.com/debian/pool/main/c/chromium/)

### 验收和后续升级

在网页开始播放后打开 `chrome://media-internals`，选中该标签页的播放器，查看 `kVideoDecoderName` 或 `video_decoder`，并核对 `kIsPlatformVideoDecoder`。通过 SSH 在播放前后读取下列计数。请同时关闭其他视频播放器，避免把它们的解码帧计入结果。

```sh
cat /sys/devices/platform/mv100-vdh/{decoded_frames,rejected_frames}
```

对可控的视频源，可用 FFmpeg 生成 30 秒 H.264+AAC 分段复测片，在同目录通过 HTTP 打开随附页面。将 `mse=1` 换为 `mse=0` 可验证普通视频标签。网站服务若只提供 VP9、AV1 或带 B 帧的 AVC，切换网页分辨率并不能保证硬解。[测试网页](../../../tests/resources/chromium-vdh.html) · [实测帧数](../../../evidence/chromium-vdh.md)

```sh
cp tests/resources/chromium-vdh.html .
ffmpeg -nostdin -f lavfi -i testsrc2=size=1280x720:rate=24 -f lavfi -i sine=frequency=600:sample_rate=44100 \
  -t 30 -pix_fmt yuv420p -c:v libx264 -profile:v high -level:v 3.1 -threads 2 \
  -x264-params 'ref=1:bframes=0:weightp=0:sliced-threads=0:keyint=48:min-keyint=48:scenecut=0' \
  -c:a aac -movflags empty_moov+frag_keyframe+default_base_moof video.mp4
python3 -m http.server 8080 --bind 127.0.0.1
# 在运行此服务的盒子上打开 http://127.0.0.1:8080/chromium-vdh.html?mse=1
```

后续 Chromium 需要保留 Linux V4L2 stateful 解码、Wayland NV12 导入和 armhf 支持。新的浏览器版本应先并行安装，再用相同网页、硬件计数和软件回退片验收。普通发行版的 Chromium 替换升级无法保证继续硬解。内核更换时还需按新 ABI 重建模块。[下游 Chromium 的 V4L2 选择](https://github.com/RPi-Distro/chromium/blob/pios/bookworm/debian/patches/rpi/v4l2-do-not-advertise-stateless-codecs.patch)

### 回退

正常退出所有 Chromium 窗口。单次回到原 Debian Chromium 和 X11，在桌面终端运行 `HISTB_CHROMIUM_X11=1 histb-chromium`。要让桌面图标也回到旧入口，在 SSH 中运行以下命令。删除标记后本地播放器仍然可以调用 VDH。

```sh
rm -f /etc/histb/chromium-vdh.enabled
```

要重新启用已安装的版本，运行 `install -m 644 /dev/null /etc/histb/chromium-vdh.enabled`。要卸载板级视频模块，请按上一级说明操作。[VDH 模块的安装与回退](../README.md)
