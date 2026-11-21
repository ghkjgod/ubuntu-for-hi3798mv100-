# Chromium 网页视频真机验收 · 2026-11-21

EC6108V9C / Hi3798MV100，eMMC 上的 Ubuntu 24.04.5 armhf，Linux `6.12.114-hi3798-mv100-exp`，Chromium 156.0.8078.79。经原有 SSH 安装和重启，LightDM、NetworkManager、SSH 均恢复 active。`/dev/mv100-avc` 指向 stateful H.264 节点，`/dev/video-dec0` 指向原有 stateless H.264/HEVC 节点。重启后的模块 SHA-256 为 `ca858f08d1c848b9973404137e79fb216220bc6bdfd0179abcf582d858df6e8e`；从发布源文件在另一目录重建得到相同的散列。

## 网页播放器

测试由 Xfce 的 Chromium 入口启动，页面从盒子回环 HTTP 服务加载。重启复测使用桌面浏览器的持久配置目录。下表的解码器读自该播放器的 Chromium Media 事件。VDH 增量取自 Linux 驱动的 `decoded_frames`，每项硬解均同时报告 `kIsPlatformVideoDecoder=true`。掉帧和总帧来自网页的 `getVideoPlaybackQuality()`，包括显示环节。每行是一次实测，计数口径与硬件完成帧数不同。

| 测试 | 媒体时长 | 播放用时 | 掉帧 / 浏览器总帧 | VDH 增量 | 视频解码器 |
| --- | ---: | ---: | ---: | ---: | --- |
| 重启后，H.264 High 1920×1080p24，网页内画面宽 960 像素 | 6.00 s | 6.39 s | 4 / 143 | 144 | V4L2VideoDecoder |
| 重启后，同片全屏，实际视频元素 1920×1080 | 6.00 s | 7.25 s | 27 / 143 | 144 | V4L2VideoDecoder |
| 重启后，MSE 分片 H.264 High 1280×720p24 + AAC，自动调频 | 30.02 s | 31.19 s | 47 / 719 | 720 | V4L2VideoDecoder |
| 同一 MSE 片，CPU 1.0 GHz、GPU 400 MHz | 30.02 s | 30.77 s | 30 / 719 | 720 | V4L2VideoDecoder |
| MDN Flower 原始 H.264 High，960×540，含 B 帧 | 5.055 s | 7.12 s | 1 / 150 | 0 | FFmpegVideoDecoder |

全屏复测将外层 Weston 窗口置于全屏，再通过网页请求视频全屏。DOM 读出的视频矩形覆盖 1920×1080；页面像素回读区分出相邻的红、绿、蓝色块，有线 HDMI 采集记录了整屏测试图。采集时桌面 HDMI 输出配置为 1920×1080@60。源片 24 fps。[HDMI 实拍](chromium-vdh-hdmi.png)

30 秒 MSE 片由 MDN 的公开 Flower 素材循环、重新编码为单参考帧 I/P H.264，并加 AAC 轨。测试网页按块调用 `SourceBuffer.appendBuffer()`；浏览器用 `FFmpegAudioDecoder` 解码音轨，测试为静音播放。MDN 原片独立验证了软件回退。上述实测覆盖网页入口与媒体管线；商业网站的广告、DRM、自动选码和切换清晰度尚无逐站验收。[MDN 原始视频](https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4) · [可重跑的网页](../tests/resources/chromium-vdh.html)

## 图形、频率及回退

Chromium GPU 信息报告 ANGLE / Lima / Mali450 / OpenGL ES 2.0，图形合成、光栅化和 WebGL 已启用。Weston 自身也报告 Mali450。浏览器 GPU 进程 UID 1000、补充组 video 与 render、Seccomp 模式 2。退出浏览器后专用 Weston 服务恢复 inactive。临时测试网页服务和开发者协议端口均已关闭。使用 X11 回退入口实际启动了 Debian Chromium 150 的版本查询，恢复通常入口后返回 Chromium 156。

4 核 CPU 在线，当前内核开放 400、600、800、1000 MHz。Mali450 开放 200、250、300、345.6、375、400 MHz。上表的满频对照只临时切换已开放档，完成后恢复 `schedutil` 和 `simple_ondemand`。CPU 驱动中还列有固定启动电压下的 1.2 和 1.5 GHz 选择，GPU 时钟表列有 432 和 500 MHz。当前板级策略未开放这些高档，系统未枚举 thermal zone 或 hwmon 温度传感器。这些高档尚无本机持续负载稳定性与温度结果。[CPU 驱动](https://github.com/duskbreakmist/hi3798-linux/blob/6bf33e9dead46a7a2dd0dac34e1c0c3b50c46ce7/drivers/cpufreq/hi3798mv100-cpufreq.c) · [GPU 时钟驱动](https://github.com/duskbreakmist/hi3798-linux/blob/6bf33e9dead46a7a2dd0dac34e1c0c3b50c46ce7/drivers/clk/hisilicon/clk-hi3798mv100-gpu.c)

## 本地播放器回归

320×240 与 1920×1080 的 H.264、HEVC 各测试 24 帧，四次驱动帧数增量均为 24，硬件输出的整段 NV12 散列与 FFmpeg 软件参考完全相同。GStreamer 1.24.2 结束显式硬解管线时仍打印既有的 `gst_mini_object_unref` 警告。重启后的网页测试结束时 VDH 计数为 1728，`rejected_frames=0`。[复测命令](../board/vdh/README.md) · [基线记录](vdh-6.12.114.md)

最终经 SSH 同步并正常停机，COM6 收到内核 halt 消息。继电器控制器回读 `OK READ 25=0`，结束状态 `STOPPED_SAFE`。
