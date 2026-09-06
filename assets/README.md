# 图像资源

仓库默认使用由脚本生成的原创通用机器人头像，不包含任何私人照片。

生成默认头像：

```bash
python3 tools/generate_avatar_asset.py
```

将自己的照片转换成 48 × 48、16 色像素头像：

```bash
python3 tools/generate_avatar_asset.py --input /path/to/avatar.png
```

脚本会更新：

- `assets/images/avatar-48x48-passport-palette.png`：预览图。
- `main/passport_avatar.h`：编译进固件的 RGB565 数据。

请只使用你有权处理和发布的图片。生成文件沿用本仓库 MIT 许可证；输入照片的版权不会因此改变。
