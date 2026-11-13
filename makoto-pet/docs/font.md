# 设备字体 `font_cn_16`

## 为什么换

LVGL 自带的 `lv_font_simsun_16_cjk` 只有约 1000 个常用字，实测缺：

```
橘 琴 桌 游 戏 记 设 乐 按 说 话 聆 听 开 贪 蛇 网 启 备 亮
```

以及日文假名/大量日文汉字，所以设备屏幕会出现方框。

## 生成方法（已生成好，一般不用重做）

1. 从 Windows 微软雅黑提取单字体 TTF（`.ttc` 不能直接喂给 lv_font_conv）：

```python
from fontTools.ttLib import TTCollection
TTCollection(r"C:\Windows\Fonts\msyh.ttc").fonts[0].save("msyh.ttf")
```

2. 生成 LVGL 字体（16px、2bpp、全 CJK + 假名）：

```powershell
npx --yes lv_font_conv@1.5.3 `
  --font msyh.ttf --size 16 --bpp 2 --format lvgl --no-compress `
  --range 32-127 `
  --range 0x3000-0x30FF `
  --range 0x4E00-0x9FFF `
  --range 0xFF00-0xFFEF `
  -o font_cn_16.c
```

3. 把 `font_cn_16.c` 放到 `firmware/main/`，加入 `main/CMakeLists.txt` 的
   `SRCS`；UI 里用：

```c
LV_FONT_DECLARE(font_cn_16);
static const lv_font_t *FONT_CN = &font_cn_16;
```

4. `sdkconfig.defaults` 必须包含：

```
CONFIG_LV_FONT_FMT_TXT_LARGE=y
```

因为 2bpp 全 CJK 的位图超过 1MB，默认 20 位 `bitmap_index` 会溢出
（编译报 `unsigned conversion ... changes value`）。

## 待机时钟艺术字（Broadway 48px）

待机页时间用的单独字体，只包含 `- . / 0-9 :`：

```powershell
npx --yes lv_font_conv@1.5.3 `
  --font "C:\Windows\Fonts\BROADW.TTF" --size 48 --bpp 4 --format lvgl --no-compress `
  --range 0x2D-0x3A -o clock_font.c
```

生成的 `clock_font.c` 里如果包含：

```c
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif
```

要把整段替换成 `#include "lvgl.h"`（本工程的 LVGL 组件目录不叫 `lvgl/`）。
然后把 `clock_font.c` 加进 `main/CMakeLists.txt` 的 `SRCS`。

## 体积与清晰度

- 当前 2bpp：源码约 11MB，编译进固件约 1.1MB，app 约 2.8MB，4MB 分区够用。
- 想更清晰：把 `--bpp 2` 改 `--bpp 4`，字体约 2.2MB，app 会超过 4MB，
  需要把 `partitions.csv` 的 `factory` 改成 6M（NVS 不变，配网信息不丢）。
- 想更小：`--bpp 1`，或减少 `--range`（只保留常用字和假名）。
