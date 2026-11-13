# -*- coding: utf-8 -*-
"""冒烟测试：直接调用 GPT-SoVITS 合成一句，验证音色权重和参考音频。

用法（先启动 api_v2.py）：
    python -X utf8 tools/smoke_tts.py "你好，我是橘真琴。"
"""
import asyncio
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.config import CFG, ROOT                      # noqa: E402
from app.voices import VoiceStore                     # noqa: E402
from app.tts import TTSClient, wav_bytes              # noqa: E402


async def main():
    text = sys.argv[1] if len(sys.argv) > 1 else "你好，我是橘真琴。今天也一起加油吧。"
    data_dir = os.path.join(ROOT, "data")
    voices = VoiceStore(os.path.join(data_dir, "voices.json"),
                        os.path.join(data_dir, "refs"), CFG)
    tts = TTSClient(CFG, voices)
    voice = voices.get()
    print("音色:", voice["name"], "| 参考音频:", voice["ref_audio"])
    print("TTS :", tts.base, "| 在线:", await tts.health())
    sr, pcm = await tts.synth(text, voice)
    out = os.path.join(data_dir, "smoke_tts.wav")
    with open(out, "wb") as f:
        f.write(wav_bytes(sr, pcm))
    dur = len(pcm) / 2 / sr
    print("完成: %d Hz, %.2f 秒 -> %s" % (sr, dur, out))


if __name__ == "__main__":
    asyncio.run(main())
