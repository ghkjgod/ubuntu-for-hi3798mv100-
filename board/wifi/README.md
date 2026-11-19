# RTL8188FU

目标内核为 `4.4.35_ecoo_81082668 SMP mod_unload ARMv7 p2v8`。随附 Realtek GPL 模块版本 `v4.3.23.6_20964.20170110`，绑定 USB `0bda:f179`，SHA-256 为 `43330f5bdcf00fbd539975c9029c2872ac639d54a87216bc3e63e20dddac342c`。

模块取自该盒子的原有离线 p9 备份。可用 debugfs 的 dump 命令从自己的备份中复取 `/lib/modules/4.4.35_ecoo_81082668/kernel/drivers/wifi/usb_rtl8188fu/rtl8188FU_linux_v4.3.23.6_20964.20170110/rtl8188fu.ko`。同版本驱动源码位于板级 SDK 的 `source/component/wifi/drv/usb_rtl8188fu/rtl8188FU_linux_v4.3.23.6_20964.20170110`。完整模块索引由目标系统的 depmod 生成。

本目录的 `rtl8188fu-source.tar.xz` 归档自 SDK 提交 `fd20f78ab02934e71474dbb1d933c6ec911b01c9` 的上述目录。`modules.builtin` 取自相同内核的原系统。[板级 SDK](https://gitcode.com/eagle_xmw/HiSTBLinuxV100R005C00SPC050)
