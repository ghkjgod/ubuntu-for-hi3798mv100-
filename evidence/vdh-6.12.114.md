# EC6108V9C VDH 验收 · 2026-11-21

Ubuntu 24.04.5 armhf 从 eMMC p9 启动，内核 `6.12.114-hi3798-mv100-exp`。驱动以 V4L2 Request 模块部署，FFmpeg 版本 `7:6.1.1-3ubuntu5`，GStreamer v4l2codecs 来自 `gstreamer1.0-plugins-bad 1.24.2-1ubuntu4`。本次通过台架有线 SSH 更新文件并验证，COM6 记录启动和停机。

## 画面与路径

下表是两段独立制作的 1920×1080 MP4，各含 24 帧。管理电脑强制软件解码后输出 NV12。盒子用 `gst-launch-1.0` 显式选择 `v4l2slh264dec` 或 `v4l2slh265dec`，将可见画面输出为同一格式，每段 74,649,600 字节。两端散列分别相同。重启后重新读取两个文件，硬件计数从 0 到 48，拒绝计数为 0。

| 视频 | 两端原始像素的 SHA-256 |
| --- | --- |
| H.264 High，8 位，I/P | `a0d522f4ecd01bf178f0a8d639917d23bb8964b9730c2a9b8334802c37877408` |
| HEVC Main，8 位，I/P | `e1620f29e33411fcb9bb8039320ce9911edabc5712ec188035bd58e780897799` |

还验证了 H.264 Baseline/CAVLC、Main/CABAC 和 High/CABAC，以及 HEVC Main 的 I/P 参考图像。驱动对过大、超出支持语法或分配不到缓冲的请求返回错误。GStreamer 1.24.2 在显式硬解管线中报告过一次 `gst_mini_object_unref` 引用计数警告；上述管线均正常结束，输出散列相符。这里的结果覆盖测试样片的像素正确性，未测持续长片的显示帧率。

发布模块再次重启自载后，在盒子本机运行仓库的 `verify.sh`，为每项重新编码 24 帧并独立生成 FFmpeg 参考。下表六项均通过，VDH 从 0 增至 144 帧，拒绝数保持 0。

| 可见尺寸 | H.264 NV12 SHA-256 | HEVC NV12 SHA-256 |
| --- | --- | --- |
| 1280×720 | `13917624aa74356d98bcd8ec2058db520af8bf3fdb3c7c6405704cb0a36a97c1` | `fef3d16da3055e15b9cc8fec724c46cf6caac4f10684982f8ec513239cbef21a` |
| 1920×1080 | `0aa94d448ffb644ae5612314957da0b8aa02190dfbbd32c4a3fc60d6eb21efb2` | `82e2160c7b0d097f776268b51db4b445322b6f1a7449610e4a353c2693c95053` |
| 1920×1088 | `2bfd7556c274a5c2e40af274b38eee88e1bbc8448e44a829f12a61197bcc7282` | `f64b02e4300689079d98e0f83dd887f9ff04ba2de368143dfeb3da9030cc05a8` |

在运行 Xfce 的盒子上使用桌面入口所调用的 `histb-play` 做了以下检查。

| 文件 | 播放结果 |
| --- | --- |
| 两段 1080p H.264 和 HEVC MP4 | 依次选中两个 V4L2 解码器，VDH 累计 0 → 24 → 48，拒绝 0。 |
| VP9、AV1、VP8、MPEG-2 | 分别选中 FFplay，播放到文件末尾，VDH 帧数维持 48。 |
| H.264 含 B 帧 | 预检后直接使用 FFplay，硬件帧数维持 48。 |
| HEVC Main 10 | FFprobe 确认 10 位 4:2:0、无 B 帧；播放器直接选用 FFplay，播放正常结束。 |
| H.264 加权 P 预测 | 预检通过；VDH 拒绝图片后播放器切到 FFplay 从头播放，结束码 0。 |
| 桌面文件关联 | `gio mime` 查询 MP4、WebM、AVI、WMV，默认均为 `histb-play.desktop`。 |

## 浏览器

使用 H.264 160×128、24 帧的 MP4，通过浏览器开发者协议实际播放并读取播放器属性。已安装的 Debian Chromium 150.0.7871.181 和通过临时挂载单独测试的 Raspberry Pi OS Chromium 156.0.8078.79 均报告 `kVideoDecoderName=FFmpegVideoDecoder`、`kIsPlatformVideoDecoder=false`。虽然启用视频解码开关后 GPU 状态显示 enabled，硬解 profile 列表为空，VDH 帧数没有增长。临时浏览器已移除。日常安装继续使用已通过 Mali450 网页合成和 WebGL 1 验收的 Debian 版本。[桌面图形验收](../HARDWARE.md)

## 安装与恢复

通过 SSH 安装、卸载，再次安装。卸载后模块及开机自载配置消失，SSH 仍为 active。正常重启后使用原主机公钥重新登录，有线 IPv4 配置为 auto，SSH、NetworkManager、显示管理器均为 active。设备由 udev 创建，属组 video、权限 0660。正式模块两次在独立目录重建所得 SHA-256 均为 `0d1e722b2146d9bcbe36a92469a36883a9e03961da7abbcaf0333ff41156ab43`；文件约 28 KiB。[自检与卸载步骤](../board/vdh/README.md)

验收结束通过 SSH 正常关机，COM6 记录内核已停机。继电器随后回读 GPIO25=0，控制器最终状态 STOPPED_SAFE。

随后加入了 Chromium 使用的 stateful H.264 节点并完成网页视频复测。[新版结果](chromium-vdh.md)
