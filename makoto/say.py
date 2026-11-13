# -*- coding: utf-8 -*-
"""
橘真琴 (Makoto Tachibana) GPT-SoVITS 日语语音合成 CLI

用法:
    runtime\\python.exe say.py "おはよう、春ちゃん。今日もいい天気だね。"
    runtime\\python.exe say.py "本文" -o out.wav --speed 1.1

权重、参考音频、标注全部来自本仓库（makoto 根目录），框架放在 ./GPT-SoVITS。
"""
import argparse
import os
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
FW = os.path.join(ROOT, "GPT-SoVITS")

DEFAULT_REF = os.path.join(ROOT, "output", "denoise_opt",
                           "vocals.wav_0000040640_0000210560.wav")
NAME2TEXT = os.path.join(ROOT, "logs", "makoto", "2-name2text.txt")
ASR_LIST = os.path.join(ROOT, "output", "asr_opt", "denoise_opt.list")


def lookup_prompt_text(ref_path):
    """
    取参考音频对应的**日文原文**作为提示文本。

    注意：不要填 2-name2text.txt 第 2 列的音素串。框架会在推理时把 prompt_text
    再喂一遍 pyopenjtalk，音素串会被当成罗马字重新解析、膨胀成上百个乱码音素，
    模型把这些乱码念出来会凭空多出约 10 秒音频（实测目标文本只写「あ」也照样 10 秒）。
    填日文原文时，G2P 结果与训练格式几乎一致。
    """
    ref_name = os.path.basename(ref_path)

    # 首选 ASR 列表里的日文原文（带标点，最干净）
    if os.path.exists(ASR_LIST):
        with open(ASR_LIST, "r", encoding="utf-8") as f:
            for line in f:
                parts = line.strip().split("|")
                if len(parts) >= 4 and os.path.basename(parts[0]) == ref_name:
                    if parts[3].strip():
                        return parts[3].strip()

    # 退路：2-name2text.txt 第 4 列
    with open(NAME2TEXT, "r", encoding="utf-8") as f:
        for line in f:
            parts = line.strip().split("\t")
            if len(parts) >= 4 and parts[0] == ref_name and parts[3].strip():
                return parts[3].strip()
    return ""


def main():
    ap = argparse.ArgumentParser(description="橘真琴 TTS")
    ap.add_argument("text", help="要合成的日文文本")
    ap.add_argument("-o", "--out", default="makoto_output.wav", help="输出 wav 路径")
    ap.add_argument("-r", "--ref", default=DEFAULT_REF, help="参考音频路径")
    ap.add_argument("--gpt", default=os.path.join(ROOT, "GPT_weights_v2", "makoto-e15.ckpt"))
    ap.add_argument("--sovits", default=os.path.join(ROOT, "SoVITS_weights_v2", "makoto_e8_s200.pth"))
    ap.add_argument("--device", default="cuda", choices=["cuda", "cpu"])
    ap.add_argument("--lang", default="ja", choices=["ja", "zh", "en", "ko", "yue", "auto"],
                    help="目标文本语种（中文文本要用 --lang zh，否则日语G2P读不出汉字）")
    ap.add_argument("--top-k", type=int, default=15)
    ap.add_argument("--temperature", type=float, default=1.0)
    ap.add_argument("--speed", type=float, default=1.0)
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    for p, what in ((args.gpt, "GPT 权重"), (args.sovits, "SoVITS 权重"),
                    (args.ref, "参考音频"), (NAME2TEXT, "标注文件")):
        if not os.path.exists(p):
            sys.exit("[错误] %s 不存在: %s" % (what, p))

    prompt_text = lookup_prompt_text(args.ref)
    if not prompt_text:
        sys.exit("[错误] 未在 %s 中找到参考音频 %s 的标注"
                 % (NAME2TEXT, os.path.basename(args.ref)))

    # 框架内部用相对路径找 pretrained_models，必须先切到框架目录
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
        "t2s_weights_path": args.gpt,
        "vits_weights_path": args.sovits,
        "version": "v2",
        "device": args.device,
        "is_half": args.device == "cuda",
    })

    print("[1/2] 加载模型 (%s, %s) ..." % (args.device, "FP16" if args.device == "cuda" else "FP32"))
    t0 = time.time()
    tts = TTS(TTS_Config(cfg))
    print("      完成，用时 %.1fs" % (time.time() - t0))

    inputs = {
        "text": args.text,
        "text_lang": args.lang,
        "ref_audio_path": args.ref,
        "prompt_text": prompt_text,
        "prompt_lang": "ja",
        "top_k": args.top_k,
        "temperature": args.temperature,
        "speed_factor": args.speed,
        "seed": args.seed,
    }

    print("[2/2] 合成中 ...")
    t0 = time.time()
    sr, audio = None, None
    for chunk in tts.run(inputs):          # 生成器，最后一个 yield 是完整音频
        if isinstance(chunk, tuple) and len(chunk) == 2:
            sr, audio = chunk
    if audio is None:
        sys.exit("[错误] 推理未返回音频")

    out = os.path.abspath(args.out)
    sf.write(out, audio, sr)
    el = time.time() - t0
    dur = len(audio) / sr
    print("[完成] %.2fs 音频，耗时 %.2fs (RTF %.2f)" % (dur, el, el / dur))
    print("       输出: %s" % out)


if __name__ == "__main__":
    main()
