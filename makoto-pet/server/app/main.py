# -*- coding: utf-8 -*-
"""Makoto Brain：设备 WebSocket 语音会话 + 音色管理网页。"""
import asyncio
import base64
import json
import os
import shutil
import socket
import subprocess
import uuid
from typing import Any

from fastapi import FastAPI, File, Form, HTTPException, UploadFile, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse, Response
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from .config import CFG, ROOT, resolve
from . import kb_parser
from .knowledge import KnowledgeBase
from .vision import VisionClient
from .voices import VoiceStore
from .asr import ASR
from .llm import LLM
from .tts import TTSClient, resample_pcm, split_sentences, wav_bytes
from . import tools as tools_mod
from . import music as music_mod
from . import pinyin as pinyin_ime

DATA_DIR = os.path.join(ROOT, "data")
REFS_DIR = os.path.join(DATA_DIR, "refs")
WEB_DIR = os.path.join(ROOT, "web")
os.makedirs(REFS_DIR, exist_ok=True)

voices = VoiceStore(os.path.join(DATA_DIR, "voices.json"), REFS_DIR, CFG)
DEVICES = {}  # device_id -> DeviceSession（当前在线的设备）
kb = KnowledgeBase(os.path.join(DATA_DIR, "knowledge.json"))
vision = VisionClient(CFG)
TTS_STREAM = bool((CFG.get("tts") or {}).get("stream", True))
PLAYBACK_RATE = int((CFG.get("tts") or {}).get("playback_rate", 0) or 0)
KB_JOBS = {}
KB_UPLOAD_DIR = os.path.join(DATA_DIR, "kb_uploads")
os.makedirs(KB_UPLOAD_DIR, exist_ok=True)
KB_ALLOWED_EXT = {".pdf", ".docx", ".md", ".markdown", ".txt",
                  ".png", ".jpg", ".jpeg", ".webp", ".bmp"}

_ZH_SWITCH = ("用中文", "说中文", "中文回答", "讲中文", "切换中文", "用汉语", "中国語")
_JA_SWITCH = ("用日语", "说日语", "日语回答", "讲日语", "切换日语", "用日文", "日本語")


def detect_reply_lang(text: str, current: str = "ja") -> str:
    """默认日语；用户明确要求中文/日语时切换，并保持到下一次切换。"""
    t = (text or "").lower()
    zh = max([t.rfind(k) for k in _ZH_SWITCH] or [-1])
    ja = max([t.rfind(k) for k in _JA_SWITCH] or [-1])
    if zh < 0 and ja < 0:
        return current
    return "zh" if zh > ja else "ja"
tts = TTSClient(CFG, voices)
asr = ASR(CFG)
llm = LLM(CFG)

app = FastAPI(title="Makoto Brain", version="1.0")

if os.path.isdir(WEB_DIR):
    app.mount("/static", StaticFiles(directory=WEB_DIR), name="static")


# =====================================================================
# 设备 WebSocket
# =====================================================================
class DeviceSession:
    def __init__(self, ws: WebSocket):
        self.ws = ws
        self.send_lock = asyncio.Lock()
        self.audio = bytearray()
        self.listening = False
        self.voice_id = voices.default_id
        self.turn_task = None
        self.closed = False
        self.abort_flag = asyncio.Event()
        self.history = []
        self.device_id = None
        self.device_name = "szpi-s3"
        self.status = {}
        self.last_asr = ""
        self.last_reply = ""
        self.reply_lang = "ja"  # 默认日语；用户要求中文后保持中文
        # 音乐
        self.music = music_mod.MusicPlayer(self)
        self.playlist = []
        self.pending_music = None
        self.defer_music = False

    # ---------------- 发送 ----------------
    async def send_json(self, obj: dict):
        if self.closed:
            return
        async with self.send_lock:
            try:
                await self.ws.send_text(json.dumps(obj, ensure_ascii=False))
            except Exception:
                self.closed = True

    async def send_cmd(self, cmd: str, value=None, text: str = None):
        msg = {"type": "cmd", "cmd": cmd}
        if value is not None:
            msg["value"] = value
        if text is not None:
            msg["text"] = text
        await self.send_json(msg)

    async def send_pcm(self, sample_rate: int, pcm: bytes):
        if not pcm:
            return
        chunk = 4096
        for i in range(0, len(pcm), chunk):
            if self.closed or self.abort_flag.is_set():
                return
            part = pcm[i:i + chunk]
            async with self.send_lock:
                try:
                    await self.ws.send_bytes(part)
                except Exception:
                    self.closed = True
                    return
            # 略快于实时，既不让设备缓冲溢出，也留出下一句的合成时间
            await asyncio.sleep(len(part) / (sample_rate * 2.0) * 0.85)

    async def send_music_pcm(self, pcm: bytes):
        if not pcm or self.closed:
            return
        async with self.send_lock:
            try:
                await self.ws.send_bytes(pcm)
            except Exception:
                self.closed = True

    # ---------------- 接收 ----------------
    async def run(self):
        try:
            while True:
                msg = await self.ws.receive()
                if msg.get("type") == "websocket.disconnect":
                    break
                if msg.get("text") is not None:
                    await self.on_text(msg["text"])
                elif msg.get("bytes") is not None:
                    if self.listening:
                        self.audio.extend(msg["bytes"])
                        # 最长 30 秒，防止内存无限增长
                        if len(self.audio) > 16000 * 2 * 30:
                            del self.audio[:len(self.audio) - 16000 * 2 * 30]
        except WebSocketDisconnect:
            pass
        except Exception as e:
            print("[ws] session error:", e)
        finally:
            self.closed = True
            if self.device_id and DEVICES.get(self.device_id) is self:
                del DEVICES[self.device_id]
            await self.cancel_turn(send=False)

    async def on_text(self, raw: str):
        try:
            msg = json.loads(raw)
        except Exception:
            return
        typ = msg.get("type")

        if typ == "hello":
            self.device_name = msg.get("device") or "szpi-s3"
            host = getattr(getattr(self.ws, "client", None), "host", "")
            self.device_id = "%s@%s" % (self.device_name, host or id(self))
            DEVICES[self.device_id] = self
            v = voices.get(self.voice_id)
            await self.send_json({
                "type": "ready",
                "voice": v["id"],
                "voice_name": v["name"],
                "voices": [{"id": x["id"], "name": x["name"]} for x in voices.all()],
            })
            print("[ws] device hello:", self.device_id, msg.get("fw"))

        elif typ == "status":
            self.status = {k: v for k, v in msg.items() if k != "type"}

        elif typ == "ptt_start":
            await self.music.stop()
            await self.cancel_turn(send=False)
            self.audio = bytearray()
            self.listening = True
            self.abort_flag.clear()
            await self.send_json({"type": "state", "state": "listening"})

        elif typ == "ptt_stop":
            self.listening = False
            pcm = bytes(self.audio)
            self.audio = bytearray()
            if len(pcm) < 16000 * 0.3 * 2:
                await self.send_json({"type": "state", "state": "idle"})
                return
            await self.send_json({"type": "state", "state": "thinking"})
            self.turn_task = asyncio.create_task(self.handle_turn(pcm))

        elif typ == "abort":
            await self.music.stop()
            await self.cancel_turn()

        elif typ == "music_cmd":
            action = str(msg.get("action") or "")
            p = self.music
            if action == "pause":
                await p.pause()
            elif action == "resume":
                # 暂停中 = 继续；没歌/已停止 = 从本地曲库第一首开始
                if p.state == "paused":
                    await p.resume()
                elif p.cur is None or p.state == "stopped":
                    await self.play_local_first()
            elif action == "toggle":
                if p.state == "paused":
                    await p.resume()
                elif p.state == "playing":
                    await p.pause()
                else:
                    await self.play_local_first()
            elif action in ("stop", "close"):
                await p.stop()
            elif action == "next":
                await self.local_step(1)
            elif action == "prev":
                await self.local_step(-1)

        elif typ == "set_voice":
            v = voices.find(msg.get("voice_id") or msg.get("name") or "")
            if v:
                self.voice_id = v["id"]
                await self.send_json({"type": "voice", "name": v["name"]})

        elif typ == "pinyin":
            # 设备记事本的拼音输入：返回候选中文，不经过大模型
            text = str(msg.get("text") or "")
            rid = msg.get("id")
            offset = int(msg.get("offset") or 0)
            try:
                cands = await asyncio.to_thread(
                    pinyin_ime.candidates, text, 24, 80, offset)
            except Exception as e:
                print("[pinyin] convert failed:", e)
                cands = []
            await self.send_json({
                "type": "pinyin_result",
                "id": rid,
                "offset": offset,
                "candidates": cands,
            })

        elif typ == "ping":
            await self.send_json({"type": "pong"})

    async def cancel_turn(self, send: bool = True):
        self.abort_flag.set()
        self.pending_music = None
        self.defer_music = False
        task = self.turn_task
        self.turn_task = None
        if task and not task.done():
            task.cancel()
            try:
                await task
            except (asyncio.CancelledError, Exception):
                pass
        if send:
            await self.send_json({"type": "tts_stop"})
            await self.send_json({"type": "state", "state": "idle"})

    # ---------------- 一轮对话 ----------------
    async def on_tool(self, name: str, args: dict):
        if name == "knowledge_search":
            return kb.search_text(args.get("query", "")) or "知识库里没有找到相关内容。"
        if name == "device_set_volume":
            v = int(args.get("volume", 70))
            await self.send_cmd("volume", value=v)
            return "已把设备音量设为 %d" % v
        if name == "device_set_brightness":
            v = int(args.get("brightness", 70))
            await self.send_cmd("brightness", value=v)
            return "已把设备亮度设为 %d" % v
        if name == "device_open_app":
            app = args.get("app", "desktop")
            await self.send_cmd("open_app", value=app)
            return "已在设备上打开 %s" % app
        if name == "device_write_note":
            text = args.get("text", "")
            await self.send_cmd("note_append", text=text)
            return "已写进设备记事本"
        if name == "switch_voice":
            v = voices.find(args.get("voice_id") or args.get("name") or "")
            if not v:
                return "没有找到这个音色"
            self.voice_id = v["id"]
            await self.send_json({"type": "voice", "name": v["name"]})
            return "已切换到音色：%s" % v["name"]

        if name == "music_search":
            query = str(args.get("query") or "")
            source = str(args.get("source") or "auto")
            self.playlist = await music_mod.search_music(query, source)
            return music_mod.format_results(self.playlist, query)

        if name == "music_play":
            track, idx, err = await self._music_prepare(
                args.get("query"), args.get("index"), args.get("source") or "auto")
            if err:
                return err
            if self.defer_music:
                self.pending_music = (track, idx)
                return "马上播放《%s》— %s。" % (
                    track.get("title", ""), track.get("artist", ""))
            await self.music.play(track, playlist=self.playlist, index=idx)
            return "开始播放《%s》— %s。" % (
                track.get("title", ""), track.get("artist", ""))

        if name == "music_control":
            action = str(args.get("action") or "")
            if action == "pause":
                await self.music.pause()
            elif action == "resume":
                await self.music.resume()
            elif action == "stop":
                await self.music.stop()
            elif action == "next":
                return await self.music.next()
            elif action == "prev":
                return await self.music.prev()
            else:
                return "不支持的音乐操作：%s" % action
            return self.music.status_text()

        if name == "music_status":
            return self.music.status_text()

        await self.send_json({"type": "tool", "name": name})
        return await tools_mod.execute(name, args)

    async def play_local_first(self):
        items = music_mod.local_tracks()
        if not items:
            await self.send_json({"type": "music_error",
                                  "message": "本地曲库为空，请把音乐放进 server/music"})
            return
        self.playlist = items
        await self.music.play(items[0], playlist=items, index=0,
                              auto_next=music_mod.auto_next_enabled())

    async def local_step(self, delta: int):
        items = music_mod.local_tracks()
        if not items:
            await self.send_json({"type": "music_error",
                                  "message": "本地曲库为空，请把音乐放进 server/music"})
            return
        self.playlist = items
        cur = self.music.cur
        idx = 0
        if cur:
            for i, t in enumerate(items):
                if t.get("id") == cur.get("id"):
                    idx = i
                    break
        idx = max(0, min(len(items) - 1, idx + delta))
        await self.music.play(items[idx], playlist=items, index=idx,
                              auto_next=music_mod.auto_next_enabled())

    async def _music_prepare(self, query=None, index=None, source="auto"):
        query = (query or "").strip()
        if query:
            if index is not None:
                results = await music_mod.search_music(query, source)
                self.playlist = results
                try:
                    i = int(index) - 1
                except (TypeError, ValueError):
                    return None, None, "序号无效。"
                if not (0 <= i < len(results)):
                    return None, None, "这个关键词只找到 %d 首。" % len(results)
                return results[i], i, None
            low = query.lower()
            for i, t in enumerate(self.playlist):
                if low in t.get("title", "").lower() or low in t.get("artist", "").lower():
                    return t, i, None
            results = await music_mod.search_music(query, source)
            self.playlist = results
            if not results:
                return None, None, music_mod.format_results([], query)
            return results[0], 0, None
        if index is not None:
            try:
                i = int(index) - 1
            except (TypeError, ValueError):
                return None, None, "序号无效。请用 music_search 先搜歌。"
            if not (0 <= i < len(self.playlist)):
                return None, None, "没有第 %d 首。请先调用 music_search。" % (i + 1)
            return self.playlist[i], i, None
        if self.playlist and 0 <= self.music.idx < len(self.playlist):
            return self.playlist[self.music.idx], self.music.idx, None
        return None, None, "还没有选歌。请先告诉我歌名或风格。"

    async def start_pending_music(self):
        item, self.pending_music = self.pending_music, None
        if not item:
            return
        track, idx = item
        try:
            await self.music.play(track, playlist=self.playlist, index=idx)
        except Exception as e:
            print("[music] start failed:", e)

    async def handle_turn(self, pcm: bytes):
        try:
            try:
                text = await asyncio.wait_for(asr.transcribe(pcm), timeout=120)
            except asyncio.TimeoutError:
                await self.send_json({"type": "error",
                                      "message": "语音识别超时，请再说一次"})
                await self.send_json({"type": "state", "state": "idle"})
                return
            if self.abort_flag.is_set():
                return
            if not text:
                await self.send_json({"type": "state", "state": "idle"})
                return

            print("[asr]", text)
            self.last_asr = text
            self.reply_lang = detect_reply_lang(text, self.reply_lang)
            await self.send_json({"type": "asr", "text": text})

            kb_text = kb.search_text(text)
            self.defer_music = True
            self.pending_music = None
            reply = await llm.chat(text, on_tool=self.on_tool, history=self.history,
                                   extra_context=kb_text, lang=self.reply_lang)
            if self.abort_flag.is_set():
                return
            print("[llm]", reply)

            self.history.append({"role": "user", "content": text})
            self.history.append({"role": "assistant", "content": reply})
            self.history = self.history[-8:]

            self.last_reply = reply
            await self.send_json({"type": "reply", "text": reply})
            await self.speak(reply)
            if not self.abort_flag.is_set() and self.pending_music:
                await self.start_pending_music()
            self.defer_music = False
            if not self.abort_flag.is_set():
                await self.send_json({"type": "state", "state": "idle"})

        except asyncio.CancelledError:
            raise
        except Exception as e:
            print("[turn] error:", e)
            await self.send_json({"type": "tts_stop"})
            await self.send_json({"type": "error", "message": "处理失败: %s" % e})
            await self.send_json({"type": "state", "state": "idle"})

    @staticmethod
    def _shorten_first(sentences, limit=24):
        """把第一句再切短一点，让第一段语音更快出来。"""
        if not sentences:
            return sentences
        s = sentences[0]
        if len(s) <= limit:
            return sentences
        cut = -1
        for i, ch in enumerate(s[:limit]):
            if ch in "，,、：:；;":
                cut = i + 1
        if cut <= 0:
            cut = limit
        return [s[:cut], s[cut:]] + sentences[1:]

    async def speak(self, reply: str):
        # 语音优先：先把音乐停掉，避免叠音（音乐模式正常不会触发）
        await self.music.stop()
        voice = voices.get(self.voice_id)
        sentences = self._shorten_first(split_sentences(reply))
        if not sentences:
            await self.send_json({"type": "tts_end"})
            return

        queue: asyncio.Queue = asyncio.Queue(maxsize=32)
        started = False

        async def producer():
            nonlocal started
            try:
                for s in sentences:
                    if self.abort_flag.is_set():
                        break
                    try:
                        if TTS_STREAM:
                            # 流式：GPT-SoVITS 出来一片就发一片，首包约 0.3~0.5 秒
                            async for sr, pcm in tts.synth_stream(s, voice):
                                if self.abort_flag.is_set():
                                    break
                                if not started:
                                    started = True
                                    await self.send_json({"type": "tts_start",
                                                          "sample_rate": sr})
                                await queue.put((sr, pcm))
                        else:
                            sr, pcm = await tts.synth(s, voice)
                            sr, pcm = resample_pcm(sr, pcm, PLAYBACK_RATE)
                            if not started:
                                started = True
                                await self.send_json({"type": "tts_start",
                                                      "sample_rate": sr})
                            await queue.put((sr, pcm))
                    except Exception as e:
                        print("[tts] stream failed, fallback to full synth:", e)
                        sr, pcm = await tts.synth(s, voice)
                        sr, pcm = resample_pcm(sr, pcm, PLAYBACK_RATE)
                        if not started:
                            started = True
                            await self.send_json({"type": "tts_start",
                                                  "sample_rate": sr})
                        await queue.put((sr, pcm))
            except Exception as e:
                await queue.put(("error", e))
            finally:
                await queue.put(None)

        prod = asyncio.create_task(producer())
        try:
            while True:
                item = await queue.get()
                if item is None:
                    break
                if item[0] == "error":
                    raise item[1]
                if self.abort_flag.is_set():
                    break
                await self.send_pcm(item[0], item[1])
        finally:
            if not prod.done():
                prod.cancel()
            try:
                await prod
            except Exception:
                pass

        if not self.abort_flag.is_set():
            await self.send_json({"type": "tts_end"})


@app.websocket("/ws/device")
async def ws_device(ws: WebSocket):
    await ws.accept()
    session = DeviceSession(ws)
    print("[ws] device connected")
    await session.run()
    print("[ws] device disconnected")


# =====================================================================
# 管理接口
# =====================================================================
class PreviewReq(BaseModel):
    text: str = ""
    voice_id: str | None = None


class ChatReq(BaseModel):
    text: str
    voice_id: str | None = None


class DeviceCmd(BaseModel):
    device_id: str | None = None
    cmd: str
    value: Any = None
    text: str | None = None


class KbReq(BaseModel):
    title: str
    content: str
    tags: str | None = ""


class SayReq(BaseModel):
    text: str
    device_id: str | None = None


class PinyinReq(BaseModel):
    text: str
    limit: int = 6


class MusicPlayReq(BaseModel):
    device_id: str | None = None
    query: str | None = None
    source: str = "auto"
    index: int | None = None
    track: dict | None = None


class MusicCtrlReq(BaseModel):
    device_id: str | None = None
    action: str


@app.get("/api/kb")
async def api_kb_list():
    return {"items": kb.public_list()}


@app.post("/api/kb")
async def api_kb_add(req: KbReq):
    try:
        item = kb.add(req.title, req.content, req.tags or "")
    except ValueError as e:
        raise HTTPException(400, str(e))
    return {"ok": True, "item": item}


@app.delete("/api/kb/{item_id}")
async def api_kb_delete(item_id: str):
    if not kb.delete(item_id):
        raise HTTPException(404, "knowledge item not found")
    return {"ok": True}


@app.post("/api/kb/upload")
async def api_kb_upload(file: UploadFile = File(...)):
    filename = file.filename or "document"
    ext = os.path.splitext(filename)[1].lower()
    if ext not in KB_ALLOWED_EXT:
        raise HTTPException(400, "支持 PDF / Word(.docx) / Markdown / TXT / 图片(PNG/JPG)")

    job_id = uuid.uuid4().hex
    tmp_path = os.path.join(KB_UPLOAD_DIR, job_id + ext)
    size = 0
    with open(tmp_path, "wb") as f:
        while True:
            chunk = await file.read(1024 * 1024)
            if not chunk:
                break
            size += len(chunk)
            if size > 30 * 1024 * 1024:
                f.close()
                try:
                    os.remove(tmp_path)
                except OSError:
                    pass
                raise HTTPException(400, "文件超过 30MB")
            f.write(chunk)

    job = {
        "id": job_id,
        "filename": filename,
        "status": "parsing",
        "progress": 0,
        "message": "准备解析",
        "added": 0,
    }
    KB_JOBS[job_id] = job
    asyncio.create_task(_parse_kb_job(job, tmp_path, ext))
    return {"ok": True, "job_id": job_id}


async def _parse_kb_job(job: dict, path: str, ext: str):
    try:
        def on_progress(pct, message=""):
            job["progress"] = max(0, min(100, int(pct)))
            job["message"] = message

        text = await asyncio.to_thread(kb_parser.parse_file, path, ext, on_progress, vision)
        chunks = kb_parser.chunk_text(text)
        if not chunks:
            raise RuntimeError("没有解析出可用文字")
        base = os.path.splitext(os.path.basename(job["filename"]))[0]
        for i, chunk in enumerate(chunks):
            kb.add("%s（%d/%d）" % (base, i + 1, len(chunks)), chunk, "文档")
            job["added"] = i + 1
            job["progress"] = 85 + int(15 * (i + 1) / len(chunks))
            job["message"] = "写入知识库 %d/%d" % (i + 1, len(chunks))
        job["status"] = "done"
        job["progress"] = 100
        job["message"] = "解析完成，共 %d 段" % len(chunks)
    except Exception as e:
        job["status"] = "error"
        job["message"] = str(e)
        job["progress"] = 100
    finally:
        try:
            os.remove(path)
        except OSError:
            pass


@app.get("/api/kb/jobs/{job_id}")
async def api_kb_job(job_id: str):
    job = KB_JOBS.get(job_id)
    if not job:
        raise HTTPException(404, "job not found")
    return job


def _lan_ip() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


@app.get("/api/health")
async def api_health():
    return {
        "ok": True,
        "llm_configured": llm.configured,
        "tts_url": tts.base,
        "tts_online": await tts.health(),
        "asr_provider": asr.provider,
        "asr_ready": asr.ready,
        "default_voice": voices.default_id,
        "ws_url": "ws://%s:%d/ws/device" % (_lan_ip(), CFG.get("server", {}).get("port", 8765)),
        "search_provider": (CFG.get("search") or {}).get("provider", "ddgs"),
    }


def first_device(device_id=None):
    if device_id:
        return DEVICES.get(device_id)
    return next(iter(DEVICES.values()), None)


@app.get("/api/device")
async def api_device():
    out = []
    for did, s in DEVICES.items():
        out.append({
            "id": did,
            "name": s.device_name,
            "voice": voices.get(s.voice_id).get("name"),
            "status": s.status,
            "asr": s.last_asr,
            "reply": s.last_reply,
        })
    return {"devices": out, "count": len(out)}


@app.post("/api/device/command")
async def api_device_command(req: DeviceCmd):
    s = first_device(req.device_id)
    if s is None:
        raise HTTPException(404, "设备不在线")
    await s.send_cmd(req.cmd, value=req.value, text=req.text)
    return {"ok": True}


@app.post("/api/device/say")
async def api_device_say(req: SayReq):
    s = first_device(req.device_id)
    if s is None:
        raise HTTPException(404, "设备不在线")
    text = (req.text or "").strip()
    if not text:
        raise HTTPException(400, "text is empty")
    asyncio.create_task(s.speak(text))
    return {"ok": True}


@app.get("/api/music/search")
async def api_music_search(q: str = "", source: str = "auto", limit: int = 8):
    results = await music_mod.search_music(q, source, limit)
    return {"query": q, "source": source, "results": results}


@app.post("/api/music/play")
async def api_music_play(req: MusicPlayReq):
    s = first_device(req.device_id)
    if s is None:
        raise HTTPException(404, "设备不在线")
    if req.track:
        track = req.track
        idx = -1
        if req.query:
            s.playlist = await music_mod.search_music(req.query, req.source)
        for i, t in enumerate(s.playlist):
            if t.get("id") == track.get("id"):
                idx = i
                break
    else:
        track, idx, err = await s._music_prepare(req.query, req.index, req.source)
        if err:
            raise HTTPException(400, err)
    await s.music.play(track, playlist=s.playlist, index=idx)
    return {"ok": True, "playing": {
        "title": track.get("title", ""), "artist": track.get("artist", ""),
        "source": track.get("source", ""), "index": idx + 1}}


@app.post("/api/music/control")
async def api_music_control(req: MusicCtrlReq):
    s = first_device(req.device_id)
    if s is None:
        raise HTTPException(404, "设备不在线")
    p = s.music
    if req.action == "pause":
        await p.pause()
    elif req.action == "resume":
        await p.resume()
    elif req.action == "toggle":
        await p.toggle()
    elif req.action == "stop":
        await p.stop()
    elif req.action == "next":
        await p.next()
    elif req.action == "prev":
        await p.prev()
    else:
        raise HTTPException(400, "不支持的操作")
    return {"ok": True, "status": p.status_text()}


@app.get("/api/music/status")
async def api_music_status(device_id: str | None = None):
    s = first_device(device_id)
    if s is None:
        raise HTTPException(404, "设备不在线")
    p = s.music
    return {"ok": True, "status": p.status_text(), "state": p.state,
            "position": p.position, "duration": p.duration, "track": p.cur}


@app.get("/api/voices")
async def api_voices():
    return {"default": voices.default_id, "voices": voices.public_list()}


@app.post("/api/voices/{voice_id}/default")
async def api_set_default(voice_id: str):
    if not voices.set_default(voice_id):
        raise HTTPException(404, "voice not found")
    return {"ok": True, "default": voice_id}


@app.delete("/api/voices/{voice_id}")
async def api_delete_voice(voice_id: str):
    v = voices.find(voice_id)
    if not v:
        raise HTTPException(404, "voice not found")
    voices.delete(v["id"])
    # 只删 refs 目录里的参考音频，内置音色指向别处，不动
    ref = v.get("ref_audio", "")
    if ref and os.path.abspath(ref).startswith(os.path.abspath(REFS_DIR)) and os.path.exists(ref):
        try:
            os.remove(ref)
        except OSError:
            pass
    return {"ok": True}


def _normalize_audio(src: str, dst: str):
    audio_cfg = CFG.get("audio") or {}
    max_s = float(audio_cfg.get("ref_max_seconds", 10))
    ff = resolve(audio_cfg.get("ffmpeg", ""))
    if not (ff and os.path.exists(ff)):
        ff = shutil.which("ffmpeg")
    if ff:
        cmd = [ff, "-y", "-i", src, "-ac", "1", "-ar", "32000",
               "-sample_fmt", "s16", "-t", str(max_s), dst]
        proc = subprocess.run(cmd, capture_output=True)
        if proc.returncode == 0 and os.path.exists(dst):
            return
        raise HTTPException(400, "ffmpeg 转码失败: %s" % proc.stderr.decode("utf-8", "ignore")[-300:])

    # 没有 ffmpeg 时用 soundfile + 线性重采样
    try:
        import numpy as np
        import soundfile as sf
        data, sr = sf.read(src, dtype="float32", always_2d=True)
        mono = data.mean(axis=1)
        if sr != 32000 and len(mono):
            n = int(len(mono) * 32000 / sr)
            idx = np.linspace(0, len(mono) - 1, n)
            mono = np.interp(idx, np.arange(len(mono)), mono).astype(np.float32)
        mono = mono[: int(max_s * 32000)]
        sf.write(dst, mono, 32000, subtype="PCM_16")
    except Exception as e:
        raise HTTPException(400, "音频处理失败（请安装 ffmpeg 或 soundfile）: %s" % e)


@app.post("/api/voices")
async def api_add_voice(
    name: str = Form(...),
    prompt_text: str = Form(...),
    prompt_lang: str = Form("ja"),
    text_lang: str = Form("auto"),
    speed: float = Form(1.0),
    gpt_weights: str = Form(""),
    sovits_weights: str = Form(""),
    audio: UploadFile = File(...),
):
    name = name.strip()
    prompt_text = prompt_text.strip()
    if not name or not prompt_text:
        raise HTTPException(400, "音色名和参考文本都必须填写")

    uid = uuid.uuid4().hex[:8]
    raw_path = os.path.join(REFS_DIR, "_upload_%s.tmp" % uid)
    with open(raw_path, "wb") as f:
        f.write(await audio.read())

    dst_path = os.path.join(REFS_DIR, "%s.wav" % uid)
    try:
        _normalize_audio(raw_path, dst_path)
    finally:
        if os.path.exists(raw_path):
            os.remove(raw_path)

    item = voices.add(
        name=name,
        ref_audio=dst_path,
        prompt_text=prompt_text,
        prompt_lang=prompt_lang,
        text_lang=text_lang,
        gpt_weights=gpt_weights.strip(),
        sovits_weights=sovits_weights.strip(),
        speed=speed,
    )
    return {"ok": True, "voice": item}


@app.post("/api/preview")
async def api_preview(req: PreviewReq):
    text = req.text.strip() or "你好，我是橘真琴。今天也一起加油吧。"
    v = voices.get(req.voice_id)
    try:
        sr, pcm = await tts.synth(text, v)
    except Exception as e:
        raise HTTPException(500, str(e))
    return Response(content=wav_bytes(sr, pcm), media_type="audio/wav")


@app.post("/api/pinyin")
async def api_pinyin(req: PinyinReq):
    """拼音 → 中文候选（记事本输入法 / 网页测试用）。"""
    cands = await asyncio.to_thread(
        pinyin_ime.candidates, req.text, max(1, min(req.limit, 12)), 60)
    return {"ok": True, "candidates": cands}


@app.post("/api/chat")
async def api_chat(req: ChatReq):
    text = req.text.strip()
    if not text:
        raise HTTPException(400, "text is empty")
    v = voices.get(req.voice_id)

    async def on_tool(name, args):
        if name == "knowledge_search":
            return kb.search_text(args.get("query", "")) or "知识库里没有找到相关内容。"
        if name.startswith("device_") or name.startswith("music_"):
            s = first_device()
            if s is None:
                return "没有设备在线"
            return await s.on_tool(name, args)
        return await tools_mod.execute(name, args)

    try:
        reply = await llm.chat(text, on_tool=on_tool,
                               extra_context=kb.search_text(text),
                               lang=detect_reply_lang(text, "ja"))
    except Exception as e:
        raise HTTPException(500, "大模型调用失败: %s" % e)

    pcm = b""
    sr = 32000
    for s in split_sentences(reply):
        sr, part = await tts.synth(s, v)
        pcm += part

    return {
        "reply": reply,
        "audio_b64": base64.b64encode(wav_bytes(sr, pcm)).decode("ascii"),
        "sample_rate": sr,
    }


@app.get("/")
async def index():
    path = os.path.join(WEB_DIR, "index.html")
    if not os.path.exists(path):
        return JSONResponse({"message": "web/index.html 不存在"}, status_code=404)
    return FileResponse(path)


async def _warmup_tts():
    """启动后预热：加载真琴权重 + 跑一次推理，避免第一句话等模型加载。"""
    await asyncio.sleep(3)
    try:
        v = voices.get()
        await tts.synth("おはようございます。", v)
        print("[tts] warmup done:", v.get("name", "?"))
    except Exception as e:
        print("[tts] warmup skipped:", e)


async def _warmup_asr():
    """启动后加载语音识别模型（CPU 首次约 10~15 秒）。"""
    await asyncio.sleep(2)
    try:
        await asr.warmup()
        print("[asr] model ready:", asr.cfg.get("model", "?"))
    except Exception as e:
        print("[asr] model load failed:", e)


async def _warmup_pinyin():
    """构建/加载记事本拼音词库（首次约 3~6 秒，之后走缓存）。"""
    try:
        await asyncio.to_thread(pinyin_ime.warmup)
    except Exception as e:
        print("[pinyin] warmup failed:", e)


@app.on_event("startup")
async def on_startup():
    print("=" * 60)
    print("  Makoto Brain 已启动")
    print("  管理页面 : http://127.0.0.1:%d" % CFG.get("server", {}).get("port", 8765))
    print("  设备地址 : ws://%s:%d/ws/device" %
          (_lan_ip(), CFG.get("server", {}).get("port", 8765)))
    print("  默认音色 : %s" % voices.get().get("name", "?"))
    print("  大模型   : %s (%s)" % (llm.model, "已配置" if llm.configured else "缺 api_key"))
    print("  TTS      : %s" % tts.base)
    print("=" * 60)
    asyncio.create_task(_warmup_tts())
    asyncio.create_task(_warmup_asr())
    asyncio.create_task(_warmup_pinyin())
