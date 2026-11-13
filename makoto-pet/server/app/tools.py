# -*- coding: utf-8 -*-
"""给大模型用的工具：联网搜索、抓网页、天气。"""
import asyncio
import json
import re
from datetime import datetime, timedelta, timezone

import httpx

from .config import CFG

_TZ = timezone(timedelta(hours=8))
_WEEKDAYS = ["星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"]


def current_time_text() -> str:
    now = datetime.now(_TZ)
    return "现在是 %d年%d月%d日 %s %02d:%02d（北京时间 UTC+8）" % (
        now.year, now.month, now.day, _WEEKDAYS[now.weekday()], now.hour, now.minute)


async def get_current_time() -> str:
    return current_time_text()


def _parse_date(text: str):
    t = (text or "").strip()
    m = re.match(r"(\d{4})\s*[-/.年]\s*(\d{1,2})\s*[-/.月]\s*(\d{1,2})", t)
    if not m:
        m = re.match(r"(\d{4})(\d{2})(\d{2})", t)
    if not m:
        return None
    try:
        return datetime(int(m.group(1)), int(m.group(2)), int(m.group(3)), tzinfo=_TZ)
    except ValueError:
        return None


async def get_date_info(date_str: str) -> str:
    dt = _parse_date(date_str)
    if not dt:
        return "日期格式无法识别，请用 YYYY-MM-DD，例如 2026-11-10。"
    today = datetime.now(_TZ).date()
    delta = (dt.date() - today).days
    if delta == 0:
        rel = "就是今天"
    elif delta > 0:
        rel = "%d 天后" % delta
    else:
        rel = "%d 天前" % (-delta)
    return "%d年%d月%d日 是 %s，%s。" % (
        dt.year, dt.month, dt.day, _WEEKDAYS[dt.weekday()], rel)

TOOL_SCHEMA = [
    {
        "type": "function",
        "function": {
            "name": "get_current_time",
            "description": "获取当前日期、星期和时间（北京时间）。用户问今天几号、"
                           "星期几、现在几点时使用，不要联网搜索。",
            "parameters": {"type": "object", "properties": {}},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "get_date_info",
            "description": "查询某个日期是星期几、距今多少天",
            "parameters": {
                "type": "object",
                "properties": {
                    "date": {"type": "string", "description": "YYYY-MM-DD，例如 2026-11-10"},
                },
                "required": ["date"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "knowledge_search",
            "description": "优先查询本地知识库（人设、常问资料、产品说明等）。"
                           "回答问题前先查这里；知识库没有或不够，再调用 web_search。",
            "parameters": {
                "type": "object",
                "properties": {"query": {"type": "string", "description": "查询关键词"}},
                "required": ["query"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "web_search",
            "description": "联网搜索。用于新闻、实时信息、事实核查、人物/作品/产品资料等。"
                           "中文机构/学校/人名建议用双引号精确匹配，例如：\"北京师范大学珠海校区\" 校长",
            "parameters": {
                "type": "object",
                "properties": {
                    "query": {"type": "string", "description": "搜索关键词"},
                    "max_results": {"type": "integer", "description": "返回条数，默认 5"},
                },
                "required": ["query"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "fetch_page",
            "description": "打开一个网页并提取正文。搜索摘要不够时再用",
            "parameters": {
                "type": "object",
                "properties": {"url": {"type": "string"}},
                "required": ["url"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "get_weather",
            "description": "查询城市当前天气和未来两天预报",
            "parameters": {
                "type": "object",
                "properties": {
                    "city": {"type": "string", "description": "城市名，如 深圳、东京"},
                },
                "required": ["city"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "switch_voice",
            "description": "切换当前说话的音色（voice_id 或音色名）",
            "parameters": {
                "type": "object",
                "properties": {
                    "voice_id": {"type": "string", "description": "音色 id 或名称"},
                },
                "required": ["voice_id"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "device_set_volume",
            "description": "设置设备扬声器音量",
            "parameters": {
                "type": "object",
                "properties": {"volume": {"type": "integer", "description": "0-100"}},
                "required": ["volume"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "device_set_brightness",
            "description": "设置设备屏幕亮度",
            "parameters": {
                "type": "object",
                "properties": {"brightness": {"type": "integer", "description": "5-100"}},
                "required": ["brightness"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "device_open_app",
            "description": "在设备桌面上打开应用",
            "parameters": {
                "type": "object",
                "properties": {
                    "app": {
                        "type": "string",
                        "enum": ["desktop", "games", "2048", "snake", "note", "settings",
                                 "music", "camera"],
                    },
                },
                "required": ["app"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "device_write_note",
            "description": "把一段文字追加到设备记事本",
            "parameters": {
                "type": "object",
                "properties": {"text": {"type": "string"}},
                "required": ["text"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "music_search",
            "description": "搜索可播放的音乐。曲库：本地 server/music 目录 + ccMixter（CC 授权）。"
                           "主流平台的版权歌曲不在这里，搜不到时如实说明，不要编造链接。",
            "parameters": {
                "type": "object",
                "properties": {
                    "query": {"type": "string", "description": "歌名、歌手、风格或关键词"},
                    "source": {
                        "type": "string",
                        "enum": ["auto", "local", "ccmixter"],
                        "description": "搜索来源，默认 auto（先本地再 CC 曲库）",
                    },
                },
                "required": ["query"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "music_play",
            "description": "播放音乐。可以给 query 搜索并播放第一首，"
                           "或用 index 播放上一次 music_search 结果中的第 N 首。"
                           "在线歌曲会先缓存到本地再播放；播放期间设备进入音乐模式。",
            "parameters": {
                "type": "object",
                "properties": {
                    "query": {"type": "string", "description": "歌名/歌手/风格；和 index 二选一"},
                    "index": {"type": "integer", "description": "music_search 结果序号，从 1 开始"},
                    "source": {
                        "type": "string",
                        "enum": ["auto", "local", "ccmixter"],
                    },
                },
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "music_control",
            "description": "控制正在播放的音乐：暂停、继续、停止、下一首、上一首",
            "parameters": {
                "type": "object",
                "properties": {
                    "action": {
                        "type": "string",
                        "enum": ["pause", "resume", "stop", "next", "prev"],
                    },
                },
                "required": ["action"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "music_status",
            "description": "查询当前播放的音乐、进度和状态",
            "parameters": {"type": "object", "properties": {}},
        },
    },
]

_UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
       "(KHTML, like Gecko) Chrome/124.0 Safari/537.36")


def _fmt_results(results, query: str) -> str:
    lines = ["搜索「%s」的结果：" % query]
    for i, r in enumerate(results, 1):
        title = r.get("title") or ""
        url = r.get("href") or r.get("url") or ""
        body = (r.get("body") or r.get("content") or "").strip().replace("\n", " ")
        if len(body) > 400:
            body = body[:400] + "…"
        lines.append("%d. %s\n   %s\n   %s" % (i, title, url, body))
    return "\n".join(lines)


def _search_ddgs(query: str, n: int):
    try:
        from ddgs import DDGS
    except ImportError:  # 兼容旧包名
        from duckduckgo_search import DDGS
    with DDGS() as d:
        return list(d.text(query, max_results=n))


async def _search_bing_cn(query: str, n: int, timeout: float = 12):
    """国内可直接访问的 cn.bing.com，不需要 Key。"""
    async with httpx.AsyncClient(timeout=timeout, follow_redirects=True,
                                 headers={"User-Agent": _UA}) as client:
        resp = await client.get("https://cn.bing.com/search",
                                params={"q": query, "setlang": "zh-CN"})
        resp.raise_for_status()
        html = resp.text

    from bs4 import BeautifulSoup
    soup = BeautifulSoup(html, "html.parser")
    results = []
    for li in soup.select("li.b_algo")[:n]:
        h2 = li.find("h2")
        a = h2.find("a") if h2 else None
        if not a:
            continue
        p = li.find("p")
        results.append({
            "title": a.get_text(" ", strip=True),
            "href": a.get("href", ""),
            "body": p.get_text(" ", strip=True) if p else "",
        })
    return results


async def _search_sogou(query: str, n: int, timeout: float = 12):
    """Sogou：中文实体/人物查询结果通常比 Bing CN 准。"""
    async with httpx.AsyncClient(timeout=timeout, follow_redirects=True,
                                 headers={"User-Agent": _UA}) as client:
        resp = await client.get("https://www.sogou.com/web",
                                params={"query": query})
        resp.raise_for_status()
        html = resp.text

    from bs4 import BeautifulSoup
    soup = BeautifulSoup(html, "html.parser")
    results = []
    for div in soup.select("div.vrwrap"):
        a = div.select_one("h3 a") or div.select_one("a")
        if not a:
            continue
        title = a.get_text(" ", strip=True)
        if not title or "搜狗图片" in title:
            continue
        href = a.get("href", "")
        if href.startswith("/"):
            href = "https://www.sogou.com" + href
        body = div.get_text(" ", strip=True)
        if title and title in body:
            body = body.replace(title, "", 1).strip()
        results.append({"title": title, "href": href, "body": body[:400]})
        if len(results) >= n:
            break
    return results


async def web_search(query: str, max_results: int = None) -> str:
    sc = CFG.get("search") or {}
    n = int(max_results or sc.get("max_results", 5))
    provider = sc.get("provider", "ddgs")
    timeout = float(sc.get("timeout", 12))

    try:
        if provider == "tavily":
            key = sc.get("tavily_api_key", "")
            if not key:
                return "搜索失败：没有配置 tavily_api_key"
            async with httpx.AsyncClient(timeout=timeout) as client:
                resp = await client.post("https://api.tavily.com/search", json={
                    "api_key": key,
                    "query": query,
                    "max_results": n,
                    "search_depth": "basic",
                    "include_answer": True,
                })
                resp.raise_for_status()
                data = resp.json()
            results = [{"title": r.get("title"), "href": r.get("url"),
                        "body": r.get("content")} for r in data.get("results", [])]
            out = _fmt_results(results, query)
            if data.get("answer"):
                out = "简要答案：" + data["answer"] + "\n\n" + out
            return out

        if provider == "searxng":
            base = sc.get("searxng_url", "").rstrip("/")
            if not base:
                return "搜索失败：没有配置 searxng_url"
            async with httpx.AsyncClient(timeout=timeout, headers={"User-Agent": _UA}) as client:
                resp = await client.get(base + "/search",
                                        params={"q": query, "format": "json"})
                resp.raise_for_status()
                data = resp.json()
            results = [{"title": r.get("title"), "href": r.get("url"),
                        "body": r.get("content")} for r in data.get("results", [])][:n]
            return _fmt_results(results, query)

        if provider == "bing_cn":
            results = await _search_bing_cn(query, n, timeout)
            return _fmt_results(results, query) if results else "没有搜到结果。"

        if provider == "sogou":
            results = await _search_sogou(query, n, timeout)
            return _fmt_results(results, query) if results else "没有搜到结果。"

        if provider == "ddgs":
            results = await asyncio.to_thread(_search_ddgs, query, n)
            return _fmt_results(results, query) if results else "没有搜到结果。"

        # auto：中文查询优先 Sogou（人名/机构更准），再 Bing CN，最后 DuckDuckGo
        if provider == "auto" and re.search(r"[一-鿿]", query):
            try:
                results = await _search_sogou(query, n, timeout)
                if results:
                    return _fmt_results(results, query)
            except Exception:
                pass
        try:
            results = await _search_bing_cn(query, n, timeout)
            if results:
                return _fmt_results(results, query)
        except Exception:
            pass
        results = await asyncio.to_thread(_search_ddgs, query, n)
        if not results:
            return "没有搜到结果。可以换个关键词再试。"
        return _fmt_results(results, query)
    except Exception as e:
        return "搜索失败：%s" % e


def _strip_html(html: str) -> str:
    html = re.sub(r"(?is)<(script|style|noscript).*?</\1>", " ", html)
    html = re.sub(r"(?s)<[^>]+>", " ", html)
    html = re.sub(r"&nbsp;", " ", html)
    html = re.sub(r"&amp;", "&", html)
    html = re.sub(r"\s+", " ", html)
    return html.strip()


async def fetch_page(url: str) -> str:
    if not url.startswith(("http://", "https://")):
        return "只支持 http/https 链接"
    try:
        async with httpx.AsyncClient(timeout=15, follow_redirects=True,
                                     headers={"User-Agent": _UA}) as client:
            resp = await client.get(url)
            resp.raise_for_status()
            html = resp.text
        text = ""
        try:
            import trafilatura
            text = trafilatura.extract(html) or ""
        except Exception:
            pass
        if not text:
            text = _strip_html(html)
        return text[:6000] if text else "页面没有提取到正文"
    except Exception as e:
        return "打开网页失败：%s" % e


async def get_weather(city: str) -> str:
    try:
        async with httpx.AsyncClient(timeout=15, headers={"User-Agent": _UA}) as client:
            resp = await client.get("https://wttr.in/%s" % city,
                                    params={"format": "j1", "lang": "zh"})
            resp.raise_for_status()
            data = resp.json()
    except Exception as e:
        return "查天气失败：%s" % e

    try:
        cur = data["current_condition"][0]
        desc = (cur.get("lang_zh") or cur.get("weatherDesc") or [{}])[0].get("value", "")
        out = ["%s 现在 %s℃，%s，湿度 %s%%" % (
            city, cur.get("temp_C"), desc, cur.get("humidity"))]
        for day in data.get("weather", [])[:2]:
            out.append("%s：%s ~ %s℃，%s" % (
                day.get("date"),
                day["mintempC"], day["maxtempC"],
                (day.get("hourly") or [{}])[4].get("lang_zh", [{}])[0].get("value", "")
                if day.get("hourly") else ""))
        return "；".join(out)
    except Exception:
        return "天气数据解析失败"


async def execute(name: str, args: dict) -> str:
    args = args or {}
    if name == "get_current_time":
        return await get_current_time()
    if name == "get_date_info":
        return await get_date_info(args.get("date", ""))
    if name == "web_search":
        return await web_search(args.get("query", ""), args.get("max_results"))
    if name == "fetch_page":
        return await fetch_page(args.get("url", ""))
    if name == "get_weather":
        return await get_weather(args.get("city", "深圳"))
    return "未知工具：%s" % name
