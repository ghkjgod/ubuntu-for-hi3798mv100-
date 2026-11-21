# 在 Hi3798MV100 上用 wiliwili 播放 B 站 H.264

适用 EC6108V9C、Ubuntu 24.04 armhf、内核 `6.12.114-hi3798-mv100-exp`。菜单启动 wiliwili，保持播放器的硬件解码开启，选用 AVC/H.264 视频。客户端自动选择海思 VDH。已在实际 B 站 720p 和 1080p 带 B 帧的视频上验证。登录状态和站点策略会影响客户端可选清晰度。[真机验收和掉帧记录](../../../evidence/wiliwili-vdh-bframes.md)

## 通过 SSH 安装

在管理机上的本仓库执行以下命令，将 `BOX` 改为盒子的 SSH 地址。先退出盒子上正在播放视频的应用。已有本项目 VDH 驱动的系统可以复用已安装的 CABAC 表。首次安装 VDH 时，去掉下面的 `--from-dir /lib/firmware/hisilicon`，安装器会下载并校验这两张表。APT 需要能获取尚缺的运行库。

```sh
BOX=root@盒子的局域网IP
git archive --format=tar.gz --output=mv100-player.tar.gz HEAD board/vdh
scp mv100-player.tar.gz "$BOX":/tmp/
ssh "$BOX" 'set -e
  mkdir -p /root/mv100-player
  cd /root/mv100-player
  tar -xzf /tmp/mv100-player.tar.gz
  cd board/vdh/wiliwili/dist
  sha256sum -c SHA256SUMS
  bash ../../install.sh --from-dir /lib/firmware/hisilicon
  apt-get install -y ./wiliwili_1.6.0-1+mv100.7_armhf.deb'
```

已装旧版本的盒子也可以使用配套的离线更新包 `wiliwili-vdh-update.tar.gz`。通过 SCP 上传后运行 `tar xzf wiliwili-vdh-update.tar.gz && bash update/install.sh`。关闭播放器后执行 `bash update/install.sh --rollback` 可恢复随包保存的旧模块、板级播放器及 wiliwili mv100.2。更新直接安装到 Ubuntu 文件系统，下一次上电生效，有线 SSH 仍可使用。仅希望切回软件解码时，可直接关闭客户端的硬件解码选项。

## 查看实际解码器

播放 H.264 时，通过 SSH 观察 VDH 计数。单独运行一个播放器，便于将计数归因于当前播放。程序使用 FFmpeg 的 `h264_v4l2m2m` 解码器，通过 stateful V4L2 输出 NV12。libmpv 的 `hwdec-current` 在这种接入方式下可以显示 `no`。验收同时核对了 `video-codec` 的 V4L2 名称、视频尺寸、音轨、驱动帧数以及输出像素。

```sh
watch -n 1 'grep . /sys/devices/platform/mv100-vdh/*frames /sys/devices/platform/mv100-vdh/avc_refusals'
```

不受支持的 H.264 码流可回退到 FFmpeg 软件解码。当前支持的图像格式和参考帧限制见[驱动说明](../README.md)。

## 重建

[发行目录](dist/)含 armhf 安装包、配套源码包和 SHA-256 清单。源码包包含 wiliwili v1.6.0、Borealis 等子模块源码及 mpv 0.37.0，并已应用本目录中的补丁。私有 libmpv 随 wiliwili 安装，负责在跳转时重新打开 stateful 解码器。系统其他播放器使用各自的库。

在 Ubuntu 24.04 x86-64 管理机的 Linux 文件系统上预留 4 GiB 空间，并安装下列构建工具。使用签名的 Ubuntu armhf 软件源准备 sysroot 后执行重建。在已交付的完整离线源码归档 `source-sysroot.tar.xz` 中可直接从 `bash rebuild.sh` 开始。

```sh
sudo apt-get install build-essential g++-arm-linux-gnueabihf cmake ninja-build meson pkg-config dpkg-dev python3 git
mkdir mv100-wili-build
tar -xJf board/vdh/wiliwili/dist/wiliwili-mpv-source.tar.xz -C mv100-wili-build
cd mv100-wili-build
bash prepare-sysroot.sh
bash rebuild.sh
dpkg-deb -f out/*.deb Package Version Architecture
```

驱动构建需要与运行内核配套的配置和 Module.symvers。可用 `board/vdh/verify-avc-bframes.sh` 在板端生成含 B 帧的小片段，并通过两个 V4L2 接口分别与软件输出比较。[内核模块重建说明](../README.md)
