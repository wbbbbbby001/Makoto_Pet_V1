# -*- coding: utf-8 -*-
r"""把图片转成固件用的 320x240 聊天背景 + 待机画面。

用法（在 makoto-pet 目录下）：
    server\.venv\Scripts\python.exe firmware\tools\make_makoto_bg.py [聊天图] [待机图]

- 只给一个参数：聊天和待机都用它；
- 给两个参数：第一张聊天背景，第二张待机画面；
- 不给参数：使用下面的默认路径。

输出（firmware/main/）：
    makoto_bg.bin         AI 聊天页背景（压暗 + 顶部渐变）
    makoto_bg_preview.png
    makoto_standby.bin    待机画面（只轻微压暗）
    makoto_standby_preview.png
生成后重新运行 firmware\build.ps1 编译烧录即可。

说明：
- 自动按高度铺满、水平居中裁切；
- LVGL 配置为 LV_COLOR_16_SWAP=1，因此按 RGB565 大端字节序输出。
"""
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "..", "main")
CHAT_DEFAULT = os.path.join(HERE, "assets", "03.png")
STANDBY_DEFAULT = os.path.join(HERE, "assets", "makoto壁纸.jpg")

W, H = 320, 240
chat_src = sys.argv[1] if len(sys.argv) > 1 else CHAT_DEFAULT
if len(sys.argv) > 2:
    standby_src = sys.argv[2]
elif len(sys.argv) > 1:
    standby_src = chat_src
else:
    standby_src = STANDBY_DEFAULT


def cover_crop(im):
    w, h = im.size
    scale = H / float(h)
    nw = int(w * scale + 0.5)
    im = im.resize((nw, H), Image.LANCZOS)
    left = max(0, (nw - W) // 2)
    return im.crop((left, 0, left + W, H))


def write_bin(im, path):
    px = im.load()
    with open(path, "wb") as fp:
        for y in range(H):
            for x in range(W):
                r, g, b = px[x, y]
                v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                fp.write(bytes((v >> 8, v & 0xFF)))
    print("bin    :", path, os.path.getsize(path), "bytes")


im = cover_crop(Image.open(chat_src).convert("RGB"))
dark = Image.new("RGB", (W, H), (11, 16, 32))

# 1) 聊天页背景：压暗 32% + 顶部渐变（标题可读）
chat = Image.blend(im, dark, 0.32)
px = chat.load()
for y in range(70):
    f = 0.55 + 0.45 * (y / 69.0)
    for x in range(W):
        r, g, b = px[x, y]
        px[x, y] = (int(r * f), int(g * f), int(b * f))
for y in range(H - 30, H):
    f = 0.85 + 0.15 * ((H - 1 - y) / 29.0)
    for x in range(W):
        r, g, b = px[x, y]
        px[x, y] = (int(r * f), int(g * f), int(b * f))
chat_png = os.path.join(MAIN, "makoto_bg_preview.png")
chat.save(chat_png)
print("chat src:", chat_src)
print("preview:", chat_png)
write_bin(chat, os.path.join(MAIN, "makoto_bg.bin"))

# 2) 待机画面：只轻微压暗，保留原图观感
st = cover_crop(Image.open(standby_src).convert("RGB"))
standby = Image.blend(st, dark, 0.08)
px = standby.load()
# 顶部渐变，让白色艺术字时钟更清楚（不是矩形阴影）
for y in range(90):
    f = 0.78 + 0.22 * (y / 89.0)
    for x in range(W):
        r, g, b = px[x, y]
        px[x, y] = (int(r * f), int(g * f), int(b * f))
for y in range(H - 40, H):
    f = 0.88 + 0.12 * ((H - 1 - y) / 39.0)
    for x in range(W):
        r, g, b = px[x, y]
        px[x, y] = (int(r * f), int(g * f), int(b * f))
st_png = os.path.join(MAIN, "makoto_standby_preview.png")
standby.save(st_png)
print("standby src:", standby_src)
print("preview:", st_png)
write_bin(standby, os.path.join(MAIN, "makoto_standby.bin"))
