# EC6108V9C / Hi3798MV100 Ubuntu 桌面

上电从 eMMC p9 进入 Ubuntu 24.04.5 LTS armhf。系统、root 主目录和浏览器配置保存在 eMMC。8 GB TF 的单个 ext4 分区挂载在 `/srv/tf`，桌面可通过 `~/TF` 访问。Ubuntu 的启动选择独立于 TF 是否插入。

实测盒子的默认内核为 Linux 6.12.114，Xorg 使用 Mesa Lima 驱动 Mali450 加速。HDMI 按显示器 EDID 选择模式，桌面以所选分辨率原生扫描输出，支持 2560×1440 和 3840×2160 等时序。原厂 4.4 内核可从台架恢复，使用海思 HIFB。内核升级包和基础 Ubuntu 根文件系统分别交付。[升级、检测和回退说明](board/linux-6.12/README.md)

LightDM 免密码进入本地 root 桌面，Xfce 关闭合成。本机和新构建使用 Fcitx 4 与 Google 拼音，Ctrl+Space 切换。已发布的基础根文件系统附带 IBus 智能拼音，升级步骤给出所需的 Fcitx 软件包。Chromium 使用独立的普通用户运行，另装有 NetSurf。NetworkManager 管理 Wi-Fi 和有线网口。接入提供 DHCP 的路由器后，按主机名 `hi3798-desktop` 查找 IPv4 地址。[真机验收与 HDMI 采集帧](HARDWARE.md)

## Chromium 图形与视频

安装网页视频更新后，Xfce 的 Chromium 156 可以调用海思 VDH 播放符合条件的 H.264。已在 HTML 视频和 Media Source Extensions 分段播放中验收，网页图形由 Mali450 加速。当前硬解覆盖 8 位、逐行、单 slice、单参考帧的 I/P 码流。网站实际下发的编码决定能否启用。旧归档中的 Debian Chromium 150 继续作为回退入口。[通过 SSH 安装或回退](board/vdh/chromium/README.md) · [网页视频实测](evidence/chromium-vdh.md)

本地视频可使用 VDH 硬解 H.264 和 H.265 Main 的单参考帧 I/P 码流，已在 1920×1080 逐帧核对画面。打开视频文件或运行 `histb-play 文件名` 即可自动选择解码方式，其他格式由 FFmpeg 接手。已有系统可通过 SSH 安装独立更新。[安装、支持范围与回退](board/vdh/README.md) · [解码验收](evidence/vdh-6.12.114.md)

## 性能模式

本机装入四核 1.2 GHz 更新后，默认 `balanced` 让 CPU 空闲时可降到 400 MHz，负载升高时四核共用时钟可升到 1.2 GHz。芯片达到 90°C 时停用加速，低于 85°C 后恢复。Mali450 在 432–500 MHz 间自动调节。基础 6.12 内核的上限为 CPU 1 GHz、GPU 400 MHz。[更新与 SSH 回退](board/linux-6.12/README.md) · [真机时钟与温控测量](evidence/cpu-gpu-clocks-20261121.md)

要在交互或渲染前直接使用最高频率，运行 `histb-performance full`。用 `histb-performance balanced` 切回，`histb-performance status` 查看当前策略。选择会在重启后保留。两种模式都允许 GPU 空闲挂起。压缩交换空间按内存的四分之一设置，按写入量占用物理 RAM。[负载、响应时间和硬件限制](HARDWARE.md)

## 无屏 SSH 登录

镜像内置 OpenSSH，`ssh.service` 随系统启动并监听 TCP 22。每台新安装的盒子在首次启动服务时生成主机密钥，随后复用。root 通过公钥认证登录。本地桌面保留自动登录。

公开镜像由安装者在电脑端配置自己的登录公钥。下面的 eMMC 安装示例在首次重启前写入公钥并输出盒子主机指纹。自行构建专用镜像时，也可以把 `HISTB_SSH_PUBKEY=/绝对路径/id_ed25519.pub` 作为环境变量传给构建脚本。用对应私钥执行 `ssh root@盒子的地址`，首次连接时核对指纹。

### 脱离台架后的维护

接路由器后，在 DHCP 租约中查看 `hi3798-desktop` 的有线地址。用已登记的公钥登录。更换 IP 时，OpenSSH 的 `HostKeyAlias` 可以让新地址继续按原先核实的主机公钥验证。例如已在本机 known_hosts 中将该盒子登记为 `hi3798-owner` 时，可用以下命令。台架专网的地址仅在相应专网有效。

```sh
ssh -o HostKeyAlias=hi3798-owner -o StrictHostKeyChecking=yes root@盒子的局域网IP
hostname
uname -r
systemctl is-enabled ssh
systemctl is-active ssh NetworkManager
nmcli -g IP4.ADDRESS device show eth0
```

浏览器升级在 Debian 运行环境 `/opt/histb-chromium` 内进行。更新板级浏览器组件时，可用 SCP 上传已校验的升级包，再从 SSH 执行板级说明中的用户态安装命令。两个操作都可以保留 Ubuntu 的 sshd、网络、登录公钥和现有内核。SSH 的远程恢复能力以盒子能启动到 Linux 并建立网络为前提。涉及启动内核、网卡驱动或根文件系统的更新，需要先验证独立的启动回退路径。[浏览器用户态安装](board/linux-6.12/README.md#更新已有-612-桌面的浏览器)

升级前关闭浏览器，将 `/opt/histb-chromium` 和本次将替换的启动器、私有驱动备份到有空间的 ext4 数据卷或管理电脑。需要保留浏览器个人资料时，一并备份 `/home/desktop/.config/chromium`。版本回退时退出浏览器，将已校验的备份恢复到空目录，再换回原路径，以免混入新版库文件。仅需临时切换软件渲染时，也可以直接在 SSH 中执行下方命令。它通过原有 Xorg 会话打开浏览器，退出后仍可从原桌面入口恢复硬件加速。

```sh
systemd-run --collect --setenv=DISPLAY=:0 --setenv=XAUTHORITY=/root/.Xauthority \
    --setenv=HISTB_CHROMIUM_SOFTWARE=1 /usr/local/bin/histb-chromium
```

远程复验 GPU 时，可从 SSH 在同一桌面启动一个独立的临时配置。调试端口只绑定盒子本机，经 SSH 隧道访问。以下第一条在盒子 SSH 会话执行，第二条在管理电脑的另一个终端执行。电脑访问 `http://127.0.0.1:19224/json/version` 可核对浏览器版本，浏览器级 DevTools 协议的 `SystemInfo.getInfo` 可核对 Mali450、合成状态及沙箱。完成后在盒子执行 `systemctl stop histb-browser-check`。

```sh
systemd-run --collect --unit=histb-browser-check -p RuntimeMaxSec=300 \
    --setenv=DISPLAY=:0 --setenv=XAUTHORITY=/root/.Xauthority \
    /usr/local/bin/histb-chromium --user-data-dir=/run/browser/ssh-check \
    --remote-debugging-address=127.0.0.1 --remote-debugging-port=19223 about:blank
ssh -N -L 127.0.0.1:19224:127.0.0.1:19223 局域网SSH别名
```

## 取得安装文件

```bash
git lfs install
git clone git@github.com:ghkjgod/ubuntu-for-hi3798mv100-.git
cd ubuntu-for-hi3798mv100-
git lfs pull
(cd release && sha256sum -c SHA256SUMS)
```

`release/ubuntu-noble-armhf-xfce.tar.gz` 是完整根文件系统。`release/tar-helper.tar.gz` 为原系统提供支持 ACL、扩展属性和 numeric owner 的 GNU tar。网络安装仅传输压缩后的文件和一份启动脚本，跳过 eMMC 和 TF 的空闲空间。Ubuntu 软件包版本见 `release/packages.tsv`，Chromium 独立环境的版本清单随根文件系统保存在 `/opt/histb-chromium/etc/chromium-runtime-packages.tsv`。

## 安装到 eMMC

本入口针对已验证的 EC6108V9C、厂商内核 `4.4.35_ecoo_81082668`、eMMC p9 ext4，以及现有 `/sbin/init -> ../bin/busybox` 的系统。先用该系统启动。Linux 主机上的 SSH 别名 `ec6108` 应绑定专用网口和已核对的盒子主机公钥。

```bash
bash install-emmc.sh ec6108 inspect
```

记下输出的 eMMC CID、p9 起始扇区和扇区数。如果准备使用 TF，先在原系统用 `blkid` 核对其 ext4 UUID。以下两个变量需替换成本机实测值。

```bash
emmc='实际eMMC_CID 实际p9起始扇区 实际p9扇区数'
tf_uuid='实际TF_ext4_UUID'
bash install-emmc.sh ec6108 stage release "$emmc" "$tf_uuid"
```

不带最后的 TF UUID 也可安装独立的 eMMC 系统。stage 验证目标分区和可用空间，在 p9 的 `/noble-system` 展开 Ubuntu，保留原系统及其启动文件。先在原系统的交互终端给本机设置管理密码，供屏幕锁定、手工登录和维护使用。发行包不含预设密码或台架密钥。

```bash
ssh -t ec6108 'chroot /noble-system passwd root'
ssh ec6108 'umask 077; mkdir -p /noble-system/root/.ssh && cat >> /noble-system/root/.ssh/authorized_keys' < ~/.ssh/id_ed25519.pub
ssh ec6108 'chroot /noble-system ssh-keygen -A && chroot /noble-system ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub'
bash install-emmc.sh ec6108 trial "$emmc"
ssh ec6108 'sync && reboot'
```

trial 原子替换 p9 上的 `/sbin/init` 链接。盒子沿用原有 eMMC U-Boot、环境、内核和分区表。PID 1 的小脚本先消费一次试启标记，再将同一个 p9 上的 `/noble-system` 切换为根，执行 Ubuntu systemd。第一次试启成功后，通过 SSH 登录新的 Ubuntu，或在本地终端确认并设为默认系统。若原系统与 Ubuntu 使用同一 IP，可在首次连接 Ubuntu 时加上 `-o HostKeyAlias=hi3798-ubuntu`，按上一行输出的主机指纹独立核对。

```bash
findmnt -o TARGET,SOURCE,MAJ:MIN,FSROOT /
cat /proc/1/comm
loginctl list-sessions
touch /etc/histb-boot/default
sync
```

在本机，根显示 `/dev/mmcblk0p9[/noble-system]`、`259:1`，PID 1 为 systemd。本地桌面自动登录的 UID 为 0。单次试启未确认时，再次启动会回到原系统，可重新执行 trial。

## TF 数据卷

已配置的 ext4 卡按 UUID 自动挂载。访问 `/srv/tf` 后，确认设备匹配，再为普通用户浏览器和 root 桌面建立共享数据目录。

```bash
ls /srv/tf
findmnt -t ext4 --mountpoint /srv/tf
# 确认上一行的 SOURCE 为外置卡后执行。
install -d -m 0755 -o desktop -g desktop /srv/tf/desktop
```

这台盒子的 TF 已完成这些步骤。早期 TF 试运行使用过的个人文件保存在数据卷的 `desktop/tf-trial-home.tar.gz`。如需把本项目早期的 TF 系统测试卡转换为纯数据卡，可在已经从 eMMC 运行的 Ubuntu 下使用 `board/tf-data.sh`。它核对 SD 类型、CID、分区几何信息、UUID 和测试安装标记，保存测试用户的主目录，然后格式化这个外置分区，校验文件系统并恢复归档。普通数据卡直接挂载即可。

## 回退

在 Ubuntu 的 root 终端执行以下命令，下一次启动会进入保留在同一个 p9 上的原系统。

```bash
touch /etc/histb-boot/stock
sync
systemctl reboot
```

在原系统可运行 `bash install-emmc.sh ec6108 rollback "$emmc"` 恢复原有 BusyBox init 链接。保留 Ubuntu 文件可供取回数据。准备再次试启时先在原系统移除 `/noble-system/etc/histb-boot/stock` 与 `/noble-system/etc/histb-boot/default`，再运行 trial。整个流程使用常规文件操作，无需改 U-Boot 环境或重写整个 flash。

## 从源码重建

使用 Ubuntu 24.04 或 WSL2，将工作目录放在 Linux ext4 上，预留约 6 GiB。主机需提供 bash、GNU tar/gzip、curl、gpgv、ubuntu-keyring、debian-archive-keyring、debootstrap、ca-certificates、util-linux、coreutils 和 dpkg。x86_64 主机还需 qemu-user-static，以及带 F 标志的 qemu-arm binfmt 注册。已经安装 qemu-user-static 的 WSL2 可以这样检查并注册该架构。

```bash
sudo sh -c 'test -e /proc/sys/fs/binfmt_misc/qemu-arm || cat /usr/lib/binfmt.d/qemu-arm.conf > /proc/sys/fs/binfmt_misc/register'
sudo env HISTB_SDK=/path/to/built/sdk bash build-rootfs.sh /path/to/ext4/work /path/to/output
(cd /path/to/output && sha256sum -c SHA256SUMS)
```

板级工具需要 SDK 提交 `fd20f78ab02934e71474dbb1d933c6ec911b01c9` 及编译生成的 `out/hi3798mv100/hi3798mdmo1g/include`。Wi-Fi 使用与本机内核匹配的 Realtek 模块，模块、源代码归档和许可证位于 `board/wifi/`。[SDK 构建入口](https://github.com/ghkjgod/emuelec-for-hi3798mv100)

构建脚本使用 Ubuntu keyring 核验官方 Ubuntu Base 24.04.5 armhf 的签名及 SHA-256。软件包来自清华 Ubuntu Ports 的 noble、noble-updates 和 noble-security。Chromium 及其运行库单独从清华的 Debian bookworm 和 bookworm-security 获取并验证 Debian 签名，保存在 `/opt/histb-chromium`。启动器用普通用户 desktop 运行 Chromium。两个发行版使用各自的软件包数据库。安全更新会影响重新构建时可获得的版本。[Ubuntu Base](https://cdimage.ubuntu.com/ubuntu-base/releases/24.04/release/) · [清华 Ubuntu Ports](https://mirrors.tuna.tsinghua.edu.cn/help/ubuntu-ports/) · [Debian armhf Chromium](https://packages.debian.org/bookworm/armhf/chromium)

## Linux 6.12 的 HDMI 输出

在 Xfce 的“显示”面板中选择分辨率和刷新率，也可以在本地终端运行以下命令。星号表示当前时序，加号表示首选时序。HDMI 接收端没有有效 EDID 时，默认选择 1080p60。支持的模式、像素时钟和物理采集结果见[真机记录](HARDWARE.md)。

```sh
xrandr --current
xrandr --output HDMI-1 --auto
# 以下模式出现在当前显示器的列表时可以手选
xrandr --output HDMI-1 --mode 3840x2160 --rate 30
```

已验证 4K 超高清 30 Hz、DCI 4K 24/30 Hz、超宽 3440×1440 50 Hz 和 1080p 120 Hz。验证自定义模式时，在已知接收端能够接受该时序的条件下，可以用 XRandR 临时添加。例如下列 DCI 4K 30 Hz 参数来自 CTA VIC 100，像素时钟 297 MHz。重新启动 Xorg 即清除手工添加的模式。

```sh
xrandr --newmode DCI4K30 297 4096 4184 4272 4400 2160 2168 2178 2250 +hsync +vsync
xrandr --addmode HDMI-1 DCI4K30
xrandr --output HDMI-1 --mode DCI4K30
```

## 原厂 4.4 内核的显示设置

启动和热插拔后读取 HDMI 接收端 EDID。优先匹配已验证的 2560×1440 reduced-blanking 时序；优先时序为 1080p 的设备保留 1080p。遇到无效 EDID，默认输出 1080p60。本机 BSP 的 HIFB 图层宽度上限为 1920。发射 1440p 时，显示控制器把完整的 1920×1080 桌面缩放至整个 HDMI 画面。实际刷新率以 HDMI 时序和接收端的输入信息为准。

在 root 终端可以复测两个模式。每次手工切换后读取 `/proc/msp/hdmi0` 和 `/proc/msp/disp1`，并核对接收设备的输入信号页。

```bash
systemctl stop histb-monitor.timer
sink-mode 1080
sink-mode 1440
/usr/local/bin/histb-monitor
systemctl start histb-monitor.timer
```

最初的 TF 根实验脚本 `install-over-ssh.sh`、`boot-over-ssh.sh` 和 `board/ram-root.sh` 也随仓库保留。日常使用按上面的 eMMC 安装流程操作。
