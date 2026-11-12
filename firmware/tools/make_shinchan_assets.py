# -*- coding: utf-8 -*-
"""从真实蜡笔小新图片生成记事本皮肤素材：

- shinchan_icon.bin   28x28  标题栏头像（头部，深色底）
- shinchan_space.bin  28x28  空格键小新头像（黄底）
- shinchan_kb.bin     316x126 键盘背景（春日部防卫队 5 人 + 全家福 5 人）

源图：
- tools/shinchan_src.png         小新全身（头像/空格键）
- tools/shinchan_friends_src.jpg 春日部防卫队五人
- tools/shinchan_family_src.png  全家福抠图
"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN = os.path.join(HERE, "..", "main")
SRC = os.path.join(HERE, "shinchan_src.png")
FRIENDS = os.path.join(HERE, "shinchan_friends_src.jpg")
FAMILY = os.path.join(HERE, "shinchan_family_src.png")
W, H = 316, 126

im = Image.open(SRC).convert("RGBA")
print("source", im.size)

# 头部方裁（脸 + 一点红衣服）
head = im.crop((75, 40, 215, 180))
print("head", head.size)


def write_bin(img, path):
    px = img.convert("RGB").load()
    with open(path, "wb") as f:
        for y in range(img.height):
            for x in range(img.width):
                r, g, b = px[x, y]
                v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                f.write(bytes((v >> 8, v & 0xFF)))
    print("bin", path, os.path.getsize(path))


def write_bin_alpha(img, path):
    """RGB565 + 1 字节 alpha，供 LV_IMG_CF_TRUE_COLOR_ALPHA 使用。"""
    px = img.load()
    with open(path, "wb") as f:
        for y in range(img.height):
            for x in range(img.width):
                r, g, b, a = px[x, y]
                v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                f.write(bytes((v >> 8, v & 0xFF, a)))
    print("bin", path, os.path.getsize(path))


# 1) 标题栏头像：深色底
av = Image.new("RGBA", head.size, (0x14, 0x26, 0x3A, 255))
av.alpha_composite(head)
av = av.resize((28, 28), Image.LANCZOS)
av.save(os.path.join(MAIN, "shinchan_icon_preview.png"))
write_bin(av, os.path.join(MAIN, "shinchan_icon.bin"))

# 2) 空格键：透明底全身小新，居中在 28x28 里
body = im.crop(im.getbbox())
body.thumbnail((26, 26), Image.LANCZOS)
sp = Image.new("RGBA", (28, 28), (0, 0, 0, 0))
sp.alpha_composite(body, ((28 - body.width) // 2, (28 - body.height) // 2))
# 预览：合成到空格键的黄色上
preview = Image.new("RGBA", (28, 28), (0xFF, 0xD8, 0x4D, 255))
preview.alpha_composite(sp)
preview.save(os.path.join(MAIN, "shinchan_space_preview.png"))
write_bin_alpha(sp, os.path.join(MAIN, "shinchan_space.bin"))

# 3) 键盘背景：春日部防卫队五人 + 全家福五人
friends = Image.open(FRIENDS).convert("RGB")
scale = W / float(friends.width)
friends = friends.resize((W, int(friends.height * scale + 0.5)), Image.LANCZOS)
top = max(0, min(friends.height - H, 10))
base = friends.crop((0, top, W, top + H)).convert("RGBA")

fam = Image.open(FAMILY).convert("RGBA")
fam = fam.crop((0, 55, 282, 195))
bb = fam.getbbox()
if bb:
    fam = fam.crop(bb)
fam.thumbnail((185, 78), Image.LANCZOS)
base.alpha_composite(fam, ((W - fam.width) // 2, H - fam.height - 2))

# 圆角（和键盘 radius 10 对齐），角外填页面底色
mask = Image.new("L", (W, H), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, W - 1, H - 1), radius=10, fill=255)
out = Image.new("RGBA", (W, H), (0x0B, 0x10, 0x20, 255))
out.paste(base, (0, 0), mask)
preview = out.convert("RGB")
preview.save(os.path.join(MAIN, "shinchan_kb_preview.png"))
write_bin(out, os.path.join(MAIN, "shinchan_kb.bin"))
print("done")
