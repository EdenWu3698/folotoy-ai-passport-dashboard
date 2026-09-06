# FoloToy AI Passport Dashboard

把 FoloToy AI Passport 变成一枚离线优先的 AI 开发者电子护照：像素头像、个人资料、GitHub 二维码、项目主题、91 天绿色活动热力图，以及 Kimi 与 Codex 配额，都显示在掌心大小的 ESP32-C3 屏幕上。

数据由 macOS 本地助手通过加密蓝牙同步，不依赖 Claude Desktop。仓库同时提供可直接刷写的固件和完整源码。

> English: A privacy-first, BLE-synced developer dashboard for the FoloToy AI Passport. Prebuilt firmware and source are both included.

## 能看到什么

页面按照“个人 → 项目 → 配额”排列：

1. `PROFILE`：像素头像、姓名、签名、兴趣标签。
2. `LINKS`：与系统主题一致的深绿反色 GitHub 二维码。
3. `FOCUS`：近 30 天项目主题、会话数和活跃天数。
4. `HEAT`：91 天 GitHub 风格绿色热力图和 token 汇总。
5. `STAMPS`：最近项目主题印章。
6. `KIMI QUOTA`：5 小时/7 天使用率、重置时间、当日请求及 token。
7. `CODEX QUOTA`：Codex 主窗口、Spark 5 小时/周配额和重置额度。

短按 `UP` / `DOWN` 循环翻页。同步结果保存在设备 NVS，Mac 离线或重启后仍可显示最近一次数据。

## 设备型号与硬件

本项目针对 **FoloToy AI Passport（ESP32-C3、8 MB Flash 版）**。

| 项目 | 规格 |
| --- | --- |
| MCU | ESP32-C3，RISC-V，BLE 5 LE，无 PSRAM |
| 屏幕 | ST7789P3 彩色 TFT，240 × 320，RGB565 |
| 存储 | 8 MB SPI Flash |
| 电池 | 520 mAh，CW2017 电量计 |
| 操作 | UP / DOWN / OK 三个 ADC 按键 |
| 无线 | 2.4 GHz Wi-Fi 802.11 b/g/n；Bluetooth 5 LE |
| 接口 | USB-C 5 V；USB Serial/JTAG；被动 NTAG213 NFC |
| 音频 | 内置麦克风与扬声器，ES8311 codec（本固件未使用音频） |
| 尺寸/重量 | 约 60 × 95 × 8.5 mm / 50 g |

其他版本的分区、屏幕或引脚可能不同，请勿直接刷写。

## 直接刷入预编译固件

从 [Releases](https://github.com/EdenWu3698/folotoy-ai-passport-dashboard/releases) 下载：

- `FoloToy-AI-Passport-Dashboard-full.bin`：推荐，包含 bootloader、分区表和应用。
- `FoloToy-AI-Passport-Dashboard-app.bin`：仅应用，适合已有相同分区表的设备。
- `SHA256SUMS`：用于校验下载文件。

安装 `esptool` 并找到串口：

```bash
python3 -m pip install esptool
ls /dev/cu.usbmodem*
```

写入完整固件：

```bash
python3 -m esptool --chip esp32c3 -p /dev/cu.usbmodem101 \
  write_flash 0x0 FoloToy-AI-Passport-Dashboard-full.bin
```

把串口名替换成你机器上的实际值。**不要运行 `erase_flash`**：项目分区布局保留了设备的 `cardid`（`0x356000`）和恢复区（`0x700000`）。刷写前建议先备份原厂固件；刷机本身有风险，请确认型号一致。

## 同步个人资料与配额

当前本地助手支持 macOS 13+、Python 3.11+ 和系统蓝牙：

```bash
cd host/ai-passport-sync
chmod +x setup.sh run.sh launch-agent.sh
./setup.sh
```

编辑生成的 `config.json`，再执行：

```bash
./run.sh preview
./run.sh sync
```

首次同步时，设备会显示六位配对码；在 macOS 弹窗中输入。无需 Claude Desktop。若要开机自动运行：

```bash
./launch-agent.sh install
```

后台模式默认每 15 秒刷新 Kimi/Codex 配额，每 5 分钟刷新完整资料和项目统计。详细说明见 [本地同步助手手册](host/ai-passport-sync/README.md)。

数据来源是可选的：

- Claude Code：`~/.claude/projects` 中的本地历史，仅统计元数据和输出 token。
- Kimi：CC Switch 当前 Provider 的本地数据库与 Kimi 用量接口。
- Codex：本机已登录的 Codex CLI 只读配额接口。

缺少某项时，对应页面会显示无数据，其余页面仍能使用。

## 换成自己的头像和二维码

安装 Pillow 与 qrcode：

```bash
python3 -m pip install pillow qrcode
```

使用照片生成 48 × 48、16 色像素头像：

```bash
python3 tools/generate_avatar_asset.py --input /path/to/avatar.png
```

不带 `--input` 会生成仓库内默认的通用机器人头像。生成自己的二维码：

```bash
python3 tools/generate_github_qr.py --url https://github.com/your-name
```

重新构建并刷入后生效。二维码使用深绿背景和亮绿色模块，仍保留完整静区；不同相机表现有差异，发布前请用目标手机实测。

## 从源码构建

需要 ESP-IDF 5.5.3：

```bash
. /path/to/esp-idf-v5.5.3/export.sh
idf.py set-target esp32c3
idf.py build
idf.py merge-bin
idf.py -p /dev/cu.usbmodem101 flash
```

主机逻辑测试：

```bash
cmake -S tests -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
python3 -m unittest discover -s host/ai-passport-sync/tests -v
```

## 项目结构

```text
assets/                 默认头像与资源说明
components/bsp/         FoloToy AI Passport 板级驱动
docs/                   设备与操作手册
firmware/               可直接刷写的发布固件
host/ai-passport-sync/  macOS BLE 本地同步助手
main/                   BLE、协议、持久化、状态与 LVGL UI
tests/                  无硬件主机测试
tools/                  头像和二维码生成器
partitions.csv          8 MB 安全分区布局
```

## 隐私与安全

本地助手不传输对话正文，只把统计和你在 `config.json` 中填写的公开资料发给已配对设备。BLE RX/TX 特征要求加密访问。`config.json`、日志和虚拟环境均已被 Git 忽略，请不要把 CC Switch 数据库、token 或私人历史提交到仓库。

完整按键、配对、刷写和排障说明见 [设备与操作手册](docs/DEVICE_AND_USER_GUIDE.zh-CN.md)。

## 来源与许可证

本项目基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) 开发，并以独立发布快照维护；上游项目与原作者归属见此处链接及 [MIT License](LICENSE) 中的版权声明。硬件商标及第三方组件分别归其权利人所有。
