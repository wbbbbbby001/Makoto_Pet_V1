# -*- coding: utf-8 -*-
"""音乐播放器 v1（流畅优先）。

- 本地优先：扫描 server/music（跳过 cache 子目录）
- 在线只接 ccMixter（CC 授权）；播放前先完整下载到 music/cache，再按本地文件播放
- 解码后统一 16k 单声道 PCM，经现有 WebSocket 音频通道推送
- 设备端音乐模式：播放期间不接收语音
- 2.5 秒预缓冲，WebSocket 分片由固件重组
"""
import asyncio
import hashlib
import os
import re
import shutil
import time

import httpx

from .config import CFG, ROOT, resolve

_UA = "MakotoPet/1.0 (local desktop pet)"
AUDIO_EXTS = {".mp3", ".flac", ".wav", ".m4a", ".aac", ".ogg", ".opus",
              ".wma", ".ape", ".aiff", ".aif"}

_local_cache = {"t": 0.0, "items": []}


def _cfg() -> dict:
    return CFG.get("music") or {}


def _ffmpeg() -> str:
    p = _cfg().get("ffmpeg") or (CFG.get("audio") or {}).get("ffmpeg", "")
    p = resolve(p) if p else ""
    if p and os.path.exists(p):
        return p
    return shutil.which("ffmpeg") or ""


def _ffprobe() -> str:
    ff = _ffmpeg()
    if ff:
        cand = os.path.join(os.path.dirname(ff),
                            "ffprobe.exe" if os.name == "nt" else "ffprobe")
        if os.path.exists(cand):
            return cand
    return shutil.which("ffprobe") or ""


def _cache_dir() -> str:
    p = resolve(_cfg().get("cache_dir") or "music/cache")
    os.makedirs(p, exist_ok=True)
    return p


def fmt_seconds(sec: float) -> str:
    sec = max(0, int(round(float(sec or 0))))
    return "%d:%02d" % (sec // 60, sec % 60)


# ---------------------------------------------------------------------------
# 本地曲库
# ---------------------------------------------------------------------------
def _local_items():
    now = time.time()
    if now - _local_cache["t"] < 30:
        return _local_cache["items"]
    dirs = _cfg().get("local_dirs") or ["music", "data/music"]
    items = []
    for d in dirs:
        base = resolve(d)
        if not os.path.isdir(base):
            continue
        for root, subdirs, files in os.walk(base):
            # 不把缓存的在线歌曲当成“本地曲库”
            subdirs[:] = [x for x in subdirs if x.lower() != "cache"]
            for fn in files:
                ext = os.path.splitext(fn)[1].lower()
                if ext not in AUDIO_EXTS:
                    continue
                full = os.path.join(root, fn)
                stem = os.path.splitext(fn)[0]
                if " - " in stem:
                    artist, title = stem.split(" - ", 1)
                else:
                    artist, title = "", stem
                items.append({
                    "id": "local:" + full,
                    "title": title.strip() or fn,
                    "artist": artist.strip() or "本地音乐",
                    "source": "local",
                    "uri": full,
                    "duration": 0.0,
                    "license": "本地文件",
                    "live": False,
                })
                if len(items) >= 3000:
                    break
            if len(items) >= 3000:
                break
    _local_cache["t"] = now
    _local_cache["items"] = items
    return items


def _match(items, query, limit):
    q = (query or "").strip().lower()
    if not q:
        return items[:limit]
    toks = [t for t in re.split(r"\s+", q) if t]
    scored = []
    for it in items:
        hay = ("%s %s" % (it["title"], it["artist"])).lower()
        if not all(t in hay for t in toks):
            continue
        score = 0
        if it["title"].lower() == q:
            score += 100
        if q in it["title"].lower():
            score += 30
        if q in it["artist"].lower():
            score += 10
        scored.append((score, it))
    scored.sort(key=lambda x: -x[0])
    return [it for _s, it in scored[:limit]]


def _duration_of(uri: str) -> float:
    fp = _ffprobe()
    if not fp:
        return 0.0
    try:
        import subprocess
        out = subprocess.run(
            [fp, "-v", "error", "-show_entries", "format=duration",
             "-of", "default=nw=1:nk=1", uri],
            capture_output=True, timeout=12,
            creationflags=(0x08000000 if os.name == "nt" else 0),
        ).stdout.decode("utf-8", "ignore").strip()
        return float(out)
    except Exception:
        return 0.0


# ---------------------------------------------------------------------------
# 在线源：ccMixter（CC 授权）
# ---------------------------------------------------------------------------
def _parse_clock(s: str) -> float:
    s = (s or "").strip()
    m = re.match(r"^(\d+):(\d+)(?::(\d+))?$", s)
    if not m:
        return 0.0
    if m.group(3) is not None:
        return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
    return int(m.group(1)) * 60 + int(m.group(2))


async def _search_ccmixter(q: str, limit: int):
    async with httpx.AsyncClient(timeout=15, follow_redirects=True,
                                 headers={"User-Agent": _UA}) as c:
        r = await c.get("https://ccmixter.org/api/query",
                        params={"f": "json", "search": q, "limit": limit})
        r.raise_for_status()
        data = r.json()
    out = []
    for it in data or []:
        if not isinstance(it, dict):
            continue
        files = []
        for f in it.get("files") or []:
            info = f.get("file_format_info") or {}
            if info.get("media-type") != "audio" or not f.get("download_url"):
                continue
            files.append(f)
        if not files:
            continue
        mp3 = [f for f in files
               if (f.get("file_format_info") or {}).get("format-name", "").endswith("mp3")
               or f.get("file_name", "").lower().endswith(".mp3")]
        f = (mp3 or files)[0]
        info = f.get("file_format_info") or {}
        out.append({
            "id": "ccmixter:%s" % it.get("upload_id"),
            "title": (it.get("upload_name") or "未命名").strip(),
            "artist": (it.get("user_real_name") or it.get("user_name") or "ccMixter").strip(),
            "source": "ccmixter",
            "uri": f["download_url"],
            "duration": _parse_clock(info.get("ps", "")),
            "license": (it.get("license_name") or "CC") + (
                " " + it["license_url"] if it.get("license_url") else ""),
            "live": False,
        })
    return out


async def search_music(query: str, source: str = "auto", limit: int = None):
    q = (query or "").strip()
    limit = int(limit or _cfg().get("max_results", 8) or 8)
    src = (source or "auto").strip().lower()

    if q.startswith(("http://", "https://")):
        return [{
            "id": "url:" + q, "title": os.path.basename(q.split("?")[0]) or q,
            "artist": "网络链接", "source": "url", "uri": q,
            "duration": 0.0, "license": "用户提供的链接", "live": False,
        }]

    results = []
    if src in ("auto", "all", "local"):
        results.extend(_match(_local_items(), q, limit))
    if len(results) >= limit:
        return results[:limit]

    if src in ("auto", "all", "ccmixter") and q:
        try:
            results.extend(await _search_ccmixter(q, limit))
        except Exception:
            pass

    seen = set()
    uniq = []
    for r in results:
        key = (r.get("title", "").lower(), r.get("artist", "").lower())
        if key in seen:
            continue
        seen.add(key)
        uniq.append(r)
    return uniq[:limit]


def _play_order_keys():
    raw = _cfg().get("play_order") or []
    if isinstance(raw, str):
        raw = [raw]
    return [str(x).strip().lower() for x in raw if str(x).strip()]


def _track_key_text(track: dict) -> str:
    uri = track.get("uri") or ""
    base = os.path.basename(uri)
    stem = os.path.splitext(base)[0]
    return " ".join([
        track.get("title") or "",
        track.get("artist") or "",
        stem,
        base,
    ]).lower()


def local_tracks():
    """本地曲库，用于设备端“播放/上一首/下一首”。

    先按 music.play_order 里列出的歌名匹配（Sugar、Baby...），
    剩下的再按歌名/作者排序。
    """
    items = list(_local_items())
    items.sort(key=lambda t: ((t.get("title") or "").lower(),
                              (t.get("artist") or "").lower()))
    order = _play_order_keys()
    if not order:
        return items

    picked = []
    used = set()
    for key in order:
        match = None
        # 先精确匹配歌名或文件名（不含扩展名），避免 "Baby" 误匹配别的歌
        for t in items:
            if t["id"] in used:
                continue
            title = (t.get("title") or "").lower()
            stem = os.path.splitext(os.path.basename(t.get("uri") or ""))[0].lower()
            if key == title or key == stem:
                match = t
                break
        if match is None:
            for t in items:
                if t["id"] in used:
                    continue
                if key in _track_key_text(t):
                    match = t
                    break
        if match is not None:
            picked.append(match)
            used.add(match["id"])

    picked.extend(t for t in items if t["id"] not in used)
    return picked


def auto_next_enabled() -> bool:
    """设备本地曲库是否一首结束后自动下一首。"""
    return bool(_cfg().get("auto_next", True))


def format_results(results, query: str) -> str:
    if not results:
        return ("没有找到「%s」的可播放音乐。\n"
                "曲库：本地 server/music 目录 + ccMixter（CC 授权）。\n"
                "可以把音乐文件放进 server/music，或换一个关键词。" % (query or "音乐"))
    lines = ["搜索「%s」找到 %d 首可播放音乐：" % (query or "音乐", len(results))]
    for i, t in enumerate(results, 1):
        dur = fmt_seconds(t["duration"]) if t.get("duration") else "时长未知"
        lines.append("%d. 《%s》— %s（%s，%s）" % (
            i, t["title"], t["artist"], dur, t["source"]))
    lines.append("需要播放时调用 music_play(index=N)，N 是上面的序号。")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# 在线歌曲先缓存到本地，再按本地文件播放
# ---------------------------------------------------------------------------
async def _ensure_cached(track: dict) -> dict:
    uri = track.get("uri") or ""
    if track.get("source") == "local" or not uri.startswith(("http://", "https://")):
        return track
    cdir = _cache_dir()
    key = hashlib.md5(uri.encode("utf-8")).hexdigest()
    ext = os.path.splitext(uri.split("?")[0])[1].lower()
    if ext not in AUDIO_EXTS:
        ext = ".mp3"
    path = os.path.join(cdir, key + ext)
    if os.path.isfile(path) and os.path.getsize(path) > 2048:
        track["uri"] = path
        track["cached"] = True
        return track

    headers = {"User-Agent": _UA}
    if track.get("source") == "ccmixter":
        headers["Referer"] = "https://ccmixter.org/"
    tmp = path + ".tmp"
    async with httpx.AsyncClient(timeout=60, follow_redirects=True,
                                 headers=headers) as c:
        async with c.stream("GET", uri) as r:
            r.raise_for_status()
            with open(tmp, "wb") as f:
                async for chunk in r.aiter_bytes(65536):
                    f.write(chunk)
    os.replace(tmp, path)
    track["uri"] = path
    track["cached"] = True
    return track


# ---------------------------------------------------------------------------
# 播放器
# ---------------------------------------------------------------------------
class MusicPlayer:
    def __init__(self, session):
        self.s = session
        self.lock = asyncio.Lock()
        self.task = None
        self.proc = None
        self.cur = None
        self.list = []
        self.idx = -1
        self.state = "stopped"
        self.duration = 0.0
        self.position = 0.0
        self.live = False
        self.rate = int(_cfg().get("stream_sample_rate", 16000) or 16000)
        self._paused = asyncio.Event()
        self._paused.set()
        self._rebase = True
        self._auto_next = False
        self._auto_pending = False

    def status_text(self) -> str:
        if not self.cur:
            return "当前没有播放音乐。"
        state = {"playing": "正在播放", "paused": "已暂停"}.get(self.state, "已停止")
        return "%s：《%s》— %s（%s / %s）" % (
            state, self.cur.get("title", ""), self.cur.get("artist", ""),
            fmt_seconds(self.position), fmt_seconds(self.duration))

    async def play(self, track: dict, playlist=None, index=None, auto_next=False):
        async with self.lock:
            await self._stop_locked(notify=False)
            self._auto_next = bool(auto_next)
            self._auto_pending = False
            if playlist is not None:
                self.list = list(playlist)
            if index is not None:
                self.idx = index
            self.cur = track
            self.live = bool(track.get("live"))
            self.state = "playing"
            self.position = 0.0
            self._paused.set()
            self._rebase = True

            # 在线歌曲先完整缓存，避免边下边播的抖动
            if track.get("source") != "local" and str(track.get("uri", "")).startswith(
                    ("http://", "https://")):
                await self.s.send_cmd("text", text="正在缓存音乐…")
                try:
                    await _ensure_cached(track)
                except Exception as e:
                    self.state = "stopped"
                    await self.s.send_json({
                        "type": "music_error",
                        "message": "缓存失败：%s" % e})
                    return

            dur = float(track.get("duration") or 0)
            if dur <= 0 and not self.live:
                dur = await asyncio.to_thread(_duration_of, track["uri"])
            self.duration = dur

            await self.s.send_cmd("open_app", value="music")
            await self.s.send_json({
                "type": "music_start",
                "title": track.get("title", ""),
                "artist": track.get("artist", ""),
                "source": track.get("source", ""),
                "license": track.get("license", ""),
                "duration": self.duration,
                "position": 0.0,
                "sample_rate": self.rate,
                "index": (self.idx + 1) if self.idx >= 0 else 0,
                "total": len(self.list),
                "live": self.live,
            })
            await self.s.send_json({"type": "music_state", "state": "buffering"})
            self.task = asyncio.create_task(self._run(track))

    async def _run(self, track: dict):
        ff = _ffmpeg()
        if not ff:
            await self.s.send_json({"type": "music_error",
                                    "message": "服务端没有找到 ffmpeg"})
            self.state = "stopped"
            return
        uri = track.get("uri") or ""
        args = [ff, "-nostdin", "-v", "error", "-i", uri,
                "-vn", "-f", "s16le", "-ac", "1", "-ar", str(self.rate), "-"]
        proc = None
        sent = 0.0
        try:
            proc = await asyncio.create_subprocess_exec(
                *args,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.DEVNULL,
                creationflags=(0x08000000 if os.name == "nt" else 0),
            )
            self.proc = proc
            loop = asyncio.get_running_loop()
            play_clock = loop.time()
            sent_since = 0.0
            target_buffer = 3.0
            last_prog = 0.0
            announced = False
            while True:
                if not self._paused.is_set():
                    t0 = loop.time()
                    await self._paused.wait()
                    play_clock += loop.time() - t0
                if self._rebase:
                    self._rebase = False
                    play_clock = loop.time()
                    sent_since = 0.0
                chunk = await proc.stdout.read(4096)
                if not chunk:
                    break
                if self.s.closed:
                    break
                await self.s.send_music_pcm(chunk)
                sec = len(chunk) / (self.rate * 2.0)
                sent += sec
                sent_since += sec
                buffered = sent_since - (loop.time() - play_clock)
                self.position = max(0.0, sent - max(0.0, buffered))
                if not announced and sent_since >= target_buffer:
                    announced = True
                    await self.s.send_json({"type": "music_state", "state": "playing"})
                if buffered > target_buffer:
                    await asyncio.sleep(buffered - target_buffer)
                now = loop.time()
                if now - last_prog >= 2.0:
                    last_prog = now
                    await self.s.send_json({
                        "type": "music_progress",
                        "position": self.position,
                        "duration": self.duration,
                    })
            rc = await proc.wait()
            self.proc = None
            if self.s.closed:
                return
            ok_end = not (rc != 0 and sent <= 0)
            if not ok_end:
                await self.s.send_json({
                    "type": "music_error",
                    "message": "播放失败，文件可能损坏"})
            else:
                await self.s.send_json({"type": "music_end"})
            self.state = "stopped"
            if (ok_end and self._auto_next and self.list
                    and self.idx + 1 < len(self.list) and not self.s.closed):
                self._auto_pending = True
                asyncio.get_running_loop().call_later(0.6, self._auto_advance)
        except asyncio.CancelledError:
            raise
        except Exception as e:
            try:
                await self.s.send_json({"type": "music_error",
                                        "message": "播放失败: %s" % e})
            except Exception:
                pass
            self.state = "stopped"
        finally:
            if proc is not None:
                try:
                    proc.kill()
                except Exception:
                    pass

    def _auto_advance(self):
        if not self._auto_pending:
            return
        self._auto_pending = False
        if self.s.closed or not self._auto_next:
            return
        nxt = self.idx + 1
        if not self.list or nxt >= len(self.list):
            return
        asyncio.get_running_loop().create_task(
            self.play(self.list[nxt], playlist=self.list, index=nxt, auto_next=True))

    async def _stop_locked(self, notify=True):
        self._paused.set()
        self._auto_pending = False
        task, self.task = self.task, None
        if task and not task.done():
            task.cancel()
            try:
                await task
            except (asyncio.CancelledError, Exception):
                pass
        proc, self.proc = self.proc, None
        if proc is not None:
            try:
                proc.kill()
            except Exception:
                pass
        was_active = self.state != "stopped" or self.cur is not None
        self.state = "stopped"
        if notify and was_active and not self.s.closed:
            await self.s.send_json({"type": "music_stop"})

    async def stop(self, notify=True):
        async with self.lock:
            await self._stop_locked(notify=notify)

    async def pause(self):
        if self.state != "playing":
            return
        self._paused.clear()
        self.state = "paused"
        await self.s.send_json({"type": "music_state", "state": "paused"})

    async def resume(self):
        if self.state != "paused":
            return
        self._paused.set()
        self.state = "playing"
        await self.s.send_json({"type": "music_state", "state": "playing"})

    async def toggle(self):
        if self.state == "paused":
            await self.resume()
        elif self.state == "playing":
            await self.pause()

    async def next(self):
        if not self.list:
            return "当前没有播放列表"
        idx = self.idx + 1
        if idx >= len(self.list):
            return "已经是最后一首了"
        await self.play(self.list[idx], playlist=self.list, index=idx,
                        auto_next=self._auto_next)
        return "下一首：" + self.list[idx].get("title", "")

    async def prev(self):
        if not self.list:
            return "当前没有播放列表"
        idx = max(0, self.idx - 1)
        await self.play(self.list[idx], playlist=self.list, index=idx,
                        auto_next=self._auto_next)
        return "上一首：" + self.list[idx].get("title", "")
