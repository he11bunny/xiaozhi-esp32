#!/usr/bin/env python3
"""Generate an embedded LVGL RGB565 image of the logo pumpkin face.

Pipeline:
  1. Enlarge the black eye sockets in the artwork (drawn over the original ones).
  2. Crop to the logo content bounding box + margin.
  3. Scale to screen width (240) and letterbox into 240x280 (black bars).
  4. Convert to RGB565 (little-endian) and emit logo_face.c / logo_face.h.

The white pupils are overlaid at runtime by FaceExpression.

Run from repo root:
  python3 scripts/gen_logo_face.py [path/to/pumpkin-face.png]
"""

import sys
from PIL import Image, ImageDraw

SRC = "../pumpkin-face.png"
OUT_C = "main/boards/lckfb/lichuang-c3-robot/logo_face.c"
OUT_H = "main/boards/lckfb/lichuang-c3-robot/logo_face.h"
PREVIEW = "/tmp/pumpkin_face_preview.png"

SCREEN_W = 240
SCREEN_H = 280

# Measured from the 1206x1066 source image.
CONTENT_BBOX = (52, 116, 1131, 1013)   # pumpkin incl. stem
LEFT_EYE = (397.5, 504.0, 135.5, 149.0)   # center x, y, radius x, radius y
RIGHT_EYE = (801.0, 504.0, 135.0, 149.0)
MARGIN = 12
TOP_PAD = 20  # 脸相对屏幕顶部的黑边，越小越靠上
PUPIL_RADIUS = 18  # 屏幕像素，运行时黄色眼珠半径
PUPIL_COLOR = (255, 193, 7)  # 金黄色，与 face_expression.h 的 pupil_color 保持一致

# 放大眼窝（屏幕像素）：眼心略向两侧外移，给更大的眼珠留出活动空间。
SOCKET_RADIUS = (38, 42)
SOCKET_SHIFT_X = 6
SOCKET_COLOR = (0, 0, 0)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else SRC
    img = Image.open(src).convert("RGB")

    # 1. Crop to content + margin, clamped to the source canvas.
    x0, y0, x1, y1 = CONTENT_BBOX
    x0 = max(0, x0 - MARGIN)
    y0 = max(0, y0 - MARGIN)
    x1 = min(img.width, x1 + MARGIN)
    y1 = min(img.height, y1 + MARGIN)
    crop_w, crop_h = x1 - x0, y1 - y0
    scale = SCREEN_W / crop_w

    # 2. Draw bigger black eye sockets over the original ones. Screen-space sizes are
    #    converted back to source pixels so the downscale still anti-aliases the edges.
    draw = ImageDraw.Draw(img)
    for eye, shift in ((LEFT_EYE, -SOCKET_SHIFT_X), (RIGHT_EYE, SOCKET_SHIFT_X)):
        cx = eye[0] + shift / scale
        cy = eye[1]
        rx = SOCKET_RADIUS[0] / scale
        ry = SOCKET_RADIUS[1] / scale
        draw.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=SOCKET_COLOR)

    # 3. Scale to screen width, letterbox vertically on black.
    crop = img.crop((x0, y0, x1, y1))
    scaled_w = SCREEN_W
    scaled_h = round(crop_h * scale)
    scaled = crop.resize((scaled_w, scaled_h), Image.LANCZOS)

    canvas = Image.new("RGB", (SCREEN_W, SCREEN_H), (0, 0, 0))
    offset_y = TOP_PAD
    canvas.paste(scaled, (0, offset_y))

    # 4. RGB565 little-endian.
    buf = bytearray()
    px = canvas.load()
    for y in range(SCREEN_H):
        for x in range(SCREEN_W):
            r, g, b = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            buf.append(v & 0xFF)
            buf.append((v >> 8) & 0xFF)

    # 5. Emit C source.
    lines = []
    for i in range(0, len(buf), 12):
        chunk = buf[i:i + 12]
        lines.append("    " + ", ".join("0x%02x" % b for b in chunk) + ",")

    c_src = (
        '#include "lvgl.h"\n'
        '\n'
        '#ifndef LV_ATTRIBUTE_MEM_ALIGN\n'
        '#define LV_ATTRIBUTE_MEM_ALIGN\n'
        '#endif\n'
        '\n'
        'const LV_ATTRIBUTE_MEM_ALIGN uint8_t logo_face_map[] = {\n'
        + "\n".join(lines) +
        "\n};\n"
        '\n'
        'const lv_image_dsc_t logo_face = {\n'
        '    .header.magic = LV_IMAGE_HEADER_MAGIC,\n'
        '    .header.cf = LV_COLOR_FORMAT_RGB565,\n'
        '    .header.w = %d,\n'
        '    .header.h = %d,\n'
        '    .data_size = sizeof(logo_face_map),\n'
        '    .data = logo_face_map,\n'
        '};\n' % (SCREEN_W, SCREEN_H)
    )

    h_src = (
        '#pragma once\n'
        '#include "lvgl.h"\n'
        '\n'
        'extern const lv_image_dsc_t logo_face;\n'
    )

    with open(OUT_C, "w") as f:
        f.write(c_src)
    with open(OUT_H, "w") as f:
        f.write(h_src)

    # 6. Report screen-space eye geometry for hardcoding into face_expression.h.
    def to_screen(cx, cy, rx, ry):
        sx = (cx - x0) * scale
        sy = (cy - y0) * scale + offset_y
        return round(sx), round(sy), round(rx * scale), round(ry * scale)

    def socket_screen(eye, shift):
        return to_screen(eye[0] + shift / scale, eye[1], SOCKET_RADIUS[0] / scale,
                         SOCKET_RADIUS[1] / scale)

    lx, ly, lrx, lry = socket_screen(LEFT_EYE, -SOCKET_SHIFT_X)
    rcx, rcy, rrx, rry = socket_screen(RIGHT_EYE, SOCKET_SHIFT_X)

    # 7. Preview with the runtime yellow pupils.
    preview = canvas.copy()
    pdraw = ImageDraw.Draw(preview)
    for cx, cy in ((lx, ly), (rcx, rcy)):
        pdraw.ellipse([cx - PUPIL_RADIUS, cy - PUPIL_RADIUS,
                       cx + PUPIL_RADIUS, cy + PUPIL_RADIUS], fill=PUPIL_COLOR)
    preview.save(PREVIEW)

    print(f"crop: {x0},{y0} {x1},{y1} -> {crop_w}x{crop_h} scale={scale:.4f}")
    print(f"scaled: {scaled_w}x{scaled_h} offset_y={offset_y}")
    print(f"LEFT_EYE  screen: center=({lx},{ly}) socket=({lrx},{lry})")
    print(f"RIGHT_EYE screen: center=({rcx},{rcy}) socket=({rrx},{rry})")
    print(f"wrote {OUT_C} ({len(buf)} bytes) and {OUT_H}")
    print(f"preview: {PREVIEW}")


if __name__ == "__main__":
    main()
