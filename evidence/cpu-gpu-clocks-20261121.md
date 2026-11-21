# EC6108V9C 实测时钟，2026-11-21

硬件为 Hi3798MV100，系统 ID 为 `0x37980100`。根据海思 SDK 的 `HI_DRV_SYS_GetChipPackageType`，本机读出的封装熔丝值 7 对应 QFP216；`SC_GEN17` 高字节为 2（TT）。SDK 的 MV100 `mpu_clk_set_rate` 会把这款封装的 1200000 kHz 请求映射为 1000000 kHz。本次全程使用开机电压。

| CPU 档位 | 台架结果 |
| --- | --- |
| 标称 1.5 GHz | 设置原厂选择器后 SSH 和串口均停止响应。重启恢复。没有取得切换后的独立频率读数。 |
| 1.4 GHz | 从 RAM 在 U-Boot 编程 APLL，周期计数比例约 1400.3 MHz，可切回原频。冷启动试验内核启用四核，在 0.360 秒 CPU3 发生 undefined instruction，并触发 kernel panic。 |
| 1.3 GHz | U-Boot 周期计数比例约 1299.8 MHz，可切回原频。相同四核 Linux RAM 镜像的串口日志停在启动后 0.474 秒，观察超过 60 秒，未到达用户态。 |
| 标称 1.2 GHz | 选择器 5，短测通过四核共 96 次 4 MiB SHA-256 和 10 次 GLES 绘制。持续混合负载在内核启动后 91.72 秒首次报告 undefined instruction。 |
| 标称 1.0 GHz | Linux 在选择器 0，Cortex-A7 PMCCNTR 对独立 SP804 测得 982.946 MHz。选择器 1（标称 800 MHz）同法测得 749.921 MHz。 |

1.4 和 1.3 GHz 的启动镜像内嵌只读测试程序，强制 `root=/dev/ram0 rdinit=/init panic=0`，关闭 CPUFreq。U-Boot 的测量先用已知 APLL 频率校准相同计时窗，再以 960 MHz 验证重编程和恢复。两次高频启动均在 RAM 中操作，eMMC 引导环境保持原配置。RAM Linux 镜像 SHA-256 为 `5881a0b57ee401b6dee1a9372597f2175726907d90c6d9f153f1695e5798e461`。

相同 RAM 镜像在原厂 APLL 配置下进入四核用户态并连续运行 240 秒。按 CPU0 至 CPU3 顺序完成 8092、8089、8094、8094 轮计算，结果均与宿主独立生成的 32 位校验值一致。该独立对照使用 U-Boot 及桌面 Linux 的频率读数；initramfs 内的附加测频模块加载返回 EFAULT。

## Mali450

CPU 限制在标称 1.0 GHz，GPU 固定 500 MHz。连续 180 秒完成 209 次 GLES2 像素校验，四个核心分别完成 2056、1902、1848、1884 次 2 MiB SHA-256 校验，共 7690 次。硬件时钟状态寄存器 `CRG+0x154` 在 432 MHz 为 `0x05561111`，500 MHz 为 `0x055611f1`（CPU 选择器为 0）。

设置 `simple_ondemand`，范围 432–500 MHz。使用 1536×1024 离屏 GLES2 连续绘制，12 秒运行窗口完成 1017 次绘制，中心像素为 RGBA `128,128,64,255`。100 次频率采样按时间顺序为 8 次 432 MHz、60 次 500 MHz、32 次 432 MHz。渲染器报告 Mali450。

## 安装与冷启动

日用内核 uImage SHA-256 为 `83183897bd65da844ad0eb7433b4a55b785de174fda4a240156689344d7b05ce`。先从 RAM 启动这个镜像，跑出 1039 次绘制，频率采样依次为 6 次 432 MHz、58 次 500 MHz、26 次 432 MHz。校验 p6 状态后，读回的 5,955,290 字节前缀 SHA-256 为 `24778bdecd9e884a2fdfdebc3ef34de21a310eefd7d1167977fd3518fab7f445`。没有读取整盘。

断电再上电，默认从 eMMC 启动，启动 ID 为 `635f9be2-1a12-412a-8da9-b29d41f600b7`。SSH、LightDM 和性能策略服务均 active，CPU0–3 在线，`schedutil` 400000–1000000 kHz；GPU 为 `simple_ondemand`，432000000–500000000 Hz。现有 H.264 节点、stateless VDH 节点和 Mali 渲染设备均已注册。
