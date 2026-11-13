# -*- coding: utf-8 -*-
"""OpenAI 兼容接口 + 工具调用（联网搜索 / 天气 / 切换音色）。"""
import asyncio
import json
import os
import re
import time

import httpx

from .config import CFG
from . import tools as tools_mod


class LLM:
    def __init__(self, cfg=None):
        c = (cfg or CFG).get("llm") or {}
        self.base = (c.get("base_url") or "").rstrip("/")
        self.api_key = os.environ.get("MAKOTO_LLM_KEY") or c.get("api_key") or ""
        self.model = c.get("model", "deepseek-chat")
        self.temperature = float(c.get("temperature", 0.75))
        self.max_tokens = int(c.get("max_tokens", 512))
        self.timeout = float(c.get("timeout", 60))
        self.max_tool_rounds = int(c.get("max_tool_rounds", 20))
        self.max_tool_seconds = int(c.get("max_tool_seconds", 180))
        self.short_max_tokens = int(c.get("short_max_tokens", 400))
        self.deep_max_tokens = int(c.get("deep_max_tokens", 1200))
        self.short_tool_rounds = int(c.get("short_tool_rounds", 3))
        self.short_tool_seconds = int(c.get("short_tool_seconds", 30))
        self.system_prompt = (c.get("system_prompt") or "").strip()
        self._cc = None

    def _simplify(self, text: str) -> str:
        """中文输出强制简体。含日文假名的文本按日语保留，不转换。"""
        if not text:
            return text
        # 去掉表情/符号（设备字体没有 emoji，会显示方框）
        text = re.sub(r"[\U0001F000-\U0001FAFF☀-➿️]", "", text).strip()
        if re.search(r"[぀-ヿ]", text):
            return text
        try:
            if self._cc is None:
                from opencc import OpenCC
                self._cc = OpenCC("t2s")
            return self._cc.convert(text)
        except Exception:
            return text

    @staticmethod
    def _is_deep(text: str) -> bool:
        t = (text or "").lower()
        if any(k in t for k in ("简短", "一句话", "别太长", "简单说", "短一点")):
            return False
        return any(k in t for k in (
            "深度思考", "深度分析", "深入分析", "深入讲", "详细", "展开",
            "仔细查", "认真查", "全面", "长一点", "多讲",
        ))

    @property
    def configured(self) -> bool:
        return bool(self.api_key and self.base)

    async def _post(self, payload: dict) -> dict:
        headers = {"Content-Type": "application/json"}
        if self.api_key:
            headers["Authorization"] = "Bearer " + self.api_key
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(self.base + "/chat/completions",
                                     headers=headers, json=payload)
            if resp.status_code != 200:
                raise RuntimeError("大模型接口 %s: %s" % (resp.status_code, resp.text[:300]))
            return resp.json()

    async def chat(self, text: str, on_tool=None, history=None,
                   extra_context: str = None, lang: str = "ja") -> str:
        if not self.configured:
            return ("我还没有配置大模型接口呢。请在 server/config.yaml 里填好 llm.api_key，"
                    "或者设置环境变量 MAKOTO_LLM_KEY。")

        deep = self._is_deep(text)
        budget_tokens = self.deep_max_tokens if deep else self.short_max_tokens
        budget_rounds = self.max_tool_rounds if deep else self.short_tool_rounds
        budget_seconds = self.max_tool_seconds if deep else self.short_tool_seconds

        system_content = self.system_prompt
        system_content += ("\n\n【当前时间】" + tools_mod.current_time_text() +
                           "\n用户问今天几号、星期几、现在几点、某个日期是星期几时，"
                           "直接使用这个时间或调用 get_date_info，不要联网搜索。")
        if (lang or "ja").lower().startswith("zh"):
            system_content += "\n\n【本轮语言】请用简体中文回答，禁止繁体字。"
        else:
            system_content += "\n\n【本轮语言】请用自然的日语回答，不要附带中文翻译。"
        if deep:
            system_content += ("\n\n【本轮模式】用户要求深度思考：请详细展开，"
                               "300~800 字，先结论再分点；不要使用 #、**、- 等 "
                               "Markdown 符号，用“第一、第二、第三”分点。")
        else:
            system_content += ("\n\n【本轮模式】默认简短：2~4 句，约 80~200 字，"
                               "不要长篇大论。")
        if extra_context:
            system_content += ("\n\n【知识库优先检索结果】\n" + extra_context +
                               "\n\n优先使用以上知识库内容；不够的部分再联网搜索。")

        messages = [{"role": "system", "content": system_content}]
        if history:
            messages.extend(history[-8:])
        messages.append({"role": "user", "content": text})

        started = time.time()
        deadline = (started + budget_seconds) if budget_seconds > 0 else None
        stop_reason = None

        for round_idx in range(budget_rounds):
            if deadline is not None and time.time() >= deadline:
                stop_reason = "time"
                break

            payload = {
                "model": self.model,
                "messages": messages,
                "temperature": self.temperature,
                "max_tokens": budget_tokens,
                "stream": False,
                "tools": tools_mod.TOOL_SCHEMA,
                "tool_choice": "auto",
            }
            try:
                if deadline is not None:
                    remain = max(0.5, deadline - time.time())
                    data = await asyncio.wait_for(
                        self._post(payload), timeout=min(self.timeout, remain))
                else:
                    data = await self._post(payload)
            except asyncio.TimeoutError:
                print("[llm] LLM request hit the %ds time budget" % budget_seconds)
                stop_reason = "time"
                break

            msg = data["choices"][0]["message"]
            tool_calls = msg.get("tool_calls") or []
            if not tool_calls:
                return self._simplify((msg.get("content") or "").strip())

            print("[llm] round %d tool calls: %s" % (
                round_idx + 1,
                [(tc.get("function") or {}).get("name") for tc in tool_calls]))

            messages.append({
                "role": "assistant",
                "content": msg.get("content") or "",
                "tool_calls": tool_calls,
            })
            timed_out = False
            for i, tc in enumerate(tool_calls):
                fn = tc.get("function") or {}
                name = fn.get("name", "")
                try:
                    args = json.loads(fn.get("arguments") or "{}")
                except Exception:
                    args = {}
                try:
                    call = (on_tool(name, args) if on_tool is not None
                            else tools_mod.execute(name, args))
                    if deadline is not None:
                        remain = max(0.5, deadline - time.time())
                        result = await asyncio.wait_for(call, timeout=remain)
                    else:
                        result = await call
                except asyncio.TimeoutError:
                    result = "工具执行超时（已到 %ds 预算，被中断）" % budget_seconds
                    timed_out = True
                except Exception as e:
                    result = "工具执行失败：%s" % e
                messages.append({
                    "role": "tool",
                    "tool_call_id": tc.get("id") or ("call_%d" % i),
                    "content": str(result)[:6000],
                })
            if timed_out:
                stop_reason = "time"
                break

        # 次数或时间预算用完：不编造，先向用户追问一个关键信息；用户回答后会带着
        # 历史继续搜（下一轮 chat 会重新获得完整的 20 轮 / 180 秒预算）。
        print("[llm] stop (%s), asking user for clarification" % (stop_reason or "rounds"))
        try:
            messages.append({
                "role": "user",
                "content": (
                    "你已经查了很多次仍未得到确定答案。请不要编造。"
                    "先用一两句话说明目前查到的线索，然后向用户追问一个最能帮助定位答案的"
                    "关键信息（例如准确全名、地区、年份、作品名或具体网址）。"
                    "不要调用工具。"
                ),
            })
            data = await asyncio.wait_for(self._post({
                "model": self.model,
                "messages": messages,
                "temperature": self.temperature,
                "max_tokens": budget_tokens,
                "stream": False,
            }), timeout=min(self.timeout, 30))
            content = (data["choices"][0]["message"].get("content") or "").strip()
            if content:
                return self._simplify(content)
        except Exception as e:
            print("[llm] clarification request failed:", e)

        if (lang or "ja").lower().startswith("zh"):
            return ("我查了很多次还是没找到能确认的答案。你能再具体说一下吗？"
                    "比如准确的全名、地区、年份，或者你是在哪里看到这个信息的。")
        return ("何度も調べましたが、確かな答えが見つかりませんでした。"
                "もう少し具体的に教えてもらえますか？"
                "正確な名前や地域、年、どこで見た情報か、などです。")

    async def health(self) -> bool:
        if not self.configured:
            return False
        try:
            async with httpx.AsyncClient(timeout=8) as client:
                resp = await client.get(self.base + "/models", headers={
                    "Authorization": "Bearer " + self.api_key})
                return resp.status_code < 500
        except Exception:
            return False
