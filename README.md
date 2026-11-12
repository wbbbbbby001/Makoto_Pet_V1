# 橘真琴 AI 桌宠 · Makoto Pet

比原版固件（`v1.4.6_lichuang-dev.zip`）更完整的一套自托管方案：

- **固件**（ESP-IDF，跑在立创实战派 ESP32-S3）负责拾音、按键/触摸、字幕 UI、音频播放；
- **语音中枢**（PC 上的 Python 服务）负责 ASR → 大模型（**可联网搜索**）→ GPT-SoVITS（**橘真琴音色**）；
- **音色管理网页**：上传一段参考音频就能新增音色，可试听、可切换默认。

> 和原版固件的区别：不依赖官方云、不锁音色、搜索在本地大模型工具里完成，
> 音色直接吃你 `makoto/` 里已经训好的 GPT-SoVITS 权重。

---

## 从 GitHub 克隆运行（Windows）

> 仓库里只有源码和界面素材，**不包含** 12GB 的 GPT-SoVITS 运行时、预训练模型和 230MB 音色权重。
> 完整流程是：克隆 → 装依赖 → 下载音色包 → 安装 GPT-SoVITS → 填 Key → 启动。

### 前置条件
- Windows 10/11 x64
- Python 3.10+（安装时勾选 **Add Python to PATH**）
- Git
- 一个 OpenAI 兼容大模型 Key（DeepSeek / 通义 / Ollama 等）
- 只有编译固件才需要：ESP-IDF **5.3.x** + 立创实战派 ESP32-S3

### 推荐目录布局
```
<parent>/
├─ makoto-pet/            # git clone 下来的仓库
├─ GPT-SoVITS/            # 官方 Windows 包（含 runtime\python.exe、
│                         #   GPT_SoVITS\pretrained_models\gsv-v2final-pretrained\）
└─ voice_models/makoto/   # 从 GitHub Release 下载的音色包
   ├─ ref.wav
   ├─ makoto-e15.ckpt
   └─ makoto_e8_s200.pth
```

### 1. 克隆 + 装依赖
```powershell
git clone <你的私有仓库地址> makoto-pet
cd makoto-pet
powershell -ExecutionPolicy Bypass -File scripts\setup.ps1
```
`setup.ps1` 会自动创建 `server\.venv`、安装依赖、生成 `server\config.yaml`。

### 2. 下载音色包
从仓库的 **Releases** 下载 `voice_models_makoto.zip`，解压到 `..\voice_models\`
（和 `makoto-pet` 同级）。私有仓库可以用：
```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -RepoSlug <owner>/<repo>
```
需要本机已安装 GitHub CLI 并执行过 `gh auth login`。

### 3. 安装 GPT-SoVITS
使用官方 Windows 整合包，放到 `makoto-pet` 同级目录并命名为 `GPT-SoVITS`；
它需要包含 `runtime\python.exe` 和
`GPT_SoVITS\pretrained_models\gsv-v2final-pretrained\`。
放在别处也可以，设置环境变量 `GPT_SOVITS_DIR` 指向该目录。

### 4. 填大模型 Key
编辑 `server\config.yaml` 里的 `llm.api_key`，或设置环境变量：
```powershell
setx MAKOTO_LLM_KEY "sk-..."
```
`setx` 之后需要重开终端。

### 5. 启动
1. 双击 `server\start_gptsovits.bat`，等日志出现 `Uvicorn running on http://127.0.0.1:9880`；
2. 双击 `server\run.bat`，浏览器打开 <http://127.0.0.1:8765>。

### 固件
- **直接烧录**：从 Releases 下载 `makoto_pet_firmware.zip`，按压缩包里的 `FLASH.md`
  用 esptool 烧到 `0x0`。
- **自己编译**：安装 ESP-IDF 5.3.x 后运行
  ```powershell
  powershell -ExecutionPolicy Bypass -File firmware\build.ps1
  ```
  脚本默认用作者机器的 IDF 路径；其他机器请设置 `IDF_PATH` / `IDF_TOOLS_PATH` /
  `IDF_PYTHON_ENV_PATH`，或用 `-IdfPath` / `-IdfTools` / `-PyEnv` 参数指定。
  `idf.py` 首次编译会自动下载 `managed_components`（约 100MB），需要联网。

### 仓库里没有的东西
- `makoto/`：15GB 的 GPT-SoVITS 运行时、预训练模型、数据集和训练权重 → 用官方 GPT-SoVITS + Release 音色包
- `server/.venv`、`server/music`、`server/config.yaml`、`server/data/voices.json` 等本地/版权内容
- 固件 `build/`、`build_out/`、`managed_components/` 构建产物

### 直接把整个文件夹拷给别人

可以，但建议先做三件事：

1. 删掉 / 清空 `server\config.yaml`（里面有你的大模型 Key），让对方复制
   `config.example.yaml` 后填自己的；
2. 删掉 `server\music\`（版权歌曲，且体积大）；
3. 保持 `makoto-pet` 和 `makoto`（或 `GPT-SoVITS`）同级，不要只拷 `makoto-pet`。

`server\.venv` 可以一起拷：`run.bat` 会检测它在对方电脑是否可用，不可用会自动删除重建
（对方需要装 Python 3.10+，首次会联网装依赖）。
`server\data\voices.json` 现在存的是相对路径，拷走后会自动迁移，不需要手改。

> **版权提醒**：本仓库包含《Free!》橘真琴、《蜡笔小新》图片、微软雅黑字体位图、
> 以及用角色声音训练的音色权重，仅限个人学习使用，请勿公开分发或商用。
> 如果要转为公开仓库，请先替换这些素材。

> 下面第 0 节之后是作者本地布局的详细说明；其中出现 `makoto/GPT-SoVITS` 的地方，
> GitHub 布局对应 `..\GPT-SoVITS`，音色权重对应 `..\voice_models\makoto`。

---

## 0. v2 整合（2026-11-10）· 飞书文档对照

飞书那份「立创实战派 ESP32-S3 资料」本质是**资料/使用说明页**：原版配网、绑定、
唤醒、烧录、硬件参数、ESP-IDF 环境，以及一个**第三方收费的“五合一/音乐固件”入口**；
它没有软件功能规格。板子的真实能力对应官方 14 个例程。

本版在自研固件上补齐了下面这些，**不再使用原版固件/官方云**：

| 能力 | 本版状态 |
|---|---|
| 橘真琴语音（GPT-SoVITS） | ✅ 已有，服务端 `voices/` 上传、切换 |
| 联网搜索 / 天气 / 网页正文 | ✅ 已有，`web_search` 等工具 |
| 触摸屏桌面 Launcher | ✅ 本版新增：AI / 小游戏 / 记事本 / 设置 / 音乐 / 相机 |
| 2048 / 贪吃蛇 | ✅ 本版新增，触屏方向键、分数、重开 |
| 记事本 | ✅ 本版新增，NVS 存储；拼音候选每键 32 个、每页 16 个，支持 `<` `>` 翻页；AI 也能说“记一下……”写入 |
| 设置 | ✅ 亮度 / 音量 / Wi-Fi 信息 / 重新配网 / 重启 |
| 网页控制台 | ✅ 本版新增“设备控制台”：在线状态、音量/亮度、远程开应用、屏幕显示文字、写记事本、重启 |
| Wi-Fi 配网（热点 192.168.4.1） | ✅ 已有 |
| 按住说话（BOOT/屏幕） | ✅ 已有 |
| 本地唤醒词 | ⏳ 第二批（esp-sr AFE + model 分区） |
| 摄像头预览 / 拍照到 TF 卡 | ✅ 本版新增（GC2145 自动识别，VGA 拍照，失败自动降 QVGA，JPEG/BMP） |
| 音乐播放器 | ✅ v1：本地曲库优先；ccMixter 在线歌曲先缓存再播；16k/2.5s 预缓冲；播放期间进入音乐模式，不接收语音 |
| 六轴姿态显示 | ⏳ 第二批（BSP 里 QMI8658 已就绪） |
| BLE HID / OTA | ⏳ 第三批（OTA 需要改双 app 分区） |

> 说明：飞书里的“五合一音乐固件”是第三方收费固件，本工程不是复刻它，
> 而是按同样思路自己做桌面应用；摄像头/音乐已接，BLE 后续补。
>
> 音乐 v1 为了流畅做了几件事：16k 采样率（这块 ES8311 已验证）、2.5 秒预缓冲、
> WebSocket 分片重组、在线歌曲先完整缓存到 `server/music/cache` 再播；
> 播放期间设备进入音乐模式，BOOT 短按暂停、长按停止音乐并退出音乐模式。
> 设备点“播放”时按 `music.play_order`（当前配置为 Sugar、Baby）开始，其余按歌名排序；
> `music.auto_next: true` 时一首播完自动下一首。

**语言**：默认用日语回答（真琴的母语）。用户明确说“用中文 / 说中文 / 中文回答”
才切换简体中文，并在这个设备会话里保持；说“用日语 / 日语回答”再切回。
网页的“测试对话”每次请求单独判断，默认也是日语。

**屏幕字体**：设备端不再用 LVGL 自带的 1000 字子集，改用
`main/font_cn_16.c`（微软雅黑 16px / 2bpp，ASCII + 假名 + 全部 CJK），
解决「橘、琴、桌、游、戏、记、设、乐、说、话」等字显示方框的问题。
生成命令见 `docs/font.md`；`sdkconfig.defaults` 里已打开
`CONFIG_LV_FONT_FMT_TXT_LARGE=y`（字体位图超过 1MB 必须开）。

**桌面图标**：全部是**透明底、无圆角底板**的浮空图标——AI 助手是从
`firmware/tools/shinchan_src.png` 抠出来的小新头像，其余是 Fluent 彩色 emoji
（音乐音符改成亮青色 `0x39d0ff`）；按下时也不会有底色或阴影。想换图标就改
`firmware/tools/make_desktop_icons.py` 里的 emoji 映射 / 小新裁切框，运行
`server\.venv\Scripts\python.exe firmware\tools\make_desktop_icons.py`
重新生成 `main/icon_*.bin`，再编译烧录。预览图在 `main/_icon_preview/launcher_preview.png`。

**聊天区**：AI 对话页是一个可上下滑动的消息列表（你=青色右对齐，真琴=白色左对齐），
新消息自动滚到底；在列表区域向上滑可查看历史，字体大小不变。

**页面背景 / 待机画面**：每个页面有独立背景（都是 320×240 RGB565，
通过 CMake 的 `EMBED_FILES` 打进固件，不占 RAM）：

| 页面 | 素材 | 源图 |
|---|---|---|
| 主桌面 Launcher | `main/desktop_bg.bin` | `01.jpg` |
| 音乐播放器 | `main/music_bg.bin` | `02.jpg` |
| AI 助手 / 聊天页 | `main/ai_bg.bin` | `03.png` |
| 开机待机画面 | `main/makoto_standby.bin` | `makoto壁纸.jpg` |

换前三个页面的图：把图片交给
`server\.venv\Scripts\python.exe firmware\tools\make_app_bgs.py 桌面图 音乐图 AI图`
（不传参就用上面的默认路径），会统一做「铺满裁切 + 压暗 + 上下渐变」，保证
白色文字可读；再编译烧录。待机画面仍用
`firmware\tools\make_makoto_bg.py` 生成 `makoto_standby.bin`。

开机后显示整屏待机图，顶部是 **Broadway 艺术字时间**（HH:MM，白色、轻微透明、无底板），
**向上滑解锁进入桌面**：拖动时数字跟着手指上移，松手后（上滑 ≥45px）整屏淡出，
不会出现从上到下的刷屏；普通点击或左右滑不会解锁。桌面 Launcher 闲置 30 秒会回到待机
（打开应用或语音交互时不会打断）。桌面顶栏标题为 `makoto`。聊天框保持半透明，
图片会透出来但文字仍然可读。

**记事本拼音输入（蜡笔小新皮肤）**：记事本默认「中」模式，直接用英文键盘打拼音
（例如 `woxiangchifan`、`beijingshifandaxuexiaozhang`）。输入框位置会弹出
**首选完整预览 + 16 个候选**：候选区可以**横向滑动**查看更多，点预览或候选上屏，
空格上屏第一个；右上角 `中/英` 可切回英文直输。候选由 PC 端**本地词库**
（wordfreq 6 万词 + jieba 前 15 万高频词，共 11 万+ 拼音键）做 DP 分词计算，
不经过大模型，通常 0~60ms 返回；服务端没连上时会把拼音原样写入，不会卡住。
词库首次启动构建约 8~9 秒并缓存到 `server/data/pinyin_dict.pkl`（约 4.8MB），之后直接加载。

键盘是**真实的蜡笔小新图片皮肤**：背景是**春日部防卫队五人**（小新、风间、妮妮、
正男、阿呆）+ **全家福**（野原一家和小白），按键半透明黄色，能看到后面的人物；
空格键上也有小新头像；标题栏左侧是 28×28 的小新头像。素材由
`firmware/tools/make_shinchan_assets.py` 从 `shinchan_src.png`、
`shinchan_friends_src.jpg`、`shinchan_family_src.png` 生成。

**相机 / TF 卡拍照（2026-11-11 新增）**：桌面点「相机」进入 GC2145 实时预览
（传感器 VGA 画面在内存里缩到 320×240 显示，不改 sensor 分辨率，拍照仍是 VGA）。
相机界面里 **轻按 BOOT（<3 秒）拍一张**，照片存到 TF 卡
`/sdcard/DCIM/IMG_0001.JPG`（JPEG 编码失败会自动改存 `.BMP`，保证有文件）。
**按住 BOOT 满 3 秒才启动橘真琴语音对话**：满 3 秒后继续按住说话，松开结束；
短按 BOOT 在其他界面不会触发 AI。屏幕底部也有「拍照」按钮，网页控制台新增「相机 / 拍照」。
TF 卡走 SDMMC 1 线（CLK=GPIO47 / CMD=GPIO48 / D0=GPIO21），未插卡时预览照常，
拍照会提示「TF 卡未插入」。相机模块只在打开相机时初始化，返回桌面会自动关摄像头省电。

> **实测硬件说明**：这块板子的摄像头是 **GC2145**（I2C 0x3C），不是资料里写的 GC0308；
> 驱动会自动识别。VGA + RGB565 必须开 `CONFIG_CAMERA_PSRAM_DMA=y`，否则内部 RAM 里
> 30KB 连续 DMA 缓冲分配失败，日志报 `cam_dma_config failed`。
> 显示侧 LVGL 绘制缓冲改成 **8 行内部 DMA**（5.1KB）：原来 20 行放在 PSRAM 时，
> SPI 每次 flush 要临时申请 12.8KB 内部 DMA 缓冲，内部内存不够会 flush 失败，
> 进而让 LVGL 任务卡死（task_wdt 打印 LVGL task backtrace）。
>
> 设备跑一段时间后内部 RAM 会碎片化，所以相机初始化任务/预览任务/拍照任务的栈
> 现在优先/自动放 PSRAM；VGA 初始化失败会自动用 QVGA 再试一次。开相机失败时，
> 屏幕 toast 会显示 `相机失败:<原因>`，网页 `/api/device` 的 `camera_err` 也能看到。

**音频输出**：设备端用 **32-bit I2S 立体声**（单声道 PCM 扩成 L/R），
服务端默认 **关闭流式 TTS、重采样到 16k** 再发送，避免 ES8311 在 32k 流式分片下
出现刺耳杂音；首包会比流式略慢。`tts.stream` / `tts.playback_rate` 可调。

**知识库文档上传**：支持 PDF / Word(.docx) / Markdown / TXT / 图片
（PNG / JPG / WEBP / BMP）。后台解析并在网页显示进度条，完成后自动切块入库。
- 普通 PDF 用 `pypdf` 按页提取文字；
- 图片和**没有文字层的扫描版 PDF** 用 `config.yaml` 的 `vision` 配置识别：
  默认复用 `llm` 的 base_url / api_key / model（实测当前 DeepSeek Key 可直接识图）；
  扫描版 PDF 会用 PyMuPDF 逐页渲染成图片再识别；
- 旧版 `.doc` 请另存为 `.docx`；单文件上限 30MB；扫描版页数越多，识别时间和 token 消耗越高。

---

## 1. 架构

```
┌──────────────────────── 立创实战派 ESP32-S3 ────────────────────────┐
│  ES7210 麦克风 ──16k PCM16──┐                                       │
│  ES8311 喇叭   ◀──32k PCM16─┘      LVGL 320×240 触摸屏（状态/字幕）   │
│  BOOT 键 / 屏幕按钮（按住说话）                                      │
│                            WebSocket (ws://电脑IP:8765/ws/device)    │
└───────────────────────────────────┬─────────────────────────────────┘
                                    │ JSON 控制 + 裸 PCM 二进制
┌───────────────────────────────────▼─────────────────────────────────┐
│                         Makoto Brain (PC)                           │
│  faster-whisper ASR                                                  │
│        ↓                                                             │
│  LLM（DeepSeek/通义/Ollama…，OpenAI 兼容）                           │
│    └─ 工具：web_search / fetch_page / get_weather / switch_voice     │
│        ↓                                                             │
│  GPT-SoVITS API (127.0.0.1:9880) ── 橘真琴权重 + 上传的参考音频       │
│        ↓                                                             │
│  Web 管理页 http://电脑IP:8765                                       │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 2. 目录

```
makoto-pet/
├── firmware/                 ESP-IDF 5.3 工程（idf.py build）
│   ├── components/
│   │   └── esp32-camera/     ★ 本地修改版（SCCB 改用新版 I2C，见 4.4）
│   ├── main/
│   │   ├── app_main.c        状态机、BOOT 键（长按 3 秒进入语音）
│   │   ├── camera_app.c      GC2145/GC0308 预览 / VGA 拍照 / JPEG·BMP 保存
│   │   ├── sd_card.c         TF 卡挂载（SDMMC 1 线）
│   │   ├── wifi.c            STA + 热点配网（192.168.4.1）
│   │   ├── net_ws.c          WebSocket 客户端、JSON 协议
│   │   ├── audio_io.c        麦克风采集 / 播放环形缓冲
│   │   ├── ui_app.c          LVGL 界面
│   │   └── bsp/              立创实战派板级支持（取自官方例程）
│   └── partitions.csv
├── server/                   PC 端语音中枢
│   ├── config.example.yaml   ★ 配置模板（复制为 config.yaml 后填 Key）
│   ├── config.yaml           本地配置（已 gitignore，含 Key/绝对路径）
│   ├── requirements.txt      依赖（>=）
│   ├── requirements-lock.txt 本机验证过的精确版本
│   ├── run.bat               一键建 venv + 启动
│   ├── start_gptsovits.bat   一键启动 GPT-SoVITS api_v2
│   ├── app/                  FastAPI + WebSocket + ASR/LLM/TTS
│   └── web/index.html        音色管理网页
├── scripts/
│   ├── setup.ps1             克隆后一键初始化
│   └── package_release.ps1   打 Release 资产
└── docs/protocol.md          设备与服务端的 WS 协议

外部目录（不进仓库）：
  ../GPT-SoVITS/              GPT-SoVITS 官方安装
  ../voice_models/makoto/     Release 音色包
```

---

## 3. 最快跑通（不接硬件，先在电脑上听声音）

### 3.1 启动 GPT-SoVITS

双击 `server/start_gptsovits.bat`。
等日志出现 `Uvicorn running on http://127.0.0.1:9880`。

> 它内部跑的是 `GPT-SoVITS/api_v2.py`（默认找仓库上一级的 `GPT-SoVITS`，
> 也可以用环境变量 `GPT_SOVITS_DIR` 指定），权重由语音中枢按音色档案自动切换。

### 3.2 启动语音中枢

双击 `server/run.bat`。第一次会自动创建 `.venv` 并装依赖（faster-whisper 首次会下载模型）。

### 3.3 填大模型 Key

编辑 `server/config.yaml`：

```yaml
llm:
  base_url: "https://api.deepseek.com/v1"
  api_key: "sk-..."          # 填你的
  model: "deepseek-chat"
```

也可以用环境变量 `MAKOTO_LLM_KEY`。用 Ollama 的话：

```yaml
llm:
  base_url: "http://127.0.0.1:11434/v1"
  api_key: "ollama"
  model: "qwen2.5:7b"
```

改完重启 `run.bat`。

### 3.4 打开管理页

浏览器打开 **http://127.0.0.1:8765** ：

1. 看到「橘真琴」音色卡片，点 **试听** —— 应该听到真琴的声音；
2. 在「测试对话」里问「帮我查一下今天深圳的天气」—— 走完整链路，会调用联网搜索再合成；
3. 设备连上后，页面里的 **设备控制台** 会自动出现：
   - 看在线状态、Wi-Fi、音量、亮度；
   - 拖音量/亮度滑杆会实时下发到设备；
   - “打开应用”可以远程切到桌面/2048/贪吃蛇/记事本/设置；
   - “显示文字”会在设备桌面弹提示，“写入记事本”会追加到设备 NVS；
   - 可以远程重启设备。

---

## 4. 编译烧录固件

### 4.1 环境

- ESP-IDF **5.3.x**（路径按你的安装位置；本机作者装在 `D:\Espressif\...`）
- ⚠️ 工程路径尽量避免中文。ESP-IDF 的 kconfgen 在中文路径下可能用 GBK 读配置而崩溃；
  `build.ps1` 会先把源码镜像到纯英文目录 `C:\makoto-pet-build` 再编译。

直接用自带脚本：

```powershell
powershell -ExecutionPolicy Bypass -File firmware\build.ps1
```

脚本默认使用作者机器的 IDF 路径；其他机器请设置 `IDF_PATH` / `IDF_TOOLS_PATH` /
`IDF_PYTHON_ENV_PATH`，或用 `-IdfPath` / `-IdfTools` / `-PyEnv` 参数指定。

烧录（把 COM3 换成你的串口）：

```powershell
powershell -ExecutionPolicy Bypass -File firmware\build.ps1 -Flash -Port COM3
```

也可以手动：

```powershell
$env:IDF_TOOLS_PATH='D:\Espressif\Espressif'
$env:IDF_PATH='D:\Espressif\Espressif\frameworks\esp-idf-v5.3.1'
$env:IDF_PYTHON_ENV_PATH='D:\Espressif\Espressif\python_env\idf5.3_py3.11_env'
$env:PYTHONUTF8='1'
. "$env:IDF_PATH\export.ps1"
cd C:\makoto-pet-build     # 源码镜像目录，不能是中文路径
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

> 为什么必须镜像：ESP-IDF 5.3 的 kconfgen / objdump 在中文路径下会按 GBK 解码，
> 把「立创实战派」解成乱码，链接阶段报 `libxtensa.a: No such file or directory`。
> 这是工具链问题，不是代码问题。

### 4.2 改 Wi-Fi 和服务器地址（推荐用配网，不改代码）

烧录后第一次开机：

1. 板子连不上 Wi-Fi 会自己开热点 **`Makoto-Pet-XXXX`**（密码 `makoto1234`）；
2. 手机/电脑连这个热点，浏览器打开 **http://192.168.4.1**；
3. 填家里 Wi-Fi、密码、语音中枢地址（形如 `ws://192.168.1.100:8765/ws/device`，IP 见管理页顶部）；
4. 保存后自动重启并连接。

也可以直接在 `idf.py menuconfig → Makoto Pet 配置` 里写死。

### 4.3 关于 `components/esp32-camera`（重要）

本工程 BSP 用的是 **新版 `i2c_master` 驱动**（触摸、PCA9557、音频共用 I2C0）。
官方 `espressif/esp32-camera` 2.1.x 在 **IDF 5.3** 上会强制编译旧版 `driver/i2c.h`
的 SCCB，而旧版驱动有一个启动构造函数：一旦检测到新版 I2C 驱动也被链接，
就直接 `abort()`（日志 `i2c: CONFLICT! driver_ng ...`），表现为**开机无限重启**。

所以这里把摄像头组件放进了 `firmware/components/esp32-camera`，并做了三处本地修改：

1. `CMakeLists.txt`：IDF 5.3 也使用 `driver/sccb-ng.c`（新版 I2C）；
2. `sccb-ng.c`：去掉 IDF 5.4 才有的 `i2c_master_get_bus_handle()` 和
   `esp_private/i2c_platform.h`，改用 `g_sccb_bus_handle`；
3. `camera_app.c` 在 `esp_camera_init()` 前把 `bsp_i2c_get_bus()` 的句柄赋给该变量。

**不要**再在 `main/idf_component.yml` 里声明 `espressif/esp32-camera`，
否则会出现两份驱动并再次启动崩溃。升级到 IDF 5.4+ 后可以直接换回官方组件。

### 4.4 操作

- **BOOT 键按住满 3 秒开始说话**，松开发送；不满 3 秒不启动 AI；
- **相机界面里短按 BOOT = 拍照**（不满 3 秒），长按满 3 秒仍然是语音对话；
- 屏幕底部 **按住说话** 按钮保持原来的即时按住说话（不受 3 秒限制）；说话中再按一次 = 打断；
- 顶部状态：待机 / 聆听中 / 思考中 / 说话中；中间显示识别文字和回复。

---

## 5. 音色怎么“自己上传”

管理页「上传新音色」：

| 项 | 说明 |
|---|---|
| 参考音频 | **3–10 秒、单人、无背景音乐**，wav/mp3/m4a 都行；服务端自动转 32k 单声道并截前 10 秒 |
| 参考文本 | **必须填这段音频里念的原文**，不要填音素串（这是 GPT-SoVITS 最容易踩的坑） |
| 参考语种 | 日文音频选日文，中文选中文 |
| 合成语种 | 一般选「自动判断」 |
| 权重路径 | 可选。上传零样本克隆不用填；如果你另训了 GPT/SoVITS 权重，填绝对路径就会切过去 |

上传后点「试听」验证；满意就「设为默认」，设备下次连接自动使用，也可以直接对设备说「换成 XX 音色」（大模型会调 `switch_voice`）。

> 实测建议沿用 `makoto/使用说明.md` 的参数：`top_k=15 / top_p=1 / 温度=1`，
> 参考文本填日文原文。`server/config.yaml` 里预置的橘真琴音色已经按这套配好了。

---

## 6. 联网搜索

大模型可以调用三个工具：

- `web_search`：默认 `auto` —— 先用 **cn.bing.com**（国内直连、免 Key），失败再退 DuckDuckGo；
  也可以在 `config.yaml` 换 `tavily`（要 Key）或自建 `searxng`；
- `fetch_page`：抓网页正文（trafilatura）；
- `get_weather`：wttr.in 天气（实测可直连）。

搜索预算在 `config.yaml` 的 `llm.max_tool_rounds`（默认 20 轮）和
`llm.max_tool_seconds`（默认 180 秒，硬截止：到点会取消正在进行的搜索/大模型请求）里调。
**两个上限触发后的行为一样**：不硬编，先说明查到的线索并向用户追问一个关键信息
（全名/地区/年份等）；用户回答后带着历史继续搜，下一轮重新获得完整预算。

回答长度：`config.yaml` 里 `llm.max_tokens` 默认 **1536**，提示词要求默认写 **300~800 字**、
先结论再分点；只有用户说“简短点/一句话”才压缩。搜索默认每次取 8 条，并鼓励看 2~3 个来源。
注意：回答越长，GPT-SoVITS 逐句合成和语音播放的时间也越长；觉得太慢可以说“简短点”，
或把 `max_tokens` 调低。

服务端在调用搜索时会先给设备发 `{"type":"tool","name":"web_search"}`，屏幕上会显示「正在联网搜索…」。

---

## 7. 常见问题

| 现象 | 原因 / 解决 |
|---|---|
| 管理页 GPT-SoVITS 灯不亮 | `start_gptsovits.bat` 没启动，或 9880 被占用 |
| 试听报 400 / 合成很长但没声音 | 参考文本填成了音素串；改填原文 |
| 中文回答只有零点几秒 | 音色档案 `text_lang` 改成 `auto` 或 `zh` |
| 设备一直“配网” | Wi-Fi 密码错 / 2.4G 与 5G 不同名；重新进 192.168.4.1 配网 |
| 设备连不上服务 | 电脑防火墙拦了 8765；放行 Python 或关闭专用网络防火墙；确认同一网段 |
| 编译报 kconfgen GBK 错 / libxtensa.a not found | 用了中文路径，改用 `firmware\build.ps1` 的英文镜像目录 |
| faster-whisper 首次下载模型失败 | 已默认设置 `HF_ENDPOINT=https://hf-mirror.com`；仍失败就手动 `pip install` 后把模型放到 HF 缓存 |
| 相机界面黑屏 / 提示初始化失败 | 检查摄像头排线；相机和触摸/音频共用 I2C0，初始化失败时日志会打印 `camera init failed` |
| 相机预览正常但拍照提示 TF 卡 | TF 卡没插好、卡是 exFAT/NTFS、或卡未格式化（固件不自动格式化）；换成 FAT32 卡 |
| BOOT 短按没反应 | 短按只在相机界面拍照；其他界面短按不启动 AI，需要按住满 3 秒 |
| 找不到拍的照片 | TF 卡里 `/DCIM/IMG_xxxx.JPG`；JPEG 编码失败时会存成 `.BMP` |
| faster-whisper CUDA 报 cuDNN | 代码会自动退回 CPU(int8)；想用 GPU 需装 CUDA 12 + cuDNN 9 |
| 联网搜索超时 | 默认走 cn.bing.com；若被网络环境影响，改用 `tavily`（要 Key）或本地 `searxng` |
| 首次对话很慢 | Whisper 模型首次加载 + GPT-SoVITS 首次推理预热，之后就快了 |

---

## 8. 这一版没做的（下一步可加）

- **本地唤醒词**：`main/Kconfig.projbuild` 已留 `MAKOTO_ENABLE_WAKE_WORD` 开关，
  但默认关闭。要做的话把 `szpi-s3-esp/12-speech_recognition` 的 `app_sr.c`
  和 model 分区搬过来即可（esp-sr 的唤醒词模型，官方例程已带）。
- **Opus 编码**：当前是裸 PCM（局域网 16k 上行约 256 kbps，够用），
  要公网/弱网再上 Opus。
- **摄像头多模态**：相机预览/拍照已经可用，下一步可以把照片通过 WebSocket
  发给服务端，让大模型“看图”。
- **OTA**：可接 ESP-IDF 的 `esp_https_ota`。

---

## 9. 本次实测结果（2026-11-11）

| 项目 | 结果 |
|---|---|
| 固件编译（ESP-IDF 5.3.1） | ✅ app 3,637,136 字节（0x377F90），4MB 分区余 13% |
| 真机启动 / Wi-Fi / WebSocket | ✅ GC2145 板，`state -> 1`，无 I2C CONFLICT、无 LVGL task_wdt |
| TF 卡挂载 | ✅ SDMMC 1 线，`/sdcard`，FAT32 |
| 相机初始化 | ✅ 识别 GC2145（0x3C），PSRAM DMA，VGA RGB565，`cam config ok` |
| 预览任务 | ✅ 启动成功；LVGL 无 flush 报错（画面请在设备上肉眼确认） |
| 网页“拍照” → TF 卡 | ✅ `/DCIM/IMG_0001.JPG` 39,012 字节 |
| 连拍编号 | ✅ `/DCIM/IMG_0002.JPG` 39,880 字节 |
| 关闭 → 重新打开相机 | ✅ `camera app stopped/started` 正常，重开后拍照 |
| 重开后拍照 | ✅ `/DCIM/IMG_0003.JPG` 37,682 字节 |
| 连续开关相机 3 轮 | ✅ 3 次均 VGA 初始化，`IMG_0005/0006/0007.JPG` 保存成功 |
| 失败原因上报 | ✅ toast 显示 `相机失败:<原因>`，网页 `/api/device` 带 `camera_err` |
| BOOT 轻按拍照 | ⏳ 和网页“拍照”走同一个 `camera_app_take_photo()`，需你按实体键确认 |
| BOOT 长按 3 秒进语音 | ⏳ 逻辑已改；需实体按键确认（短按不启动 AI） |
| 语音链路（此前实测） | ✅ 设备在线、ASR/回复正常；GPT-SoVITS/搜索/天气见 2026-11-09 记录 |

> 语音侧 2026-11-09 已验证：GPT-SoVITS 橘真琴合成、音色上传/试听/删除、cn.bing.com
> 搜索、wttr.in 天气、模拟设备 WebSocket 全链路。
> 首次编译前请确认 `C:\makoto-pet-build` 可写；脚本会自动建。

## 10. 合规提醒

生成的语音是 AI 合成，请勿用于冒充他人或对外发布误导性内容。
本服务默认监听 `0.0.0.0` 且管理页无鉴权，**只建议在家庭局域网内使用**；
不要直接映射到公网。
