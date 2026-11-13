# -*- coding: utf-8 -*-
"""按 UI 方式生成「陈婉儿同学，你好」，并留一份踩坑对比样本。"""
import os
import shutil
import numpy as np
import soundfile as sf
from gradio_client import Client

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF = os.path.join(ROOT, "output", "denoise_opt",
                   "vocals.wav_0000040640_0000210560.wav")
OUTDIR = os.path.join(ROOT, "生成音频")
NAME2TEXT = os.path.join(ROOT, "logs", "makoto", "2-name2text.txt")


def phonemes_of(ref_path):
    name = os.path.basename(ref_path)
    with open(NAME2TEXT, "r", encoding="utf-8") as f:
        for line in f:
            p = line.strip().split("\t")
            if len(p) >= 2 and p[0] == name:
                return p[1]
    return ""


PHON = phonemes_of(REF)
PLAIN = "おはよう春ちゃん。ごめんごめん春。入学式遅れるよ。"   # 用户当时填的

# (输出名, 说明, 参考文本, 参考语种, 目标文本, 目标语种)
CASES = [
    ("chenwaner_UI踩坑对比.wav",  "你当时的设置", PLAIN, "中文", "陈婉儿同学，你好", "中文"),
    ("chenwaner_UI正确_中文文本.wav", "修正后(中文文本)", PHON, "日文", "陈婉儿同学，你好", "中文"),
    ("chenwaner_UI正确_日文文本.wav", "修正后(日文文本)", PHON, "日文", "チェン・ワンアルさん、こんにちは。", "日文"),
]

c = Client("http://127.0.0.1:9872/", verbose=False)
print("%-30s %8s %8s  %s" % ("输出文件", "时长", "人声", "判定"))
print("-" * 64)
for fname, desc, ptext, plang, text, tlang in CASES:
    res = c.predict(REF, ptext, plang, text, tlang,
                    "不切", 15, 1.0, 1.0, False, 1.0, False, [], 32, False, 0.3,
                    api_name="/get_tts_wav")
    src = res[0] if isinstance(res, (list, tuple)) else res
    src = src.get("value") if isinstance(src, dict) else src
    dst = os.path.join(OUTDIR, fname)
    shutil.copyfile(src, dst)
    d, sr = sf.read(dst)
    if d.ndim > 1:
        d = d.mean(axis=1)
    w = sr // 100
    rms = np.sqrt(np.convolve(d ** 2, np.ones(w) / w, "same"))
    idx = np.where(rms > rms.max() * 0.02)[0]
    voiced = (idx[-1] - idx[0]) / sr if len(idx) else 0
    print("%-30s %6.2fs %6.2fs  %s" % (fname, len(d) / sr, voiced,
                                       "❌ 失败" if len(d) / sr < 2.0 else "✅ 正常"))
