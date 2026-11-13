# -*- coding: utf-8 -*-
"""
终极判定：目标文本用一个音节「あ」。
- 若输出仍在 8 秒上下 -> 多出来的时长与目标无关（在念提示音素）-> 音素串填法有问题
- 若输出只有 1~2 秒        -> 时长由目标驱动 -> 音素串填法正常
"""
import os
import numpy as np
import soundfile as sf
from gradio_client import Client

ROOT = r"C:\Users\1\Desktop\makoto"
REF = os.path.join(ROOT, "output", "denoise_opt",
                   "vocals.wav_0000040640_0000210560.wav")
NAME2TEXT = os.path.join(ROOT, "logs", "makoto", "2-name2text.txt")


def phonemes_of(ref_path):
    with open(NAME2TEXT, "r", encoding="utf-8") as f:
        for line in f:
            p = line.strip().split("\t")
            if len(p) >= 2 and p[0] == os.path.basename(ref_path):
                return p[1]
    return ""


PHON = phonemes_of(REF)
PLAIN = "おはよう春ちゃん。ごめんごめん春。入学式遅れるよ。"


def voiced_dur(path):
    d, sr = sf.read(path)
    if d.ndim > 1:
        d = d.mean(axis=1)
    w = sr // 100
    rms = np.sqrt(np.convolve(d ** 2, np.ones(w) / w, "same"))
    idx = np.where(rms > rms.max() * 0.03)[0]
    return len(d) / sr, ((idx[-1] - idx[0]) / sr if len(idx) else 0)


c = Client("http://127.0.0.1:9872/", verbose=False)
print("%-28s %-16s %8s %8s" % ("参考文本填法", "目标文本", "总长", "人声"))
print("-" * 64)
for plabel, ptext in (("音素串", PHON), ("日文原文", PLAIN)):
    for tlabel, text in (("「あ」(1音节)", "あ"), ("短句", "おはよう、春ちゃん。今日もいい天気だね。")):
        r = c.predict(REF, ptext, "日文", text, "日文",
                      "不切", 15, 1.0, 1.0, False, 1.0, False, [], 32, False, 0.3,
                      api_name="/get_tts_wav")
        src = r[0] if isinstance(r, (list, tuple)) else r
        src = src.get("value") if isinstance(src, dict) else src
        t, v = voiced_dur(src)
        print("%-28s %-16s %6.2fs %7.2fs" % (plabel, tlabel, t, v))
