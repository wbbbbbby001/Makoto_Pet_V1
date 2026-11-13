# -*- coding: utf-8 -*-
"""
UI 可用性验证：直接调用推理页 (127.0.0.1:9872) 的 Gradio 接口出音频。

等价于在浏览器里点「开始推理」——只是把点击换成 HTTP 调用。
"""
import os
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF = os.path.join(ROOT, "output", "denoise_opt",
                   "vocals.wav_0000040640_0000210560.wav")
NAME2TEXT = os.path.join(ROOT, "logs", "makoto", "2-name2text.txt")
OUT = os.path.join(ROOT, "生成音频", "ui_验证输出.wav")

MAIN, TTS = "http://127.0.0.1:9874/", "http://127.0.0.1:9872/"


def http_ok(url):
    try:
        with urllib.request.urlopen(url, timeout=20) as r:
            body = r.read(4000).decode("utf-8", "ignore")
            return r.status, len(body), ("gradio" in body.lower() or "<html" in body.lower())
    except Exception as e:
        return "ERR", str(e)[:80], False


ASR_LIST = os.path.join(ROOT, "output", "asr_opt", "denoise_opt.list")


def prompt_of(ref_path):
    """取参考音频的**日文原文**（不是音素串，见 使用说明.md 坑 2）。"""
    name = os.path.basename(ref_path)
    if os.path.exists(ASR_LIST):
        with open(ASR_LIST, "r", encoding="utf-8") as f:
            for line in f:
                p = line.strip().split("|")
                if len(p) >= 4 and os.path.basename(p[0]) == name and p[3].strip():
                    return p[3].strip()
    with open(NAME2TEXT, "r", encoding="utf-8") as f:
        for line in f:
            p = line.strip().split("\t")
            if len(p) >= 4 and p[0] == name and p[3].strip():
                return p[3].strip()
    return ""


print("=== 1) HTTP 页面可达性 ===")
for label, url in (("主界面 :9874", MAIN), ("推理页 :9872", TTS)):
    st, size, looks_html = http_ok(url)
    print("  %-14s status=%s  body=%s  是网页=%s" % (label, st, size, looks_html))

print("=== 2) 调用推理页接口 /get_tts_wav ===")
try:
    from gradio_client import Client
    try:
        from gradio_client import handle_file as _hf
    except ImportError:
        _hf = None

    c = Client(TTS, verbose=False)
    ref_arg = _hf(REF) if _hf else REF

    text = "チェン・ワンアルさん、こんにちは。"
    print("  文本:", text)
    res = c.predict(
        ref_arg,                 # 参考音频
        prompt_of(REF),          # 参考音频的文本（音素）
        "日文",                  # 参考音频的语种
        text,                    # 需要合成的文本
        "日文",                  # 需要合成的语种
        "不切",                  # 怎么切
        15,                      # top_k      ← README 验证过的组合
        1.0,                     # top_p
        1.0,                     # temperature
        False,                   # 无参考文本模式
        1.0,                     # 语速
        False,                   # 冻结
        [],                      # 多参考音频
        32,                      # 采样步数
        False,                   # 音质提升
        0.3,                     # 分段间隔
        api_name="/get_tts_wav",
    )

    src = res[0] if isinstance(res, (list, tuple)) else res
    src = src.get("value") if isinstance(src, dict) else src
    if src and os.path.exists(str(src)):
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        with open(src, "rb") as fi, open(OUT, "wb") as fo:
            fo.write(fi.read())
        import soundfile as sf
        d, sr = sf.read(OUT)
        print("  [PASS] UI 后端出音频: %.2fs @ %dHz -> %s" % (len(d) / sr, sr, OUT))
    else:
        print("  [WARN] 接口返回了但没拿到音频文件:", res)
except Exception as e:
    import traceback
    traceback.print_exc()
    print("  [FAIL]", type(e).__name__, str(e)[:300])
    sys.exit(1)
