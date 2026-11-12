# 发布到 GitHub（私有仓库）

本仓库只提交源码、文档和界面素材，不提交模型权重、虚拟环境、曲库和本地配置。
发布后，协作者按 README 的「从 GitHub 克隆运行」操作即可。

## 0. 发布前检查

```powershell
cd makoto-pet
git status --short
```

确认以下文件**没有**被 `git add`：

- `server/config.yaml`（含大模型 Key）
- `server/data/voices.json`（含本机绝对路径）
- `server/.venv/`
- `server/music/`
- `firmware/build/`、`firmware/build_out/`、`firmware/managed_components/`
- 任何 `*.ckpt` / `*.pth` / `voice_models/`

`.gitignore` 已覆盖这些路径。可以用下面的命令复查：

```powershell
git ls-files | Select-String -Pattern "config.yaml|voices.json|\.venv|music/|\.ckpt|\.pth|managed_components|build_out"
```

> 如果 `server/config.yaml` 曾经被推送到任何公开位置，请立即去大模型后台作废并重发 Key。

## 1. 初始化并推送

```powershell
cd makoto-pet
git init
git add .
git commit -m "Initial commit: Makoto Pet firmware + server"
git branch -M main
git remote add origin git@github.com:<owner>/<repo>.git
git push -u origin main
```

没有配置 SSH 的话，也可以用 HTTPS + Personal Access Token：

```powershell
git remote add origin https://github.com/<owner>/<repo>.git
git push -u origin main
```

## 2. 打 Release 资产

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1
```

会在 `release\` 生成：

| 文件 | 内容 | 约大小 |
|---|---|---|
| `voice_models_makoto.zip` | `voice_models/makoto/` 下的 ref.wav + 两个权重 | 230 MB |
| `makoto_pet_firmware.zip` | merged-binary / bootloader / partition-table / makoto_pet.bin + FLASH.md | 8 MB |

然后到 GitHub 仓库 → **Releases** → **Draft a new release**：

1. Tag 填 `v1.0.0`；
2. 把上面两个 zip 拖进附件；
3. 发布。

## 3. 私有仓库的下载方式

私有仓库的 Release 附件**不能匿名直接下载**，协作者需要：

- 已安装 GitHub CLI 并 `gh auth login`，然后运行：
  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -RepoSlug <owner>/<repo>
  ```
- 或者在浏览器里登录 GitHub，手动下载 `voice_models_makoto.zip`，再：
  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -VoicePackZip <zip路径>
  ```

## 4. 协作者首次运行

```powershell
git clone git@github.com:<owner>/<repo>.git makoto-pet
cd makoto-pet
powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -RepoSlug <owner>/<repo>
```

然后：

1. 安装 GPT-SoVITS 到 `makoto-pet` 同级目录（或设置 `GPT_SOVITS_DIR`）；
2. 填 `server\config.yaml` 的 `llm.api_key`，或设置 `MAKOTO_LLM_KEY`；
3. 双击 `server\start_gptsovits.bat`，再双击 `server\run.bat`。

## 5. 如果要转为公开仓库

当前素材包含《Free!》橘真琴、《蜡笔小新》、微软雅黑字体位图和角色音色权重，
仅适合私有/个人学习。公开前至少需要：

- 替换 `firmware/tools/assets/`、`firmware/tools/shinchan_*` 为可公开的素材；
- 用开源字体（如 Noto Sans CJK）重新生成 `firmware/main/font_cn_16.c`；
- 删除或替换 `server/music/` 与音色权重；
- 在 README 明确素材来源和许可证。

否则建议保持私有。
