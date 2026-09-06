# AI Passport 本地同步助手

这个 macOS 助手通过加密 BLE 直接连接护照，不依赖 Claude Desktop，也不会把本地数据上传到第三方服务器。

它读取三类本地信息：

- Claude Code 的本地 JSONL 历史，只统计日期、项目名、会话数和输出 token，不读取或发送对话正文。
- CC Switch 的只读 SQLite 数据库，用于 Kimi 5 小时/7 天配额和当日请求、token 统计。
- 已登录的 Codex CLI 本地 app-server，只调用只读配额接口。

## 首次安装

```bash
cd host/ai-passport-sync
chmod +x setup.sh run.sh launch-agent.sh
./setup.sh
```

编辑 `config.json` 后先预览：

```bash
./run.sh preview
```

打开护照并同步：

```bash
./run.sh sync
```

首次连接时护照会显示六位配对码，在 macOS 蓝牙提示中输入即可。

## 开机自动同步

```bash
./launch-agent.sh install
```

默认保持 BLE 连接，配额每 15 秒刷新，个人/项目/热力图每 5 分钟刷新。管理命令：

```bash
./launch-agent.sh status
./launch-agent.sh restart
./launch-agent.sh uninstall
```

日志和实际运行配置位于 `~/Library/Application Support/AI Passport Sync/`。重新执行 `install` 会把当前目录中的 `config.json` 同步过去。

## 数据缺失排查

- Kimi 显示无数据：确认 CC Switch 当前 Claude Provider 是 Kimi For Coding，且数据库路径正确。
- Codex 显示无数据：确认 `codex login status` 正常，并且 `codex` 位于 `PATH`；也可在配置里写绝对路径。
- 找不到设备：保持设备开机，靠近 Mac，关闭其他已占用该 BLE 连接的程序。
- 配对失败：在 macOS 蓝牙设置里忽略旧设备，再重新运行 `./run.sh sync`。
