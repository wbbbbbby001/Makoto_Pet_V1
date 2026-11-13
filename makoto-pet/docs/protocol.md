# 设备 ↔ 语音中枢 WebSocket 协议 v1

- 地址：`ws://<电脑IP>:8765/ws/device`
- 控制消息：JSON 文本帧
- 音频：二进制帧，裸 **PCM16 小端**
  - 上行（设备→服务端）：**16 kHz / 单声道**
  - 下行（服务端→设备）：采样率由 `tts_start.sample_rate` 指定（GPT-SoVITS 通常是 32000）
- 半双工：说话时麦克风开、喇叭关；播放时反过来。

---

## 时序

```
设备                                   服务端
 │  ── text hello ───────────────────▶ │
 │  ◀─ text ready {voice,voice_name} ── │
 │                                      │
 │  [按住按钮]                           │
 │  ── text ptt_start ───────────────▶ │
 │  ◀─ text state listening ─────────── │
 │  ── binary PCM16 20ms/帧 ─────────▶ │  (持续)
 │  [松开]                               │
 │  ── text ptt_stop ────────────────▶ │
 │  ◀─ text state thinking ──────────── │
 │  ◀─ text asr {text} ──────────────── │
 │  ◀─ text tool {name:web_search} ──── │  (可选，屏幕显示“正在联网搜索…”)
 │  ◀─ text reply {text} ────────────── │
 │  ◀─ text tts_start {sample_rate} ─── │
 │  ◀─ binary PCM16 (4096B/帧) ──────── │  (按 ~0.85 倍实时节奏)
 │  ◀─ text tts_end ─────────────────── │
 │  ◀─ text state idle ──────────────── │
```

打断：设备发 `{"type":"abort"}`，服务端取消本轮并回 `tts_stop` + `state idle`。

---

## 设备 → 服务端

| type | 字段 | 说明 |
|---|---|---|
| `hello` | `device`, `fw`, `cap` | 连接后必发 |
| `ptt_start` | — | 开始录音；服务端清空缓冲 |
| `ptt_stop` | — | 结束录音；服务端开始识别 |
| `abort` | — | 打断当前 ASR/LLM/TTS |
| `set_voice` | `voice_id` 或 `name` | 直接切换音色 |
| `status` | `volume`, `brightness`, `wifi`, `state` | 状态上报（连上/切换后） |
| `pinyin` | `id`, `text`, `offset` | 记事本拼音输入；`offset` 为候选起始序号（每页 24 条） |
| `music_cmd` | `action`: `pause` / `resume` / `toggle` / `stop` / `next` / `prev` | 音乐模式控制 |
| `ping` | — | 心跳，服务端回 `pong` |

二进制帧：仅在 `ptt_start` 与 `ptt_stop` 之间发送，20 ms 一帧（320 samples = 640 bytes）。

---

## 服务端 → 设备

| type | 字段 | 说明 |
|---|---|---|
| `ready` | `voice`, `voice_name`, `voices[]` | 握手成功，带音色列表 |
| `state` | `state`: `listening` / `thinking` / `idle` | 状态机 |
| `asr` | `text` | 识别结果 |
| `tool` | `name` | 正在调用工具（`web_search` 等） |
| `reply` | `text` | 大模型最终回复（先显示，再播语音） |
| `tts_start` | `sample_rate` | 后面跟二进制 PCM16 |
| `tts_end` | — | 本轮语音结束 |
| `tts_stop` | — | 立即停止播放（打断时） |
| `music_start` | `title`, `artist`, `source`, `license`, `duration`, `position`, `sample_rate`, `index`, `total` | 开始音乐；音频走二进制 PCM16，采样率 16000 |
| `music_state` | `state`: `playing` / `paused` / `stopped` | 音乐状态 |
| `music_progress` | `position`, `duration` | 播放进度（约 2 秒一次） |
| `music_end` | — | 自然播放结束 |
| `music_stop` | — | 立即停止并清空缓冲 |
| `music_error` | `message` | 播放失败 |
| `voice` | `name` | 音色已切换 |
| `error` | `message` | 出错提示 |
| `pong` | — | 心跳回应 |
| `pinyin_result` | `id`, `candidates[]`, `offset` | 记事本拼音候选（每页 24 条，设备用 `<` `>` 翻页） |
| `cmd` | `cmd`, `value`, `text` | 设备控制，见下表 |

### 设备控制 `cmd`

| cmd | 字段 | 说明 |
|---|---|---|
| `open_app` | `value`: `desktop` / `games` / `2048` / `snake` / `note` / `settings` / `music` / `camera` | 远程打开应用 |
| `photo` | — | 相机界面拍一张，存 TF 卡 `/DCIM/IMG_xxxx.JPG` |
| `volume` | `value` 0–100 | 设置音量 |
| `brightness` | `value` 5–100 | 设置亮度 |
| `text` | `text` | 屏幕提示 + 桌面 toast |
| `note_append` / `note_clear` | `text` | 记事本写入 / 清空 |
| `reboot` | — | 重启设备 |

二进制帧：PCM16 单声道，每帧 ≤ 4096 字节，按 0.85 倍实时节奏推送，
设备端 512 KB 环形缓冲约可缓存 8 秒。

---

## 状态机（设备）

```
BOOT ─▶ IDLE ──按住满3秒──▶ LISTENING ──松开──▶ THINKING ──tts_start──▶ SPEAKING
          ▲                │                  │                       │
          └──── tts_end ───┴──── 出错/abort ──┴──── 再按=打断 ─────────┘
```
