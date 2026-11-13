# -*- coding: utf-8 -*-
"""GPT-SoVITS API 客户端：切换权重 + 合成 + WAV 解析。"""
import asyncio
import io
import re
import wave

import httpx

from .config import CFG, resolve


def split_sentences(text: str, max_len: int = 60) -> list:
    """按中/日/英标点切句，适合边合成边播放。"""
    text = (text or "").strip()
    if not text:
        return []

    parts = [p.strip() for p in re.findall(r"[^。！？!?；;\n]+[。！？!?；;…]*", text)
             if p.strip()]
    chunks = []
    buf = ""
    for p in parts:
        if len(buf) + len(p) <= max_len:
            buf += p
        else:
            if buf:
                chunks.append(buf)
            buf = p
    if buf:
        chunks.append(buf)

    out = []
    for c in chunks:
        limit = int(max_len * 1.6)
        while len(c) > limit:
            cut = max(c.rfind("，", 0, limit), c.rfind(",", 0, limit),
                      c.rfind("、", 0, limit), c.rfind(" ", 0, limit))
            if cut <= 0:
                cut = limit
            out.append(c[:cut + 1])
            c = c[cut + 1:]
        if c.strip():
            out.append(c.strip())
    return out


def parse_wav(data: bytes):
    """返回 (sample_rate, pcm16_mono_bytes)。"""
    with wave.open(io.BytesIO(data), "rb") as w:
        sr = w.getframerate()
        ch = w.getnchannels()
        sw = w.getsampwidth()
        frames = w.readframes(w.getnframes())

    if sw != 2:
        raise RuntimeError("TTS 返回的不是 16bit WAV（sampwidth=%d）" % sw)
    if ch == 1:
        return sr, frames

    # 多声道下混
    import numpy as np
    arr = np.frombuffer(frames, dtype=np.int16).reshape(-1, ch)
    mono = arr.mean(axis=1).astype(np.int16)
    return sr, mono.tobytes()


def resample_pcm(sr: int, pcm: bytes, target: int):
    """int16 mono 线性重采样；target<=0 表示保持不变。"""
    if not target or sr == target or not pcm:
        return sr, pcm
    import numpy as np
    x = np.frombuffer(pcm, dtype=np.int16).astype(np.float32)
    if x.size == 0:
        return target, pcm
    n = max(1, int(x.size * target / sr))
    idx = np.linspace(0, x.size - 1, n)
    y = np.interp(idx, np.arange(x.size), x).astype(np.int16)
    return target, y.tobytes()


def wav_bytes(sample_rate: int, pcm: bytes) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(pcm)
    return buf.getvalue()


class TTSClient:
    def __init__(self, cfg=None, voices=None):
        c = (cfg or CFG).get("tts") or {}
        self.base = c.get("base_url", "http://127.0.0.1:9880").rstrip("/")
        self.timeout = float(c.get("timeout", 180))
        self.voices = voices
        # 上传的零样本音色没有自己的权重时，切回 v2 底模，避免串成上一个音色
        self.base_gpt = resolve(c.get("base_gpt_weights", "")) if c.get("base_gpt_weights") else ""
        self.base_sovits = resolve(c.get("base_sovits_weights", "")) if c.get("base_sovits_weights") else ""
        self._lock = asyncio.Lock()
        self._gpt = ""
        self._sovits = ""
        self._client = None

    def _http(self) -> httpx.AsyncClient:
        if self._client is None:
            self._client = httpx.AsyncClient(timeout=self.timeout)
        return self._client

    async def _set_weights(self, kind: str, path: str):
        if not path:
            return
        current = self._gpt if kind == "gpt" else self._sovits
        if current == path:
            return
        resp = await self._http().get("%s/set_%s_weights" % (self.base, kind),
                                      params={"weights_path": path})
        if resp.status_code != 200:
            raise RuntimeError("切换 %s 权重失败: %s" % (kind, resp.text[:200]))
        if kind == "gpt":
            self._gpt = path
        else:
            self._sovits = path

    async def ensure_weights(self, voice: dict):
        gpt = voice.get("gpt_weights") or self.base_gpt
        sovits = voice.get("sovits_weights") or self.base_sovits
        await self._set_weights("gpt", gpt)
        await self._set_weights("sovits", sovits)

    async def synth(self, text: str, voice: dict):
        """返回 (sample_rate, pcm16 mono bytes)"""
        text = (text or "").strip()
        if not text:
            return 32000, b""

        async with self._lock:
            await self.ensure_weights(voice)
            payload = {
                "text": text,
                "text_lang": voice.get("text_lang", "auto"),
                "ref_audio_path": voice["ref_audio"],
                "prompt_text": voice.get("prompt_text", ""),
                "prompt_lang": voice.get("prompt_lang", "ja"),
                "top_k": int(voice.get("top_k", 15)),
                "top_p": float(voice.get("top_p", 1.0)),
                "temperature": float(voice.get("temperature", 1.0)),
                "speed_factor": float(voice.get("speed", 1.0)),
                "text_split_method": "cut5",
                "batch_size": 1,
                "media_type": "wav",
                "streaming_mode": False,
                "seed": -1,
            }
            try:
                resp = await self._http().post(self.base + "/tts", json=payload)
            except httpx.HTTPError as e:
                raise RuntimeError("连接 GPT-SoVITS 失败（先启动 api_v2.py）：%s" % e)

            if resp.status_code != 200:
                raise RuntimeError("GPT-SoVITS 合成失败: %s" % resp.text[:300])
            return parse_wav(resp.content)

    async def synth_stream(self, text: str, voice: dict):
        """流式合成：yield (sample_rate, pcm16_mono_bytes)。

        GPT-SoVITS api_v2 在 streaming_mode=True 时先返回 44 字节 WAV 头，
        之后是逐片 PCM。这样首包 0.3~0.5 秒就能开始播，不用等整句合成完。
        """
        text = (text or "").strip()
        if not text:
            return

        async with self._lock:
            await self.ensure_weights(voice)
            payload = {
                "text": text,
                "text_lang": voice.get("text_lang", "auto"),
                "ref_audio_path": voice["ref_audio"],
                "prompt_text": voice.get("prompt_text", ""),
                "prompt_lang": voice.get("prompt_lang", "ja"),
                "top_k": int(voice.get("top_k", 15)),
                "top_p": float(voice.get("top_p", 1.0)),
                "temperature": float(voice.get("temperature", 1.0)),
                "speed_factor": float(voice.get("speed", 1.0)),
                "text_split_method": "cut5",
                "batch_size": 1,
                "media_type": "wav",
                "streaming_mode": True,
                "seed": -1,
            }
            try:
                async with self._http().stream("POST", self.base + "/tts",
                                               json=payload) as resp:
                    if resp.status_code != 200:
                        body = await resp.aread()
                        raise RuntimeError("GPT-SoVITS 合成失败: %s" % body[:300])
                    header = bytearray()
                    sample_rate = 32000
                    async for chunk in resp.aiter_bytes():
                        if len(header) < 44:
                            need = 44 - len(header)
                            header.extend(chunk[:need])
                            chunk = chunk[need:]
                            if len(header) == 44:
                                sr = int.from_bytes(header[24:28], "little")
                                if 8000 <= sr <= 48000:
                                    sample_rate = sr
                        if chunk:
                            yield sample_rate, chunk
            except httpx.HTTPError as e:
                raise RuntimeError("连接 GPT-SoVITS 失败（先启动 api_v2.py）：%s" % e)

    async def health(self) -> bool:
        try:
            resp = await self._http().get(self.base + "/docs", timeout=3)
            return resp.status_code < 500
        except Exception:
            return False
