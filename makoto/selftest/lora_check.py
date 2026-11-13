# -*- coding: utf-8 -*-
"""按 README Q8 校验 v3 LoRA 权重（自定义 b'03' 头，必须用 load_sovits_new 加载）"""
import os
import sys

FW = r"C:\Users\1\Desktop\makoto\GPT-SoVITS"
os.chdir(FW)
sys.path.insert(0, os.path.join(FW, "GPT_SoVITS"))
from process_ckpt import load_sovits_new  # noqa: E402

BASE = r"C:\Users\1\Desktop\makoto\SoVITS_weights_v3"
for name in sorted(os.listdir(BASE)):
    if not name.endswith(".pth"):
        continue
    p = os.path.join(BASE, name)
    with open(p, "rb") as f:
        head = f.read(2)
    ckpt = load_sovits_new(p)
    w = ckpt["weight"]
    print("%-28s 头=%r  键数=%d  lora_rank=%s  info=%s"
          % (name, head, len(w), ckpt.get("lora_rank", "-"), ckpt.get("info", "-")))
