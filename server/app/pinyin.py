# -*- coding: utf-8 -*-
"""本地拼音输入法：jieba 词频 + wordfreq + pypinyin，DP/beam 搜索。

词库 = jieba 前 15 万高频中文词 + wordfreq 前 6 万词，统一换算成 zipf 分数；
首次使用构建并缓存到 server/data/pinyin_dict.pkl。

相比第一版：
- 词库从 4 万键扩到 11 万+键，覆盖人名/地名/常用搭配；
- 候选从 4 条扩到 8 条（设备显示 6 条）；
- 过滤只由生僻字组成的备选，避免“你灏/你鎬”这种干扰项；
- 补充了一批口语短语（请帮我、你在干嘛、吃饭了吗……）。
"""
import math
import os
import pickle
import re
import threading
import time

_WORD_COUNT = 60000          # wordfreq 词数
_JIEBA_LIMIT = 150000        # jieba 取前多少个高频词
_CACHE_VERSION = 7
_PENALTY = 9.0               # P(word) ≈ 10^(zipf-9)
_BEAM = 512
_MAX_INPUT = 64

# 常用口语/设备短语：词库里没有或权重不够的，手动补。
_EXTRA = {
    # 整句
    "今天是星期几": 8.0, "今天星期几": 7.6, "今天几号": 7.4, "现在几点": 7.4,
    "你叫什么名字": 7.3, "我听不懂": 7.1, "请再说一遍": 7.1, "我很好": 7.0,
    "你在干嘛": 7.0, "吃饭了吗": 7.0, "晚上吃什么": 6.8, "我的天哪": 6.8,
    "我的天呐": 6.8, "今天真开心": 6.6, "今天很开心": 6.6,
    "我们是好朋友": 6.6, "我喜欢你的笑脸": 6.8,
    # 请求/操作
    "请帮我": 7.2, "帮我": 6.9, "帮我一下": 6.9, "帮我翻译": 6.9,
    "翻译一下": 6.9, "打开": 6.4, "关闭": 6.4, "看一下": 6.5,
    "听一下": 6.4, "试一下": 6.4, "开一下": 6.4, "关一下": 6.4,
    "等一下": 6.6, "我马上": 6.4, "知道了": 6.6, "好的": 6.4, "没问题": 6.6,
    # 交流
    "你好呀": 6.8, "好呀": 6.4, "我爱你": 7.2, "我喜欢你": 7.2, "我想你": 7.0,
    "谢谢你": 6.7, "谢谢": 6.5, "不客气": 6.4, "对不起": 6.6, "没关系": 6.5,
    "再见": 6.5, "拜拜": 6.5, "晚安": 6.4, "早上好": 6.4, "晚上好": 6.4,
    "明天见": 6.6, "请问": 6.4,
    # 疑问
    "为什么": 7.0, "怎么办": 6.8, "什么时候": 7.0, "在哪里": 7.0,
    "这是什么": 6.5, "可以吗": 6.5, "行不行": 6.4, "是不是": 6.8,
    "有没有": 6.7, "好不好": 6.4, "对不对": 6.3, "可不可以": 6.2, "能不能": 6.4,
    # 时间/数量
    "今天": 6.6, "明天": 6.5, "昨天": 6.4, "现在": 6.6, "时间": 6.3,
    "多少": 6.2, "多少钱": 7.0, "几点": 6.2, "几号": 6.1, "星期几": 6.6,
    "天气": 6.2, "北京": 6.2, "上海": 6.2, "深圳": 6.0,
    # 设备/作品
    "北京师范大学": 7.6, "橘真琴": 7.6, "立创实战派": 7.4, "语音中枢": 6.6,
    "记事本": 6.4, "小游戏": 6.4, "贪吃蛇": 6.3, "深度思考": 6.4,
    "联网搜索": 6.4, "知识库": 6.2, "音色": 6.0, "桌面": 6.2,
    "音乐": 6.2, "相机": 6.2, "设置": 6.2,
    # 常用词
    "男朋友": 6.4, "女朋友": 6.4, "朋友": 6.4, "学生": 6.4,
    "大学": 6.4, "老师": 6.3, "学校": 6.3, "校长": 6.2,
}

_lock = threading.Lock()
_ready = False
_word_dict = None
_char_dict = None      # pinyin -> [(单字, 分数)]，覆盖全部 CJK 单字
_common_chars = None
_max_key = 0


def _cache_path():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(root, "data", "pinyin_dict.pkl")


def _jieba_dict_path():
    try:
        import jieba
        return os.path.join(os.path.dirname(jieba.__file__), "dict.txt")
    except Exception:
        return None


def _jieba_entries(cc):
    """读取 jieba 词典，返回 [(词, 频次)] 和总频次。"""
    path = _jieba_dict_path()
    if not path or not os.path.exists(path):
        return [], 0
    items = []
    total = 0
    with open(path, encoding="utf-8") as f:
        for line in f:
            parts = line.split()
            if len(parts) < 2:
                continue
            w = parts[0]
            if not re.fullmatch(r"[一-鿿]+", w):
                continue
            try:
                freq = int(parts[1])
            except ValueError:
                continue
            if freq <= 0:
                continue
            if cc:
                w = cc.convert(w)
            items.append((w, freq))
            total += freq
    items.sort(key=lambda x: x[1], reverse=True)
    return items[:_JIEBA_LIMIT], total


def _build():
    from wordfreq import top_n_list, zipf_frequency
    from pypinyin import lazy_pinyin, Style

    try:
        from opencc import OpenCC
        cc = OpenCC("t2s")
    except Exception:
        cc = None

    wordfreq_words = top_n_list("zh", _WORD_COUNT)
    jieba_words, jieba_total = _jieba_entries(cc)
    print("[pinyin] sources: wordfreq %d, jieba %d (%.1fM tokens)" % (
        len(wordfreq_words), len(jieba_words), jieba_total / 1e6))

    wd = {}
    seen = set()

    def add(word, score):
        if not word or not re.fullmatch(r"[一-鿿]+", word):
            return
        # 个别异体字 OpenCC 不转，手动归一到简体
        word = word.replace("妳", "你").replace("祂", "他").replace("牠", "它")
        if word in seen:
            return
        seen.add(word)
        try:
            py = "".join(lazy_pinyin(word, style=Style.NORMAL, errors="ignore"))
        except Exception:
            return
        if not py or not re.fullmatch(r"[a-z]+", py):
            return
        wd.setdefault(py, []).append((word, max(score, _EXTRA.get(word, 0.0))))

    for w in wordfreq_words:
        if not re.fullmatch(r"[一-鿿]+", w):
            continue
        sw = cc.convert(w) if cc else w
        add(sw, zipf_frequency(sw, "zh"))

    log_total = math.log10(jieba_total) if jieba_total > 0 else 9.0
    for w, freq in jieba_words:
        add(w, math.log10(freq) - log_total + 9.0)

    for w in _EXTRA:
        if _EXTRA[w] > 0:
            add(w, _EXTRA[w])

    # 全汉字单字表：每个拼音音节都能给出所有可能的单字，供候选翻页
    char_dict = {}
    for cp in range(0x4E00, 0xA000):
        ch = chr(cp)
        try:
            py = "".join(lazy_pinyin(ch, style=Style.NORMAL, errors="ignore"))
        except Exception:
            continue
        if not py or not re.fullmatch(r"[a-z]+", py):
            continue
        sch = cc.convert(ch) if cc else ch
        if not sch or not re.fullmatch(r"[一-鿿]", sch):
            continue
        score = zipf_frequency(sch, "zh")
        char_dict.setdefault(py, []).append((sch, max(score, 0.8)))

    for k in wd:
        wd[k].sort(key=lambda x: x[1], reverse=True)
        del wd[k][32:]
    for k, lst in char_dict.items():
        # 去重并按字频排序
        seen_ch = set()
        uniq = []
        for ch, sc in sorted(lst, key=lambda x: x[1], reverse=True):
            if ch in seen_ch:
                continue
            seen_ch.add(ch)
            uniq.append((ch, sc))
        char_dict[k] = uniq

    # 常用汉字集合：按单字词频过滤备选里的生僻字（保留首候选）
    chars = set()
    for vals in wd.values():
        for w, _ in vals:
            chars.update(w)
    common = {ch for ch in chars if zipf_frequency(ch, "zh") >= 3.2}
    for w in _EXTRA:
        if _EXTRA[w] > 0:
            common.update(w)

    return wd, char_dict, common


def _load_or_build():
    global _word_dict, _char_dict, _common_chars, _max_key
    path = _cache_path()
    data = None
    try:
        with open(path, "rb") as f:
            data = pickle.load(f)
        if not isinstance(data, dict) or data.get("version") != _CACHE_VERSION:
            data = None
    except Exception:
        data = None

    if data is None:
        t0 = time.time()
        wd, char_dict, common = _build()
        print("[pinyin] dictionary built: %d keys in %.1fs" % (len(wd), time.time() - t0))
        data = {
            "version": _CACHE_VERSION,
            "word_dict": wd,
            "char_dict": char_dict,
            "common_chars": common,
            "max_key": max((len(k) for k in wd), default=1),
        }
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            tmp = path + ".tmp"
            with open(tmp, "wb") as f:
                pickle.dump(data, f, protocol=4)
            os.replace(tmp, path)
        except Exception as e:
            print("[pinyin] cache write failed:", e)

    _word_dict = data["word_dict"]
    _char_dict = data.get("char_dict") or {}
    _common_chars = data.get("common_chars") or set()
    _max_key = int(data.get("max_key") or 1)


def _ensure_ready():
    global _ready
    if _ready:
        return True
    with _lock:
        if _ready:
            return True
        try:
            _load_or_build()
            _ready = True
        except Exception as e:
            print("[pinyin] init failed:", e)
            _ready = False
    return _ready


def warmup():
    if _ensure_ready():
        print("[pinyin] input method ready: %d pinyin keys" % len(_word_dict))


def normalize(text: str) -> str:
    s = (text or "").lower().replace("ü", "v")
    s = re.sub(r"[^a-z]", "", s)
    return s[:_MAX_INPUT]


def candidates(text: str, limit: int = 16, max_chars: int = 80, offset: int = 0):
    """返回按概率排序的中文候选，从 offset 开始，最多 limit 条。"""
    if not _ensure_ready():
        return []
    s = normalize(text)
    if not s:
        return []
    offset = max(0, int(offset or 0))
    want = offset + max(1, int(limit or 1))

    n = len(s)
    beams = [dict() for _ in range(n + 1)]
    beams[0][""] = 0.0

    for i in range(n):
        cur = beams[i]
        if not cur:
            continue
        max_l = min(_max_key, n - i)
        for l in range(1, max_l + 1):
            key = s[i:i + l]
            entries = _word_dict.get(key) or ()
            chars = _char_dict.get(key) if _char_dict else None
            if chars:
                entries = list(entries) + chars
            if not entries:
                continue
            nxt = beams[i + l]
            for word, z in entries:
                add = z - _PENALTY
                for txt, sc in cur.items():
                    val = sc + add
                    t = txt + word
                    if nxt.get(t, -1e18) < val:
                        nxt[t] = val
        # i+1 的所有入边都来自 i，处理完 i 即可裁剪
        if len(beams[i + 1]) > _BEAM:
            beams[i + 1] = dict(sorted(beams[i + 1].items(),
                                       key=lambda kv: kv[1], reverse=True)[:_BEAM])

    ranked = sorted(beams[n].items(), key=lambda kv: kv[1], reverse=True)
    out = []
    for idx, (w, _) in enumerate(ranked):
        if not w:
            continue
        if len(w) > max_chars:
            w = w[:max_chars]
        if w not in out:
            out.append(w)
        if len(out) >= want:
            break
    return out[offset:offset + limit]
