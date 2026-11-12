# -*- coding: utf-8 -*-
"""音色档案管理：预置橘真琴 + 用户上传的参考音频（零样本克隆）。"""
import json
import os
import re
import threading
import time
import uuid

from .config import CFG, resolve


def _slug(text: str) -> str:
    text = (text or "").strip().lower()
    text = re.sub(r"[^\w一-鿿-]+", "-", text)
    return text.strip("-")[:32] or "voice"


class VoiceStore:
    def __init__(self, path: str, refs_dir: str, cfg=None):
        self.path = path
        self.refs_dir = refs_dir
        self.cfg = cfg or CFG
        self._lock = threading.Lock()
        os.makedirs(refs_dir, exist_ok=True)
        self.data = self._load()

    # ---------------- 内部 ----------------
    def _load(self) -> dict:
        if os.path.exists(self.path):
            with open(self.path, "r", encoding="utf-8") as f:
                data = json.load(f)
        else:
            data = {"voices": [], "default": ""}

        if not data.get("voices"):
            builtin = (self.cfg.get("tts") or {}).get("builtin_voices") or []
            for v in builtin:
                item = dict(v)
                item["ref_audio"] = resolve(item.get("ref_audio", ""))
                item["gpt_weights"] = resolve(item.get("gpt_weights", "")) if item.get("gpt_weights") else ""
                item["sovits_weights"] = resolve(item.get("sovits_weights", "")) if item.get("sovits_weights") else ""
                item.setdefault("speed", 1.0)
                item.setdefault("top_k", 15)
                item.setdefault("top_p", 1.0)
                item.setdefault("temperature", 1.0)
                item["builtin"] = True
                data["voices"].append(item)
            data["default"] = (self.cfg.get("tts") or {}).get("default_voice") or (
                data["voices"][0]["id"] if data["voices"] else "")

        for v in data.get("voices", []):
            v["ref_audio"] = resolve(v.get("ref_audio", ""))
            if v.get("gpt_weights"):
                v["gpt_weights"] = resolve(v["gpt_weights"])
            if v.get("sovits_weights"):
                v["sovits_weights"] = resolve(v["sovits_weights"])

        if not data.get("default") and data.get("voices"):
            data["default"] = data["voices"][0]["id"]

        self._save_raw(data)
        return data

    def _save_raw(self, data: dict):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        os.replace(tmp, self.path)

    def _save(self):
        self._save_raw(self.data)

    # ---------------- 对外 ----------------
    @property
    def default_id(self) -> str:
        return self.data.get("default", "")

    def all(self) -> list:
        return list(self.data.get("voices", []))

    def public_list(self) -> list:
        out = []
        for v in self.all():
            item = dict(v)
            item.pop("gpt_weights", None)
            item.pop("sovits_weights", None)
            out.append(item)
        return out

    def get(self, voice_id: str = None) -> dict:
        voice_id = voice_id or self.default_id
        for v in self.all():
            if v["id"] == voice_id:
                return v
        return self.all()[0]

    def find(self, key: str):
        if not key:
            return None
        key_l = key.strip().lower()
        for v in self.all():
            if v["id"].lower() == key_l or v["name"].lower() == key_l:
                return v
        for v in self.all():
            if key_l in v["name"].lower() or key_l in v["id"].lower():
                return v
        return None

    def set_default(self, voice_id: str) -> bool:
        with self._lock:
            if not self.find(voice_id):
                return False
            self.data["default"] = voice_id
            self._save()
            return True

    def add(self, name: str, ref_audio: str, prompt_text: str,
            prompt_lang: str = "ja", text_lang: str = "auto",
            gpt_weights: str = "", sovits_weights: str = "",
            speed: float = 1.0) -> dict:
        with self._lock:
            # id 只用 ASCII，避免出现在 URL 里
            prefix = _slug(name)
            prefix = re.sub(r"[^a-z0-9-]", "", prefix) or "voice"
            vid = "%s-%s" % (prefix[:16], uuid.uuid4().hex[:6])
            item = {
                "id": vid,
                "name": name,
                "ref_audio": os.path.abspath(ref_audio),
                "prompt_text": prompt_text,
                "prompt_lang": prompt_lang,
                "text_lang": text_lang,
                "gpt_weights": os.path.abspath(gpt_weights) if gpt_weights else "",
                "sovits_weights": os.path.abspath(sovits_weights) if sovits_weights else "",
                "speed": float(speed),
                "top_k": 15,
                "top_p": 1.0,
                "temperature": 1.0,
                "builtin": False,
                "created_at": int(time.time()),
            }
            self.data.setdefault("voices", []).append(item)
            self._save()
            return item

    def delete(self, voice_id: str) -> bool:
        with self._lock:
            before = len(self.data.get("voices", []))
            self.data["voices"] = [v for v in self.data.get("voices", [])
                                   if v["id"] != voice_id]
            if len(self.data["voices"]) == before:
                return False
            if self.data.get("default") == voice_id and self.data["voices"]:
                self.data["default"] = self.data["voices"][0]["id"]
            self._save()
            return True
