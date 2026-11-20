# EC6108V9C 的 Linux 6.12.114

升级包包含带板级设备树的 U-Boot legacy uImage、Ubuntu 图形配置、Lima 测试程序、RTL8188FU 固件和模块索引输入文件。它与仓库中的 Ubuntu 24.04 armhf 根文件系统配套。板级适配使用 HiSTBLinux 的 HDMI 初始化代码，Mesa 和 Xorg 来自 Ubuntu 签名软件源。[升级包](../../release/hi3798mv100-linux-6.12.114.tar.gz) · [验收记录](../../HARDWARE.md)

## 已验证的板级配置

本台 eMMC 排布为 p6 内核、p9 Ubuntu，p6 始于扇区 28672，长度 40960 个 512 字节扇区。独立 TF 为 mmcblk1。设备树使用本台原 U-Boot 的有线 MAC。为另一台盒子构建前，应核对其 U-Boot MAC、分区起止、内存和板型，并在 board.patch 中使用那台设备的 MAC。

内核提供 1080p60 simpledrm 扫描输出和独立的 Lima 渲染节点。LightDM 根据运行内核选择 Xorg 配置，4.4 走 fbdev。6.12 使用 modesetting、glamor 和进程内的 Mesa KMSRO 选择器。保留原系统、用户数据和 TF 的分区结构。

## 重建升级包

使用 Linux 文件系统和 Ubuntu 24.04 的 GCC 13 arm-linux-gnueabihf 工具链。宿主需要内核常用构建依赖、libc6-dev-armhf-cross、bc、bison、flex、libssl-dev、libelf-dev、python3、patch、curl、make 和 dtc。先准备 SDK 的 EC6108V9C boot 构建目录，其中需要已有 libdisp.a、libhdmi.a 及相应源码。实测 SDK 提交为 fd20f78ab02934e71474dbb1d933c6ec911b01c9。[HiSTBLinux SDK](https://gitcode.com/eagle_xmw/HiSTBLinuxV100R005C00SPC050) · [6.12 板级内核源码](https://github.com/duskbreakmist/hi3798-linux/tree/6bf33e9dead46a7a2dd0dac34e1c0c3b50c46ce7)

```sh
export MV100_SDK_ROOT=/path/to/HiSTBLinux
export MV100_SDK_BOOT="$MV100_SDK_ROOT/out/hi3798mv100/hi3798mdmo1g/obj/source/boot"
bash board/linux-6.12/build.sh /path/to/new-build-dir
```

产物为构建目录中的 `hi3798mv100-linux-6.12.114.tar.gz`。脚本校验固定源码、上游时钟驱动和 Realtek 固件的 SHA256。离线复编可设置 `SOURCE_ARCHIVE` 指向内核源码压缩包，`INPUT_CACHE` 指向含 upstream-v6.12.114-crg.c、rtl8188fufw.bin、LICENCE.rtlwifi_firmware.txt 的目录。

在全新目录复编后，uImage 与写入真机的镜像逐字节一致。发布包共 5974386 字节，SHA256 为 `35018566715e67ddd9204f0848bd214d97cbe24434877007e1d04fb4fbfe86fd`。

## 安装与试启动

本次实测盒子已完成安装。给同一台盒子重新部署时，先验证 release/SHA256SUMS，在新目录解开升级包，再执行以下用户态步骤。根文件系统应先按仓库说明配好有线 DHCP 和 SSH。

```sh
sha256sum -c CONTENTS.sha256
sudo apt-get install libgles2 libgl1-mesa-dri libegl-mesa0 xserver-xorg-core mesa-utils-bin
sudo tar --extract --gzip --numeric-owner --no-overwrite-dir --strip-components=2 \
    --file=/path/to/hi3798mv100-linux-6.12.114.tar.gz --directory=/ ./rootfs/
sudo depmod -a 6.12.114-hi3798-mv100-exp
sudo systemctl daemon-reload
```

台架用 COM6 接管 fastboot，通过专用网口 TFTP 将 uImage 加载到 RAM 地址 0x04000000。临时设置 ipaddr、serverip 和 netmask，执行 `tftp 04000000 trial.bin`。用 `crc32 04000000 5aa8e2` 核对本次上板镜像的 CRC32 为 c19d9107，再执行 `bootm 04000000`。重建出的其他镜像应重新计算长度及 CRC。Ubuntu 进入桌面后执行 `sudo sh check-desktop.sh`，同时从 HDMI 接收端核对画面。

默认升级仅覆盖本台 p6 的前 5941474 字节，通过网口发送镜像文件。上板镜像 SHA256 为 `5b691ef1ae8d0d0ff39074409a50d26e8bb9f792c67488c868d764e09080ce2e`。本台的维护目录在 `/var/lib/histb-kernel-recovery`，包含原 p6 完整备份、本次 uImage、写入记录及绑定该 eMMC CID 的 kernel-slot.py。工具在写前校验 CID、分区号、起止、现有分区、两个镜像和 uImage CRC，写后同步并读回核对写入范围。RAM 试启动通过后可在该目录执行 `python3 kernel-slot.py audit`、`python3 kernel-slot.py install`。盲刷其他设备会破坏原有系统。

## 回退

在仍能进入 Ubuntu 时，从 root 终端执行以下命令。重启后应看到 4.4.35_ecoo_81082668，LightDM 自动使用原 fbdev 配置。

```sh
cd /var/lib/histb-kernel-recovery
python3 kernel-slot.py restore
sync
systemctl reboot
```

若无法进入系统，用台架保存的 stock-boot.bin 执行同样的 TFTP 内存引导。此恢复镜像来自该设备原 p6，长度 10862647，RAM CRC32 为 82cc9922。以原 bootargs 启动后，仍从原 p9 进入 Ubuntu，再执行上述 restore。该网口恢复路径已在真机演练到 4.4 桌面和 SSH。台架离线目录为 E:\build\lab\kernel-612114，保存 stock-p6.bin、stock-boot.bin 和设备专用工具。保持原 U-Boot 的持久 bootcmd。
