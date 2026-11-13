# -*- coding: utf-8 -*-
"""
GPT-SoVITS 橘真琴 端到端推理自检脚本

用 makoto 根目录里的权重 / 参考音频 / 标注，跑两句日语，结果写到 selftest\\。

运行:
    C:\\Users\\1\\Desktop\\makoto\\GPT-SoVITS\\runtime\\python.exe -X utf8 selftest\\infer_test.py
"""
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "GPT-SoVITS")

REF = os.path.join(ROOT, "output", "denoise_opt",
                   "vocals.wav_0000040640_0000210560.wav")
NAME2TEXT = os.path.join(ROOT, "logs", "makoto", "2-name2text.txt")
OUT_DIR = os.path.join(ROOT, "selftest")

CASES = [
    "おはよう、春ちゃん。今日もいい天気だね。",
    "僕は橘真琴。水泳部の部長をやってるんだ。",
]


def lookup_prompt_text(ref_path):
    ref_name = os.path.basename(ref_path)
    with open(NAME2TEXT, "r", encoding="utf-8") as f:
        for line in f:
            parts = line.strip().split("\t")
            if len(parts) >= 2 and parts[0] == ref_name:
                return parts[1]
    return ""


def main():
    for p, what in ((REF, "参考音频"), (NAME2TEXT, "标注文件"),
                    (os.path.join(ROOT, "GPT_weights_v2", "makoto-e15.ckpt"), "GPT 权重"),
                    (os.path.join(ROOT, "SoVITS_weights_v2", "makoto_e8_s200.pth"), "SoVITS 权重")):
        if not os.path.exists(p):
            sys.exit("[错误] %s 不存在: %s" % (what, p))

    prompt_text = lookup_prompt_text(REF)
    assert prompt_text, "未找到参考音频标注"

    os.chdir(FW)
    sys.path.insert(0, FW)
    sys.path.insert(0, os.path.join(FW, "GPT_SoVITS"))

    import yaml
    import soundfile as sf
    from GPT_SoVITS.TTS_infer_pack.TTS import TTS, TTS_Config

    with open(os.path.join(FW, "GPT_SoVITS", "configs", "tts_infer.yaml"),
              "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    custom = cfg["custom"]
    custom.update({
        "t2s_weights_path": os.path.join(ROOT, "GPT_weights_v2", "makoto-e15.ckpt"),
        "vits_weights_path": os.path.join(ROOT, "SoVITS_weights_v2", "makoto_e8_s200.pth"),
        "version": "v2",
        "device": "cuda",
        "is_half": True,
    })

    t0 = time.time()
    tts = TTS(TTS_Config(cfg))
    print("[OK] 模型加载 %.1fs" % (time.time() - t0), flush=True)
    print("[OK] 参考标注:", prompt_text[:60], "...", flush=True)

    for i, text in enumerate(CASES, 1):
        inputs = {
            "text": text,
            "text_lang": "ja",
            "ref_audio_path": REF,
            "prompt_text": prompt_text,
            "prompt_lang": "ja",
            "top_k": 15,
            "temperature": 1.0,
            "seed": 42,
        }
        t0 = time.time()
        sr, audio = None, None
        for chunk in tts.run(inputs):
            if isinstance(chunk, tuple) and len(chunk) == 2:
                sr, audio = chunk
        assert audio is not None, "推理未返回音频"
        el = time.time() - t0
        dur = len(audio) / sr
        path = os.path.join(OUT_DIR, "infer_test_%d.wav" % i)
        sf.write(path, audio, sr)
        print("[OK] %d) %s -> %.2fs 音频, 耗时 %.2fs, RTF %.2f"
              % (i, text, dur, el, el / dur), flush=True)

    print("[DONE] 全部通过", flush=True)


if __name__ == "__main__":
    main()
