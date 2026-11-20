# EC6108V9C 的 Linux 6.12.114

升级包包含带板级设备树的 U-Boot legacy uImage、Ubuntu 图形配置、Lima 测试程序、RTL8188FU 固件和模块索引输入文件。它与仓库中的 Ubuntu 24.04 armhf 根文件系统配套。板级适配使用 HiSTBLinux 的 HDMI 初始化代码，Mesa 和 Xorg 来自 Ubuntu 签名软件源。[升级包](../../release/hi3798mv100-linux-6.12.114-hdmi.tar.gz) · [验收记录](../../HARDWARE.md)

## 已验证的板级配置

本台 eMMC 排布为 p6 内核、p9 Ubuntu，p6 始于扇区 28672，长度 40960 个 512 字节扇区。独立 TF 为 mmcblk1。设备树使用本台原 U-Boot 的有线 MAC。为另一台盒子构建前，应核对其 U-Boot MAC、分区起止、内存和板型，并在 board.patch 中使用那台设备的 MAC。

内核提供可切换时序的 HDMI-A-1 扫描输出和独立的 Lima 渲染节点。显示控制器使用 36 MiB 的保留内存，按当前桌面宽度设置图层和行跨度。驱动从 HDMI DDC 读取 EDID，校验时序和 20–297 MHz 像素时钟，支持 SDK 预设、EDID 的逐行时序和有效的自定义 modeline。LightDM 根据运行内核选择 Xorg 配置，4.4 走 fbdev。6.12 使用 modesetting、glamor 和进程内的 Mesa KMSRO 选择器。保留原系统、用户数据和 TF 的分区结构。

## 重建升级包

使用 Linux 文件系统和 Ubuntu 24.04 的 GCC 13 arm-linux-gnueabihf 工具链。宿主需要内核常用构建依赖、libc6-dev-armhf-cross、bc、bison、flex、libssl-dev、libelf-dev、python3、patch、curl、make 和 dtc。Mesa 还需要 g++-arm-linux-gnueabihf、dpkg-dev、pkg-config、ninja-build、python3-mako、python3-ply、python3-yaml、python3-packaging 和 Meson ≥ 1.4。台架使用 Meson 1.7.2，可以用带系统 Python 包的 venv 安装后通过 MESON 指定可执行文件。先准备 SDK 的 EC6108V9C boot 构建目录，其中需要已有 libdisp.a、libhdmi.a 及相应源码。实测 SDK 提交为 fd20f78ab02934e71474dbb1d933c6ec911b01c9。[HiSTBLinux SDK](https://gitcode.com/eagle_xmw/HiSTBLinuxV100R005C00SPC050) · [6.12 板级内核源码](https://github.com/duskbreakmist/hi3798-linux/tree/6bf33e9dead46a7a2dd0dac34e1c0c3b50c46ce7)

```sh
export MV100_SDK_ROOT=/path/to/HiSTBLinux
export MV100_SDK_BOOT="$MV100_SDK_ROOT/out/hi3798mv100/hi3798mdmo1g/obj/source/boot"
bash board/linux-6.12/build.sh /path/to/new-build-dir
```

产物为构建目录中的 `hi3798mv100-linux-6.12.114-hdmi.tar.gz`。脚本校验固定源码、上游时钟驱动和 Realtek 固件的 SHA256。离线复编可设置 `SOURCE_ARCHIVE` 指向内核源码压缩包，`INPUT_CACHE` 指向含 upstream-v6.12.114-crg.c、rtl8188fufw.bin、LICENCE.rtlwifi_firmware.txt 的目录。

升级包内有文件级 SHA256 清单，发布包散列见 [release/SHA256SUMS](../../release/SHA256SUMS)。最初固定 1080p 的 6.12 升级包另名保留为 [hi3798mv100-linux-6.12.114.tar.gz](../../release/hi3798mv100-linux-6.12.114.tar.gz)。

Mesa 的构建脚本从 Ubuntu Ports 获取固定版本的源包，核对三个 SHA256，并用 Ubuntu 归档密钥验证交叉编译依赖。它将 Lima 和 KMSRO 编译在一个单独的 Gallium 库中。Xorg 及用 `histb-lima-run` 启动的程序按当前 Ubuntu Mesa 的库名加载它。Ubuntu 若更新到不同 Mesa ABI，需要基于新版本重新编译适配。宽度超过 3840 的加速渲染目标使用每个像素处理器的显式分块列表，修复 Mali450 MP2 的整屏批量绘制缺块。其他宽度保留原有路径。[Mesa 补丁](mesa/large-render-target.patch)

`MESA_SOURCE_CACHE` 可以指向包含 mesa/SHA256SUMS 内三个源文件的目录，`MESA_SYSROOT` 可以指向含 armhf 开发库的 Ubuntu sysroot。单独重建用户态时运行 `bash board/linux-6.12/mesa/build.sh 新工作目录 rootfs暂存目录`。完整构建会自动执行这一步。

## 安装与试启动

实测盒子已完成安装。给同一台盒子重新部署时，先验证 release/SHA256SUMS，在新目录解开升级包，再执行以下用户态步骤。根文件系统应先按仓库说明配好有线 DHCP 和 SSH。

```sh
sha256sum -c CONTENTS.sha256
sudo apt-get install libgles2 libgl1-mesa-dri libegl-mesa0 xserver-xorg-core mesa-utils-bin \
    fcitx-bin fcitx-ui-classic fcitx-module-dbus fcitx-module-x11 fcitx-frontend-gtk3 fcitx-googlepinyin
sudo tar --extract --gzip --numeric-owner --no-overwrite-dir --strip-components=2 \
    --file=/path/to/hi3798mv100-linux-6.12.114-hdmi.tar.gz --directory=/ ./rootfs/
sudo depmod -a 6.12.114-hi3798-mv100-exp
sudo sh activate.sh
```

台架用 COM6 接管 fastboot，通过专用网口 TFTP 将 uImage 加载到 RAM 地址 0x04000000。临时设置 ipaddr、serverip 和 netmask，执行 `tftp 04000000 trial.bin`。用 `crc32 04000000 5adeda` 核对本次上板镜像的 CRC32 为 5a30e0d5，再执行 `bootm 04000000`。重建出的其他镜像应重新计算长度及 CRC。Ubuntu 进入桌面后执行 `sudo sh check-desktop.sh`，同时从 HDMI 接收端核对画面。

默认升级仅覆盖本台 p6 的前 5955290 字节，通过网口发送镜像文件。上板镜像 SHA256 为 `7d5cf2b4d78af6dca412df63c44442c630ed1a32cb1ef3e3713870acdadbea06`。本台的维护目录是 `/var/lib/histb-kernel-recovery`。其中 `stock-p6.bin` 为原厂完整内核分区，`trial.bin` 为固定 1080p 版 6.12，`hdmi-uImage` 为多模式版本。设备专用工具 `display-slot.py` 校验 eMMC 身份、分区位置和占用、当前内容、备份和目标镜像散列及 uImage CRC，安装后同步并读回写入范围。[本台实测的分区工具](display-slot.py) 使用上述目录中的已知历史镜像（含 hdmi-prior、hdmi-before-audio、hdmi-pcm）。维护目录需单独留存，不能用其他盒子的分区备份替代。该机从 RAM 验证新版后可执行 `python3 display-slot.py audit` 和 `python3 display-slot.py install`。其他设备需要用其原始 p6 备份、分区几何和实际 CID 单独准备和验证回退工具。

### 资源与应用

`activate.sh` 开启 CPU/GPU 频率策略、LZ4 zram 和 RTL8188FU 正常关机处理。仅在 NetworkManager 已接管 eth0、networkd 列出的接口全部未受其管理时，才停用重复的 networkd 服务。root 本地图形会话优先使用 Fcitx，已有用户输入法配置保留。安装后重新登录图形会话生效。新内核含浏览器所需的 madvise 接口，并提供 smaps_rollup 供内存核查。Chromium 在独立普通用户和渲染器沙箱中运行。

`histb-performance full` 将 CPU governor 切换到 performance、GPU 工作频率下限设为最高 OPP。默认 `balanced` 使用 schedutil 和 simple_ondemand，两个模式都保留 GPU runtime PM。若需恢复桌面背景，可在 Xfce 桌面设置中选回背景图。浏览器和系统启动盘分开更新。

## 回退

在本台 Ubuntu 的 root 终端执行以下命令，可回到固定 1080p 的 Linux 6.12。若要进一步回到原厂 4.4，继续执行第二条 restore 后再重启。4.4 启动时 LightDM 自动选择原 fbdev 配置。

```sh
cd /var/lib/histb-kernel-recovery
python3 display-slot.py restore
# 进一步回到原厂 4.4 时执行下一行
# python3 kernel-slot.py restore
sync
systemctl reboot
```

若无法进入系统，用台架保存的 stock-boot.bin 执行同样的 TFTP 内存引导。此恢复镜像来自该设备原 p6，长度 10862647，RAM CRC32 为 82cc9922。以原 bootargs 启动后，仍从原 p9 进入 Ubuntu，再执行上述 restore。该网口恢复路径已在真机演练到 4.4 桌面和 SSH。台架离线目录为 E:\build\lab\kernel-612114，保存 stock-p6.bin、stock-boot.bin 和设备专用工具。本次追加的完整设备专用恢复目录位于 E:\build\lab\kernel-612114-recovery-final，含完整性清单及维护脚本所需的历史内核文件。保持原 U-Boot 的持久 bootcmd。
