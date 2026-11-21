# EC6108V9C 直连 H27T22S-3 Pro，2026-11-21

同一条 HDMI 线和同一台显示器。Ubuntu 24.04 armhf，Linux 6.12.114，Xfce/Xorg。台架负责供电和串口记录；图像由人在显示器前核对。

| 阶段 | 驱动及接收端结果 |
| --- | --- |
| 原 CPU1200 内核 | DRM 报 connected，能读取两块校验正确的 EDID。720p、1080p 和 EDID 首选 1440p 均出现 RSEN=0 与模式设置失败；显示器提示无信号。 |
| RAM 试启动 | U-Boot 校验候选 uImage。串口记录 HPD 有效、现场 EDID 有效、RSEN 为低时启动 PHY。驱动成功输出 1080p，并切入 2560×1440。使用者确认看到桌面。手动切换 1080p60 和 1440p59.95，均为 gfx=1。 |
| 安装和断电重启 | 备份原 p6，写入并读回 5,959,128 字节。断电后默认从 p6 启动，U-Boot 校验通过；Lima、VDH 节点可用，有线 DHCP 和带主机公钥校验的 SSH 正常，LightDM active，CPU 上限 1.2 GHz。使用者再次确认显示器有桌面。 |

Xorg 的实际桌面为 2560×1440、59.95 Hz。接收端确认的是桌面可见，未单独记录显示器 OSD 的刷新率。最终正常关机，COM6 捕获停机，台架读回 GPIO25=0。

| 对象 | SHA-256 |
| --- | --- |
| EDID | `0edd22db2a96d6ddbae30d10ea9645dcdb5d712c95198a9d7d7ea2574ffd84f2` |
| 通过验收的 uImage | `2cf3929fdb0d3462d1b23783a32eab1712c31d829257d18e5c9bcaffa2866707` |
| 安装后完整 p6 | `9fa9d1cb7b1d001855b13cc2d2b342d0e323e8f207845ec20291fe5491f1f534` |
| 安装前完整 p6 | `f781e2935e2fe5a964d1ec1e0cd13a7a613032c5670e1a4f72a97dfedf54d861` |
