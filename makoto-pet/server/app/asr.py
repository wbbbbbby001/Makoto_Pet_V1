# -*- coding: utf-8 -*-
"""本地语音识别（faster-whisper）。设备上传 16k / 16bit / 单声道 PCM。"""
import asyncio
import threading
import time

import numpy as np

from .config import CFG


class ASR:
    def __init__(self, cfg=None):
        self.cfg = (cfg or CFG).get("asr") or {}
        self.provider = self.cfg.get("provider", "faster_whisper")
        self._model = None
        self._lock = threading.Lock()
        self._infer_lock = threading.Lock()
        self._loading = False

    # ---------------- 模型 ----------------
    def _ensure_model(self):
        if self.provider == "none":
            return None
        if self._model is not None:
            return self._model
        with self._lock:
            if self._model is not None:
                return self._model
            from faster_whisper import WhisperModel  # 延迟导入，启动更快

            name = self.cfg.get("model", "small")
            device = self.cfg.get("device", "cuda")
            compute = self.cfg.get("compute_type", "float16" if device == "cuda" else "int8")
            try:
                self._model = WhisperModel(name, device=device, compute_type=compute)
            except Exception as e:
                print("[asr] CUDA 加载失败，改用 CPU(int8)：%s" % e)
                self._model = WhisperModel(name, device="cpu", compute_type="int8")
        return self._model

    # ---------------- 识别 ----------------
    def transcribe_sync(self, pcm: bytes) -> str:
        if self.provider == "none" or not pcm:
            return ""

        audio = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0
        if audio.size == 0:
            return ""

        rms = float(np.sqrt(np.mean(audio * audio) + 1e-12))
        if rms < 0.004:
            return ""

        model = self._ensure_model()
        if model is None:
            return ""

        lang = self.cfg.get("language", "auto")
        lang = None if lang in ("auto", "", None) else lang

        t0 = time.time()
        # ctranslate2 的同一模型不要并发推理，串行化
        with self._infer_lock:
            segments, _info = model.transcribe(
                audio,
                language=lang,
                beam_size=int(self.cfg.get("beam_size", 1)),
                vad_filter=True,
                condition_on_previous_text=False,
                no_speech_threshold=0.6,
            )
            text = "".join(seg.text for seg in segments).strip()
        print("[asr] %.1fs %r" % (time.time() - t0, text))
        return text

    async def transcribe(self, pcm: bytes) -> str:
        return await asyncio.to_thread(self.transcribe_sync, pcm)

    async def warmup(self):
        """启动时把模型加载好，避免第一句话等十几秒。"""
        await asyncio.to_thread(self._ensure_model)

    @property
    def ready(self) -> bool:
        return self.provider == "none" or self._model is not None
