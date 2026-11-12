# -*- coding: utf-8 -*-
"""生成各页面各自的 320x240 背景（RGB565）。

- desktop_bg.bin  主桌面 Launcher        <- 01.jpg
- music_bg.bin    音乐播放器             <- 02.jpg
- ai_bg.bin       AI 助手 / 聊天页       <- 03.png

处理方式和原来的 makoto_bg 一致：等比铺满、居中裁切，再按页面需要压暗，
并在顶部/底部加渐变，保证白色文字和图标看得清。

用法（在 makoto-pet 目录下）：
    server\\.venv\\Scripts\\python.exe firmware\\tools\\make_app_bgs.py [01] [02] [03]

不传参数时使用下面的默认路径。生成后重新编译烧录。
"""
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "..", "main")

W, H = 320, 240
DARK = (11, 16, 32)

DEFAULTS = {
    "desktop": os.path.join(HERE, "assets", "01.jpg"),
    "music": os.path.join(HERE, "assets", "02.jpg"),
    "ai": os.path.join(HERE, "assets", "03.png"),
}

# 每个页面：压暗比例 + 顶部渐变高度 + 底部渐变高度
SETTINGS = {
    # shift_y > 0 = 画面整体往下移（裁切窗口上移），单位：像素
    "desktop": dict(src=0, dim=0.38, top=70, bottom=40, shift_y=10),
    "music": dict(src=1, dim=0.40, top=60, bottom=40),
    "ai": dict(src=2, dim=0.36, top=70, bottom=30),
}


def cover_crop(im, shift_y=0):
    """等比放大到刚好铺满 320x240，多余的横/竖居中裁掉。

    shift_y > 0：画面整体往下移（裁切窗口上移），最多到图片边缘。
    """
    w, h = im.size
    scale = max(W / float(w), H / float(h))
    nw, nh = int(w * scale + 0.5), int(h * scale + 0.5)
    im = im.resize((nw, nh), Image.LANCZOS)
    left = max(0, (nw - W) // 2)
    top = int(round((nh - H) / 2.0 - shift_y))
    top = max(0, min(nh - H, top))
    return im.crop((left, top, left + W, top + H))


def load_rgb(path, shift_y=0):
    im = Image.open(path)
    if im.mode in ("RGBA", "LA", "P"):
        im = im.convert("RGBA")
        base = Image.new("RGBA", im.size, DARK + (255,))
        base.alpha_composite(im)
        im = base
    return cover_crop(im.convert("RGB"), shift_y)


def shade(im, dim, top_h, bottom_h):
    out = Image.blend(im, Image.new("RGB", (W, H), DARK), dim)
    px = out.load()
    # 顶部渐变：让标题栏/时钟更清楚
    for y in range(top_h):
        f = 0.62 + 0.38 * (y / float(max(1, top_h - 1)))
        for x in range(W):
            r, g, b = px[x, y]
            px[x, y] = (int(r * f), int(g * f), int(b * f))
    # 底部渐变：让图标文字/按钮更清楚
    for y in range(H - bottom_h, H):
        f = 0.82 + 0.18 * ((H - 1 - y) / float(max(1, bottom_h - 1)))
        for x in range(W):
            r, g, b = px[x, y]
            px[x, y] = (int(r * f), int(g * f), int(b * f))
    return out


def write_bin(im, path):
    px = im.load()
    with open(path, "wb") as fp:
        for y in range(H):
            for x in range(W):
                r, g, b = px[x, y]
                v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                fp.write(bytes((v >> 8, v & 0xFF)))
    print("bin    :", path, os.path.getsize(path), "bytes")


def main():
    args = sys.argv[1:]
    srcs = {
        "desktop": args[0] if len(args) > 0 else DEFAULTS["desktop"],
        "music": args[1] if len(args) > 1 else DEFAULTS["music"],
        "ai": args[2] if len(args) > 2 else DEFAULTS["ai"],
    }
    for name, cfg in SETTINGS.items():
        src = srcs[name]
        im = load_rgb(src, cfg.get("shift_y", 0))
        im = shade(im, cfg["dim"], cfg["top"], cfg["bottom"])
        write_bin(im, os.path.join(MAIN, name + "_bg.bin"))
        preview = os.path.join(MAIN, name + "_bg_preview.png")
        im.save(preview)
        print("src    :", src)
        print("preview:", preview)
    print("done ->", MAIN)


if __name__ == "__main__":
    main()
