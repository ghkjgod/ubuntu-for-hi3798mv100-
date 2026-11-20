# 盒端资源复测

在该板的 root 会话中进行。受控测试完成后可用 `histb-performance balanced` 恢复默认频率策略。绘制基准需要正在运行的 Xorg，复现发布数据时使用相同的 2560×1440 模式。

```sh
export DISPLAY=:0 XAUTHORITY=/root/.Xauthority
histb-performance balanced
python3 measure.py balanced
# 使用目标 armhf 的 EGL、GLES2 开发头文件和库，可在另一台机器交叉编译
cc -O2 -Wall gpu-load.c -lEGL -lGLESv2 -o /run/gpu-load
python3 load.py balanced
histb-performance full
python3 measure.py full
python3 load.py full
histb-performance balanced
```

`measure.py` 先采样 8 秒空闲状态，再执行 120 次 800×450 的 X11 填充和同步像素读回。记录的是客户端完成时间。同步满载测试将四个 SHA256 进程各绑定到一个 CPU 核，同时执行 15 秒的 768×512 离屏 GLES 工作负载，从 Lima 驱动的 busy/total 计数统计忙碌率。

```sh
python3 system-check.py storage
systemd-run --quiet --wait --collect --pipe -p MemoryMax=52M -p MemorySwapMax=80M \
    /usr/bin/python3 "$(pwd)/system-check.py" pressure
python3 pcm-lengths.py
python3 pinyin-check.py
```

存储测试会在 /var/tmp 和 /srv/tf 各写一个 16 MiB 临时文件并 fsync，然后通过 direct I/O 读回校验。先确认 /srv/tf 已挂载目标 TF。内存压测在独立 cgroup 中触及 78 MiB 的可压缩数据，逐页检验内容。拼音测试会暂时打开 GTK 输入框，用 XTEST 模拟按键并核对实际提交的“中文”。
