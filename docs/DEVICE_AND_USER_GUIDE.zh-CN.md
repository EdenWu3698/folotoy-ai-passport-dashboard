# FoloToy AI Passport Dashboard 设备与操作手册

## 1. 适用设备

本固件仅针对 FoloToy AI Passport 的 ESP32-C3 / 8 MB Flash / 240 × 320 ST7789P3 版本。其主要硬件为 ESP32-C3、520 mAh 电池、CW2017 电量计、USB-C、UP/DOWN/OK 三键、NTAG213 NFC 和 ES8311 音频 codec。

如果你的设备屏幕方向、Flash 容量、芯片或主板版本不同，请先核对原理图和分区，不要直接刷入预编译文件。

## 2. 开机与连接

1. 用 USB-C 给设备供电或充电；按住独立电源键约 0.5 秒开机、约 2 秒关机。
2. 开机后设备会以 `Passport-<MAC 后缀>` 的名称广播 BLE。
3. Mac 同步助手连接加密特征时，屏幕会显示六位配对码。
4. 在 macOS 蓝牙提示中输入该数字。配对成功后，密钥会保存在设备 NVS 与 macOS 钥匙串中。

同步依赖 BLE，不依赖 Wi-Fi，也不经过 Claude Desktop。

## 3. 页面和按键

- `UP`：上一页。
- `DOWN`：下一页。
- `OK`：普通仪表盘页面没有绑定动作；在系统确认界面中用于确认。

七个页面依次为：PROFILE、LINKS、FOCUS、HEAT、STAMPS、KIMI QUOTA、CODEX QUOTA。页面数据保存在 NVS，重新开机后继续显示最后一次同步结果。

## 4. 安装本地助手

需要 macOS 13 或更高版本、Python 3.11+ 和可用蓝牙：

```bash
cd host/ai-passport-sync
chmod +x setup.sh run.sh launch-agent.sh
./setup.sh
```

打开 `config.json` 修改姓名、签名、兴趣、GitHub 链接和数据路径。建议先查看本地预览：

```bash
./run.sh preview
```

开机并靠近 Mac，再同步：

```bash
./run.sh sync
```

查看设备与固件状态：

```bash
./run.sh status
```

## 5. 自动同步

安装 macOS LaunchAgent：

```bash
./launch-agent.sh install
```

它会在登录后自动运行，保持 BLE 会话并按以下频率更新：

- 每 15 秒：Kimi 与 Codex 配额。
- 每 5 分钟：个人资料、主题、热力图、印章和完整统计。

```bash
./launch-agent.sh status
./launch-agent.sh restart
./launch-agent.sh uninstall
```

运行文件、配置和日志位于 `~/Library/Application Support/AI Passport Sync/`。卸载命令只移除后台任务，保留配置和日志，方便恢复。

## 6. 数据来源

### Claude Code 项目统计

默认扫描 `~/.claude/projects/**/*.jsonl`。助手只解析项目路径、时间、消息 ID 和 usage 中的输出 token，用于项目主题、活跃天数和热力图；不会把对话正文发给设备。

### Kimi 配额

默认以只读方式打开 `~/.cc-switch/cc-switch.db`，找到 CC Switch 当前启用的 Claude Provider。仅当它是 Kimi For Coding 且配置有效时，才会读取 5 小时/7 天额度，并汇总当日请求与 token。

### Codex 配额

助手启动本机 `codex app-server`，读取当前登录账号的 rate limits。请先保证以下命令正常：

```bash
codex login status
```

如果 `codex` 不在 PATH，可在 `config.json` 的 `codex_command` 中填写绝对路径。

## 7. 刷写固件

### 校验文件

```bash
shasum -a 256 -c SHA256SUMS
```

### 写入完整镜像

```bash
python3 -m pip install esptool
python3 -m esptool --chip esp32c3 -p /dev/cu.usbmodem101 \
  write_flash 0x0 FoloToy-AI-Passport-Dashboard-full.bin
```

### 只写应用

只有确认设备已经使用本仓库的 `partitions.csv` 时，才写入 app 镜像：

```bash
python3 -m esptool --chip esp32c3 -p /dev/cu.usbmodem101 \
  write_flash 0x10000 FoloToy-AI-Passport-Dashboard-app.bin
```

不要执行整片擦除。布局保留 `cardid`（`0x356000`，16 KB）与 `recovery`（`0x700000`，1 MB）；完整发布镜像从 `0x0` 写入，但不会延伸到这两个区域。

## 8. 常见问题

### Mac 找不到设备

- 确认设备开机、有电并靠近 Mac。
- 关闭可能已连接它的其他同步进程或 BLE 调试软件。
- 运行 `./run.sh status` 重试；后台任务运行时不要同时手动同步。

### 配对码没有出现或一直失败

- 在 macOS 系统设置的蓝牙列表中忽略旧的 Passport 设备。
- 重新启动设备，再运行 `./run.sh sync`。
- 保持终端命令运行，等待 macOS 弹出输入框。

### Kimi 或 Codex 显示 NO DATA

- 先运行 `./run.sh preview`，它会分别报告数据是否可读。
- Kimi：检查 CC Switch 当前 Provider、数据库路径和网络连接。
- Codex：检查 Codex CLI 已登录、命令路径和版本是否支持 app-server 配额读取。
- 某个来源不可用不会影响其他页面。

### 二维码不易识别

- 提高屏幕亮度、避免反光，保持镜头正对屏幕。
- 二维码内容越短，模块越大越容易识别。
- 修改 URL 并重建后，用实际目标手机测试。

### 想恢复原厂固件

刷机前自行备份完整 Flash，或使用 FoloToy 官方提供的、与你硬件版本匹配的恢复固件。本仓库不包含设备专属 `cardid`，也不会替你恢复唯一设备数据。

## 9. BLE 协议信息

设备使用 Nordic UART Service：

| 项目 | UUID |
| --- | --- |
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX（Mac → 设备） | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX（设备 → Mac） | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

RX、TX 和 TX CCCD 需要加密访问。应用协议为按换行分隔的 UTF-8 JSON；实现细节见 `main/passport_protocol.c` 与本地助手源码。
