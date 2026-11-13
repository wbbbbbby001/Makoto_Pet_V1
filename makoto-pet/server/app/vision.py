# -*- coding: utf-8 -*-
"""图片/扫描版 PDF 的视觉识别客户端（OpenAI 兼容的 image_url 接口）。"""
import base64
import os

import httpx

from .config import CFG


class VisionClient:
    def __init__(self, cfg=None):
        c = cfg or CFG
        v = c.get("vision") or {}
        llm = c.get("llm") or {}
        self.enabled = bool(v.get("enabled", True))
        self.base = (v.get("base_url") or llm.get("base_url") or "").rstrip("/")
        self.api_key = (v.get("api_key") or os.environ.get("MAKOTO_LLM_KEY")
                        or llm.get("api_key") or "")
        self.model = v.get("model") or llm.get("model") or "deepseek-chat"
        self.timeout = float(v.get("timeout", 90))

    @property
    def configured(self) -> bool:
        return bool(self.enabled and self.base and self.api_key)

    def transcribe(self, data: bytes, mime: str = "image/png") -> str:
        if not self.configured:
            raise RuntimeError("没有配置视觉模型（vision.enabled / api_key）")
        b64 = base64.b64encode(data).decode("ascii")
        payload = {
            "model": self.model,
            "messages": [{
                "role": "user",
                "content": [
                    {
                        "type": "text",
                        "text": "请完整转录图片中的所有文字，保持原有顺序和段落；"
                                "不要翻译，不要解释，不要总结。"
                                "如果图片里没有文字，只输出：无文字。",
                    },
                    {
                        "type": "image_url",
                        "image_url": {"url": "data:%s;base64,%s" % (mime, b64)},
                    },
                ],
            }],
            "temperature": 0.1,
            "max_tokens": 2048,
        }
        with httpx.Client(timeout=self.timeout) as client:
            resp = client.post(
                self.base + "/chat/completions",
                headers={"Authorization": "Bearer " + self.api_key,
                         "Content-Type": "application/json"},
                json=payload,
            )
        if resp.status_code != 200:
            raise RuntimeError("视觉接口 %s: %s" % (resp.status_code, resp.text[:300]))
        data = resp.json()
        return (data["choices"][0]["message"].get("content") or "").strip()
