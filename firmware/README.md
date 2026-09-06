# 预编译固件

- `FoloToy-AI-Passport-Dashboard-full.bin`：从 `0x0` 写入的完整镜像，推荐首次安装。
- `FoloToy-AI-Passport-Dashboard-app.bin`：从 `0x10000` 写入的应用镜像，仅用于相同分区布局。
- `bootloader.bin`：bootloader，偏移 `0x0`。
- `partition-table.bin`：分区表，偏移 `0x8000`。
- `SHA256SUMS`：下载校验值。

这些文件由 ESP-IDF 5.5.3 为 ESP32-C3 / 8 MB Flash 构建。不要在其他硬件版本上直接刷写，也不要执行整片擦除。完整操作见 `docs/DEVICE_AND_USER_GUIDE.zh-CN.md`。
