#!/usr/bin/env python3
"""
Konwerter TTF -> font LVGL 9 (lv_font_fmt_txt, 4 bpp, bez kompresji).
Zamiennik lv_font_conv (Node) oparty o Pillow.

    python gen_lvgl_font.py FONT.ttf SIZE NAME "ZNAKI" > NAME.c

ZNAKI: lista znakow albo "ascii" (0x20-0x7E).

Fonty uzyte w projekcie (licencja SIL OFL 1.1):
  DSEG7 Classic Bold          https://github.com/keshikan/DSEG
  Barlow Semi Condensed       https://github.com/jpt/barlow
"""
import sys
from PIL import Image, ImageDraw, ImageFont


def render(path, size, chars):
    font = ImageFont.truetype(path, size)
    glyphs = []
    for ch in chars:
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        adv = font.getlength(ch)
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        px = []
        ink = None
        if w and h:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
            ink = img.point(lambda v: 255 if v >= 8 else 0).getbbox()
        if ink:
            # docinamy do faktycznie zapalonych pikseli
            img = img.crop(ink)
            x0, y0 = x0 + ink[0], y0 + ink[1]
            w, h = img.size
            y1 = y0 + h
            px = [min(15, (v + 8) >> 4) for v in img.getdata()]
        else:
            w = h = 0
        glyphs.append(dict(ch=ch, w=w, h=h, x=x0, y=-y1, adv=round(adv * 16), px=px, top=-y0, bot=y1))
    return font, glyphs


def pack4(px):
    out = bytearray()
    for i in range(0, len(px), 2):
        hi = px[i]
        lo = px[i + 1] if i + 1 < len(px) else 0
        out.append((hi << 4) | lo)
    return out


def main():
    path, size, name, chars = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    if chars == "ascii":
        chars = "".join(chr(c) for c in range(0x20, 0x7F))
    chars = "".join(sorted(set(chars)))
    font, glyphs = render(path, size, chars)

    asc, desc = font.getmetrics()
    ink_top = max([g["top"] for g in glyphs if g["h"]] or [asc])
    ink_bot = max([g["bot"] for g in glyphs if g["h"]] + [0])
    asc, desc = max(asc, ink_top), max(desc, ink_bot)

    def ink_h(c):
        g = next((g for g in glyphs if g["ch"] == c), None)
        return g["top"] if g and g["h"] else 0

    cap_h = ink_h("H") or ink_h("8")
    x_h = ink_h("x") or cap_h

    o = []
    o.append("/* Wygenerowane przez tools/gen_lvgl_font.py - nie edytowac recznie.")
    o.append(f" * Font: {path.replace(chr(92), '/').split('/')[-1]}, {size} px, 4 bpp (SIL OFL 1.1)")
    o.append(f" * Znaki: {chars!r} */")
    o.append('#include "lvgl.h"\n')
    o.append("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")
    idx, offs = 0, []
    for g in glyphs:
        offs.append(idx)
        data = pack4(g["px"])
        idx += len(data)
        if data:
            o.append(f"    /* {g['ch']!r} */")
            for i in range(0, len(data), 16):
                o.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    if idx == 0:
        o.append("    0x00,")
    o.append("};\n")

    o.append("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    o.append("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},")
    for g, off in zip(glyphs, offs):
        o.append(f"    {{.bitmap_index = {off}, .adv_w = {g['adv']}, .box_w = {g['w']}, .box_h = {g['h']}, "
                 f".ofs_x = {g['x']}, .ofs_y = {g['y']}}}, /* {g['ch']!r} */")
    o.append("};\n")

    cps = [ord(c) for c in chars]
    o.append(f"static const uint16_t unicode_list[] = {{ {', '.join(str(c - cps[0]) for c in cps)} }};\n")
    o.append("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    o.append(f"    {{ .range_start = {cps[0]}, .range_length = {cps[-1] - cps[0] + 1}, .glyph_id_start = 1,")
    o.append(f"      .unicode_list = unicode_list, .glyph_id_ofs_list = NULL, .list_length = {len(cps)},")
    o.append("      .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY },")
    o.append("};\n")

    o.append("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    o.append("    .glyph_bitmap = glyph_bitmap, .glyph_dsc = glyph_dsc, .cmaps = cmaps,")
    o.append("    .kern_dsc = NULL, .kern_scale = 0, .cmap_num = 1, .bpp = 4, .kern_classes = 0,")
    o.append("    .bitmap_format = 0,")
    o.append("};\n")

    o.append(f"const lv_font_t {name} = {{")
    o.append("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    o.append("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    o.append(f"    .line_height = {asc + desc},")
    o.append(f"    .base_line = {desc},")
    o.append("#if LV_VERSION_CHECK(9, 6, 0) || LVGL_VERSION_MAJOR >= 10")
    o.append(f"    .cap_height = {cap_h},")
    o.append(f"    .x_height = {x_h},")
    o.append("#endif")
    o.append("    .subpx = LV_FONT_SUBPX_NONE,")
    o.append("    .underline_position = -1,")
    o.append("    .underline_thickness = 1,")
    o.append("    .static_bitmap = 1,")
    o.append("    .dsc = &font_dsc,")
    o.append("    .fallback = NULL,")
    o.append("    .user_data = NULL,")
    o.append("};")
    sys.stdout.write("\n".join(o) + "\n")


if __name__ == "__main__":
    main()
