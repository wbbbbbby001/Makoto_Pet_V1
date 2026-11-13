# -*- coding: utf-8 -*-
"""生成桌面 / Launcher 的 48x48 图标（透明底，只有图形本身）。

之前图标素材把「彩色圆角方块底板」烘焙进了 bin，桌面上看起来每个 App
都顶着一个彩色方框。这里改成只保留图形和透明背景，图形浮在壁纸上：
- AI 助手：从 shinchan_src.png 抠出的小新头部；
- 其余：直接渲染 Fluent 彩色 emoji 字形（音乐音符改成亮青色）。

输出（firmware/main/，RGB565 + 1 字节 alpha，LV_IMG_CF_TRUE_COLOR_ALPHA）：
- icon_ai.bin
- icon_games.bin
- icon_note.bin
- icon_settings.bin
- icon_music.bin
- icon_camera.bin

用法（在 makoto-pet 目录下）：
    server\\.venv\\Scripts\\python.exe firmware\\tools\\make_desktop_icons.py
"""
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "..", "main")
PREVIEW = os.path.join(MAIN, "_icon_preview")

ICON = 48          # ui_icons.c 里声明的尺寸，不要改
MARGIN = 1         # 图形四周留 1px，避免顶到图片边缘被裁
FONT_SIZE = 192    # 先大尺寸渲染，再缩到 48，边缘更平滑
CANVAS = 512
FONT = "C:/Windows/Fonts/seguiemj.ttf"

# AI 助手用小新头像（透明底抠图），其余用 Fluent emoji
SHINCHAN_SRC = os.path.join(HERE, "shinchan_src.png")
SHINCHAN_HEAD = (82, 33, 208, 158)   # 只取头部，去掉举起的手和身体
MUSIC_COLOR = (0x39, 0xD0, 0xFF)     # 亮青色，和 UI 的 C_CYAN 一致

# 和 desktop_ui.c 的 tiles[] 顺序一致
ICONS = {
    "icon_games": "\U0001F3AE",     # 🎮 游戏手柄
    "icon_note": "\U0001F4DD",      # 📝 记事本
    "icon_settings": "\u2699\uFE0F",  # ⚙️ 齿轮
    "icon_music": "\U0001F3B5",     # 🎵 音符（后面会改成亮青色）
    "icon_camera": "\U0001F4F7",    # 📷 相机
}


def fit_center(img):
    """裁掉透明空白，等比缩到 ICON 内并居中。"""
    img = img.crop(img.getbbox())
    img.thumbnail((ICON - 2 * MARGIN, ICON - 2 * MARGIN), Image.LANCZOS)
    out = Image.new("RGBA", (ICON, ICON), (0, 0, 0, 0))
    out.alpha_composite(img, ((ICON - img.width) // 2,
                              (ICON - img.height) // 2))
    return out


def render_shinchan():
    """从透明底 PNG 抠出小新头部，做成浮空图标。"""
    im = Image.open(SHINCHAN_SRC).convert("RGBA")
    return fit_center(im.crop(SHINCHAN_HEAD))


def recolor(img, rgb):
    """保留 alpha 形状，把图形整体换成指定颜色。"""
    out = Image.new("RGBA", img.size, rgb + (0,))
    out.putalpha(img.getchannel("A"))
    return out


def render_glyph(ch):
    """把 emoji 渲染成透明底、裁掉空白、居中放进 ICON x ICON 的 RGBA。"""
    font = ImageFont.truetype(FONT, FONT_SIZE)
    canvas = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    ImageDraw.Draw(canvas).text(
        (CANVAS // 2, CANVAS // 2), ch, font=font,
        embedded_color=True, anchor="mm")
    if not canvas.getbbox():
        raise RuntimeError("emoji 没有渲染出内容: %r" % ch)
    return fit_center(canvas)


def write_bin_alpha(img, path):
    """RGB565（高字节在前）+ 1 字节 alpha，供 LV_IMG_CF_TRUE_COLOR_ALPHA。"""
    px = img.load()
    with open(path, "wb") as f:
        for y in range(img.height):
            for x in range(img.width):
                r, g, b, a = px[x, y]
                v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                f.write(bytes((v >> 8, v & 0xFF, a)))


def main():
    os.makedirs(PREVIEW, exist_ok=True)

    im = render_shinchan()
    write_bin_alpha(im, os.path.join(MAIN, "icon_ai.bin"))
    im.save(os.path.join(PREVIEW, "icon_ai.png"))
    print("ok icon_ai (shinchan)")

    for name, ch in ICONS.items():
        im = render_glyph(ch)
        if name == "icon_music":
            im = recolor(im, MUSIC_COLOR)
        write_bin_alpha(im, os.path.join(MAIN, name + ".bin"))
        im.save(os.path.join(PREVIEW, name + ".png"))
        print("ok", name, im.size)
    print("done ->", MAIN)


if __name__ == "__main__":
    main()
