# MakotoPet · 橘真琴 AI 桌宠

把 **橘真琴（Makoto Tachibana）音色 TTS** 装进 ESP32-S3 的桌面宠物工程：

- **固件**：立创实战派 ESP32-S3（ESP-IDF 5.3.x），负责拾音、按键/触摸、字幕 UI、音频播放；
- **语音中枢**：PC 上的 Python 服务（FastAPI，端口 8765），负责 ASR → 大模型（可联网搜索）→ GPT-SoVITS 橘真琴音色；
- **音色管理网页**：上传参考音频即可新增音色，可试听、切换默认。

> GPT-SoVITS 的模型与运行时体积过大，未纳入 Git 仓库，补齐方法见下文。

---

## 目录结构

```
MakotoPet/
├─ makoto-pet/                 # 主项目：ESP32-S3 固件 + PC 语音中枢 + 文档
│  ├─ firmware/                # ESP-IDF 固件源码、字体、图标、背景资源、build.ps1
│  ├─ server/                  # FastAPI 语音中枢（ASR / LLM / TTS / 音乐 / 工具）
│  ├─ docs/                    # 字体与协议说明
│  ├─ scripts/                 # setup.ps1（环境安装）、package_release.ps1
│  └─ README.md                # 语音中枢与固件完整说明（建议先看）
├─ makoto/                     # GPT-SoVITS 训练/推理资产
│  ├─ dataset/                 # 原始人声 vocals.wav
│  ├─ logs/                    # 训练数据（音素标注、语义令牌、特征、32kHz 音频）
│  ├─ output/ demo/ selftest/  # 切片/降噪结果、推理样例、自检脚本
│  ├─ 使用说明.md              # 推理参数与常见坑
│  └─ say.py                   # 命令行合成脚本
├─ 资料/                       # 原理图、芯片手册、3D 打印文件、出厂测试固件、原版小智固件
├─ 工具/                       # 乐鑫 Flash Download Tool 3.9.7
├─ 素材/                       # 背景源图（当前为空）
└─ 00-项目总说明.md            # 目录整理与本地路径约定
```

---

## 未纳入仓库的大文件

GitHub 对单文件有 100 MB 硬限制，以下内容请自行补齐：

| 路径 | 大小 | 说明 |
|---|---|---|
| `makoto/GPT-SoVITS/` | 约 14 GB | GPT-SoVITS 框架 + 便携 Python runtime + 预训练基座 |
| `makoto/GPT_weights_v2/` | 约 450 MB | Makoto GPT 权重（e5 / e10 / e15） |
| `makoto/SoVITS_weights_v2/` | 约 165 MB | SoVITS v2 权重 |
| `makoto/SoVITS_weights_v3/` | 约 150 MB | SoVITS v3 LoRA 权重 |
| `makoto-pet/release/voice_models_makoto.zip` | 约 212 MB | 音色包（参考音频 + 权重） |
| `makoto-pet/server/.venv/` | 约 1 GB | 本机 Python 虚拟环境 |

### 补齐方法

1. **GPT-SoVITS 框架**
   从官方仓库 <https://github.com/RVC-Boss/GPT-SoVITS> 获取 Windows 整合包，
   放到 `makoto/GPT-SoVITS/`，需包含 `runtime/python.exe` 与
   `GPT_SoVITS/pretrained_models/gsv-v2final-pretrained/`。
   也可以放在别处，用环境变量 `GPT_SOVITS_DIR` 指向它（`start_gptsovits.bat` 会优先读取）。

2. **Makoto 音色权重**
   把以下文件按原目录放回 `makoto/` 下（你可以在我置顶另一个仓库找，也可以自己训练权重）：

   | 文件 | 放置目录 |
   |---|---|
   | `makoto-e5.ckpt` / `makoto-e10.ckpt` / `makoto-e15.ckpt` | `makoto/GPT_weights_v2/` |
   | `makoto_e4_s100.pth` / `makoto_e8_s200.pth` | `makoto/SoVITS_weights_v2/` |
   | `makoto_e1_s25_l32.pth` / `makoto_e2_s50_l32.pth` | `makoto/SoVITS_weights_v3/` |

   也可以采用 `makoto-pet/README.md` 中的 `voice_models/makoto/` 布局。

3. **虚拟环境与配置**
   运行 `makoto-pet/scripts/setup.ps1` 自动创建 `.venv`；
   或手动复制 `makoto-pet/server/config.example.yaml` 为 `config.yaml`，填入大模型 API Key。

---

## 快速启动（Windows）

1. 安装依赖并生成配置：

   ```powershell
   powershell -ExecutionPolicy Bypass -File makoto-pet\scripts\setup.ps1
   ```

2. 启动 GPT-SoVITS（端口 9880）：

   ```text
   makoto-pet\server\start_gptsovits.bat
   ```

3. 启动语音中枢（端口 8765）：

   ```text
   makoto-pet\server\run.bat
   ```

4. 浏览器打开 <http://127.0.0.1:8765>。

## 编译 / 烧录固件

```powershell
powershell -ExecutionPolicy Bypass -File "makoto-pet\firmware\build.ps1"
# 编译并烧录（COM 口按实际修改）
powershell -ExecutionPolicy Bypass -File "makoto-pet\firmware\build.ps1" -Flash -Port COM15
```

脚本会先把源码镜像到纯英文目录 `C:\makoto-pet-build` 再编译，避免 ESP-IDF 中文路径问题。

---

## 更多文档

- `makoto-pet/README.md` —— 语音中枢、音色管理、固件协议完整说明
- `makoto/使用说明.md` —— GPT-SoVITS 推理参数、参考音频选择与常见坑
- `00-项目总说明.md` —— 目录整理过程与本地路径约定



# 这个桌宠当然是给我好朋友陈婉儿定制的😋
诶好像做这种定制桌宠也蛮不戳的，好那么好，家人们，有了这次开发经验，我能赚钱养我的扳手与诗战队吗😋
