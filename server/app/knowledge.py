# -*- coding: utf-8 -*-
"""本地知识库：预添加资料，回答时优先检索；没有命中再联网。

检索用「中文二字组 + 英文单词」重叠打分，不依赖分词库，小知识库足够用。
"""
import json
import os
import re
import threading
import time
import uuid


def _tokens(text: str) -> set:
    text = (text or "").lower()
    chars = re.findall(r"[一-鿿]", text)
    bigrams = ["".join(chars[i:i + 2]) for i in range(len(chars) - 1)]
    words = re.findall(r"[a-z0-9_]+", text)
    return set(bigrams) | set(words)


class KnowledgeBase:
    def __init__(self, path: str):
        self.path = path
        self._lock = threading.Lock()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.data = self._load()

    def _load(self) -> dict:
        if os.path.exists(self.path):
            try:
                with open(self.path, "r", encoding="utf-8") as f:
                    d = json.load(f)
                if isinstance(d, dict) and isinstance(d.get("items"), list):
                    return d
            except Exception:
                pass
        return {"items": []}

    def _save(self):
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.data, f, ensure_ascii=False, indent=2)
        os.replace(tmp, self.path)

    def all(self) -> list:
        return list(self.data.get("items", []))

    def public_list(self) -> list:
        out = []
        for it in self.all():
            out.append({
                "id": it["id"],
                "title": it["title"],
                "tags": it.get("tags", ""),
                "snippet": it["content"][:160],
                "content_len": len(it["content"]),
                "created_at": it.get("created_at", 0),
            })
        return out

    def add(self, title: str, content: str, tags: str = "") -> dict:
        title = (title or "").strip()[:80]
        content = (content or "").strip()[:8000]
        if not title or not content:
            raise ValueError("title and content are required")
        item = {
            "id": "kb-" + uuid.uuid4().hex[:8],
            "title": title,
            "content": content,
            "tags": (tags or "").strip()[:120],
            "created_at": int(time.time()),
        }
        with self._lock:
            self.data.setdefault("items", []).insert(0, item)
            self._save()
        return item

    def delete(self, item_id: str) -> bool:
        with self._lock:
            before = len(self.data.get("items", []))
            self.data["items"] = [x for x in self.data.get("items", []) if x["id"] != item_id]
            if len(self.data["items"]) == before:
                return False
            self._save()
            return True

    def search(self, query: str, limit: int = 3) -> list:
        qt = _tokens(query)
        if not qt:
            return []
        scored = []
        for it in self.all():
            title_tokens = _tokens(it["title"])
            body_tokens = _tokens(it["content"] + " " + it.get("tags", ""))
            hit = len(qt & body_tokens)
            title_hit = len(qt & title_tokens)
            if hit == 0:
                continue
            score = hit / len(qt) + 0.4 * title_hit
            if score >= 0.2:
                scored.append((score, it))
        scored.sort(key=lambda x: x[0], reverse=True)
        return [it for _, it in scored[:limit]]

    def search_text(self, query: str, limit: int = 3) -> str:
        hits = self.search(query, limit=limit)
        if not hits:
            return ""
        parts = []
        for i, it in enumerate(hits, 1):
            tag = ("（标签：%s）" % it["tags"]) if it.get("tags") else ""
            parts.append("[%d] 《%s》%s\n%s" % (i, it["title"], tag, it["content"][:1500]))
        return "\n\n".join(parts)
