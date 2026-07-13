"""
Pipeline:
1. Load each tile (136 x 124 px RGBA) from main/assets/pet_sheet/{state}_{N}.png.
2. Sample the corner background color, then make near-background pixels
   transparent (fuzzy distance match, tol=40).
3. Tight-crop the sprite, pre-multiply alpha, LANCZOS resize to FINAL_SIZE,
   un-pre-multiply using the *continuous* alpha channel, then hard-threshold
   alpha for clean edges.
4. Hand each preprocessed PNG to tools/LVGLImage.py --ofmt C --cf RGB565A8
   which writes the correct LVGL 9 byte layout:
       [RGB565 rows: w*2 bytes/row * h rows]
       [Alpha plane:  w bytes/row  * h rows]
   and emits a standard lv_image_dsc_t (with .magic, .cf, .reserved_2, etc.).
5. Splice the per-frame _map[] data and descriptor bodies into one
   pet_frames.h, keeping the per-state frame pointer arrays and the
   pet_anim_state_t enum.
"""
import os
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

# Import the vendored LVGLImage.py as a module so we can call
# LVGLImage().from_png(...) in-process. Adding its output .c files to
# ESP-IDF's component build caused include-path complaints about
# `lvgl/lvgl.h` because the script emits its own include guard block.
sys.path.insert(0, str(Path(__file__).parent))
import LVGLImage as lvgl_image_tool  # noqa: E402

ROOT = Path(r'C:\Users\bujih\Desktop\code\github\ellenp2p\esp32-pet')
SRC = ROOT / 'main' / 'assets' / 'pet_sheet'
OUT_PNG = ROOT / 'main' / 'assets' / 'pet_32'
OUT_H = ROOT / 'main' / 'app' / 'pet_frames.h'

STATES = ['idle', 'happy', 'eating', 'sleeping', 'playing', 'sick']
N_FRAMES = 9
FINAL_SIZE = 96


def remove_background(im, bg_rgb, tol=40):
    """Set near-background pixels to transparent. Keeps RGB unchanged
    (straight alpha) — LVGL does the blending at render time."""
    arr = np.array(im).copy()
    r = arr[..., 0].astype(np.int32)
    g = arr[..., 1].astype(np.int32)
    b = arr[..., 2].astype(np.int32)
    a = arr[..., 3].astype(np.int32)
    dist = np.sqrt((r - int(bg_rgb[0]))**2 + (g - int(bg_rgb[1]))**2 + (b - int(bg_rgb[2]))**2)
    new_alpha = np.clip((dist - tol) / tol * 255, 0, 255).astype(np.uint8)
    final_alpha = np.minimum(a, new_alpha)
    arr[..., 3] = final_alpha
    return Image.fromarray(arr, mode='RGBA')


def fit_to_canvas(im, size, bg_threshold=0.5):
    """Tight-crop + LANCZOS resize + clean alpha edges (see plan #47)."""
    arr = np.array(im)
    if arr.shape[2] == 3:
        return im.resize((size, size), Image.LANCZOS)
    alpha = arr[..., 3]
    mask = alpha > int(255 * bg_threshold)
    if not mask.any():
        return Image.new('RGBA', (size, size), (0, 0, 0, 0))
    ys, xs = np.where(mask)
    y0, y1 = ys.min(), ys.max() + 1
    x0, x1 = xs.min(), xs.max() + 1
    crop = im.crop((x0, y0, x1, y1))
    crop_arr = np.array(crop).astype(np.float32)
    af = crop_arr[..., 3:4] / 255.0
    crop_arr[..., :3] *= af
    crop_premul = Image.fromarray(crop_arr.astype(np.uint8), mode='RGBA')
    cw, ch = crop_premul.size
    inner = size - 4
    scale = min(inner / cw, inner / ch)
    new_w = max(1, int(round(cw * scale)))
    new_h = max(1, int(round(ch * scale)))
    scaled = crop_premul.resize((new_w, new_h), Image.LANCZOS)
    s_arr = np.array(scaled).astype(np.float32)

    # Un-pre-multiply using the CONTINUOUS alpha channel from LANCZOS.
    cont_a = s_arr[..., 3]
    safe_a = np.maximum(cont_a, 1.0)
    s_arr[..., 0] = np.clip(s_arr[..., 0] * 255.0 / safe_a, 0, 255)
    s_arr[..., 1] = np.clip(s_arr[..., 1] * 255.0 / safe_a, 0, 255)
    s_arr[..., 2] = np.clip(s_arr[..., 2] * 255.0 / safe_a, 0, 255)
    # Hard-threshold alpha for clean LCD edges.
    new_a = np.where(cont_a > 32, 255.0, 0.0).astype(np.float32)
    s_arr[..., 3] = new_a
    out = Image.fromarray(s_arr.astype(np.uint8), mode='RGBA')

    canvas = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    px = (size - new_w) // 2
    py = (size - new_h) // 2
    alpha_mask = out.split()[3]
    canvas.paste(out, (px, py), alpha_mask)
    return canvas


def encode_via_lvglimagetool(state, f, png_path):
    """Use the vendored LVGLImage module in-process (no .c file output) to
    convert a preprocessed PNG into:
      - map_bytes: bytes — the raw image payload (RGB565A8 layout:
        [RGB plane w*2 bytes/row * h rows][Alpha plane w bytes/row * h rows])
      - dsc_inner: str  — the lv_image_dsc_t initializer body (between
        its outer { ... }), formatted for splicing into our per-state arrays.
      - var: str        — variable name pet_<state>_<f>

    We don't write a per-frame .c file because ESP-IDF's CMake picks them
    up and complains about the script's emitted `#include <lvgl/lvgl.h>`.
    """
    var = f'pet_{state}_{f}'
    cf = lvgl_image_tool.ColorFormat.RGB565A8
    img = lvgl_image_tool.LVGLImage()
    img.from_png(str(png_path), cf=cf)
    img.adjust_stride(align=1)
    map_bytes = bytes(img.data)

    # Build the descriptor initializer body by hand, matching the format
    # LVGLImage.to_c_array would produce. This way the descriptor points
    # at `pet_<state>_<f>_map` (our consolidated header's symbol) instead
    # of at a single-file local name.
    dsc_inner = (
        '.header = {\n'
        f'    .magic = LV_IMAGE_HEADER_MAGIC,\n'
        f'    .cf = LV_COLOR_FORMAT_{cf.name},\n'
        f'    .flags = 0,\n'
        f'    .w = {img.w},\n'
        f'    .h = {img.h},\n'
        f'    .stride = {img.stride},\n'
        f'    .reserved_2 = 0,\n'
        '  },\n'
        f'  .data_size = sizeof({var}_map),\n'
        f'  .data = {var}_map,\n'
        '  .reserved = NULL,'
    )
    return map_bytes, dsc_inner, var


def _format_map_array(map_bytes):
    """Format raw RGB565A8 image bytes as a C array literal.

    16 bytes per line, indented 4 spaces, ending with a comma.
    """
    out = []
    for i in range(0, len(map_bytes), 16):
        chunk = map_bytes[i:i + 16]
        out.append('    ' + ', '.join(f'0x{b:02X}' for b in chunk) + ',')
    return '\n'.join(out)


def emit_header(states, n_frames, encoded):
    """Build the consolidated pet_frames.h.

    `encoded` is a list of `(state, f, map_bytes, dsc_inner, var)` rows
    in iteration order.
    """
    lines = []
    lines.append('#pragma once')
    lines.append('// Auto-generated by tools/build_frames.py — DO NOT EDIT.')
    lines.append('// Bytes are produced by tools/LVGLImage.py --cf RGB565A8,')
    lines.append('// which lays out RGB565A8 as:')
    lines.append('//   [RGB565 rows: w*2 bytes/row * h rows]')
    lines.append('//   [Alpha plane: w   bytes/row * h rows]')
    lines.append('#include <lvgl.h>')
    lines.append('')
    lines.append('#ifndef LV_ATTRIBUTE_MEM_ALIGN')
    lines.append('#define LV_ATTRIBUTE_MEM_ALIGN')
    lines.append('#endif')
    lines.append('#ifndef LV_ATTRIBUTE_LARGE_CONST')
    lines.append('#define LV_ATTRIBUTE_LARGE_CONST')
    lines.append('#endif')
    lines.append('')
    lines.append('#ifdef __cplusplus')
    lines.append('extern "C" {')
    lines.append('#endif')
    lines.append('')

    # Per-frame _map[] data blocks. We also pre-define the per-symbol
    # LV_ATTRIBUTE_<NAME> macro (the way LVGLImage.py does it) so the
    # generated attribute token isn't a hard error.
    for state, f, map_bytes, _dsc, var in encoded:
        attr_macro = f'LV_ATTRIBUTE_{var.upper()}'
        lines.append(f'#ifndef {attr_macro}')
        lines.append(f'#define {attr_macro}')
        lines.append('#endif')
        lines.append(f'// {state} frame {f}')
        lines.append('static const')
        lines.append(f'LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST {attr_macro}')
        lines.append(f'uint8_t {var}_map[] = {{')
        lines.append(_format_map_array(map_bytes))
        lines.append('};')
        lines.append('')

    # Per-state descriptor arrays. Each element is the lv_image_dsc_t body
    # (between { ... }) produced by LVGLImage.py.
    lines.append('// Per-state descriptor arrays (lv_image_dsc_t).')
    for state in states:
        lines.append(f'static const lv_image_dsc_t pet_{state}_imgs[{n_frames}] = {{')
        for f in range(n_frames):
            row = next(r for r in encoded if r[0] == state and r[1] == f)
            _, _, _, dsc_inner, var = row
            lines.append(f'    // {state} frame {f}')
            lines.append('    {')
            for dl in dsc_inner.splitlines():
                dl_stripped = dl.rstrip()
                if dl_stripped:
                    lines.append('        ' + dl_stripped)
            lines.append('    },')
        lines.append('};')
        lines.append('')

    # State enum.
    lines.append('typedef enum {')
    for i, s in enumerate(states):
        lines.append(f'    PET_ANIM_{s.upper()} = {i},')
    lines.append(f'    PET_ANIM_COUNT = {len(states)},')
    lines.append('} pet_anim_state_t;')
    lines.append('')

    # Per-state pointer arrays.
    for s in states:
        lines.append(f'static const lv_image_dsc_t * pet_{s}_frames[{n_frames}] = {{')
        for f in range(n_frames):
            lines.append(f'    &pet_{s}_imgs[{f}],')
        lines.append('};')
        lines.append('')

    # Combined lookup.
    lines.append('static const lv_image_dsc_t * const * pet_anim_frames[PET_ANIM_COUNT] = {')
    for s in states:
        lines.append(f'    [PET_ANIM_{s.upper()}] = pet_{s}_frames,')
    lines.append('};')
    lines.append('')

    lines.append('#ifdef __cplusplus')
    lines.append('}')
    lines.append('#endif')
    return '\n'.join(lines)


def main():
    # Wipe previous scratch output.
    if OUT_PNG.exists():
        for f in OUT_PNG.iterdir():
            f.unlink()
    OUT_PNG.mkdir(parents=True, exist_ok=True)

    # Stage 1-3: preprocess each tile (crop + bg removal + LANCZOS resize).
    preprocessed = []
    for state in STATES:
        for f in range(N_FRAMES):
            src_png = SRC / f'{state}_{f}.png'
            im = Image.open(src_png).convert('RGBA')
            w, h = im.size
            corners = []
            for cx, cy in [(2, 2), (w-3, 2), (2, h-3), (w-3, h-3)]:
                corners.append(im.getpixel((cx, cy))[:3])
            bg = tuple(int(sum(c[i] for c in corners) / 4) for i in range(3))
            no_bg = remove_background(im, bg, tol=40)
            small = fit_to_canvas(no_bg, FINAL_SIZE)
            small.save(OUT_PNG / f'{state}_{f}.png')
            preprocessed.append((state, f, OUT_PNG / f'{state}_{f}.png'))

    # Stage 4: encode each via in-process LVGLImage.
    encoded = []
    for state, f, png_path in preprocessed:
        map_bytes, dsc_inner, var = encode_via_lvglimagetool(state, f, png_path)
        encoded.append((state, f, map_bytes, dsc_inner, var))
        print(f'  encoded {state}_{f}')

    # Contact sheet for sanity-checking.
    TILE = 48
    sheet = Image.new('RGBA', (N_FRAMES * TILE, len(STATES) * TILE),
                      (32, 32, 32, 255))
    for i, (state, f, _png) in enumerate(preprocessed):
        r = STATES.index(state)
        im = Image.open(_png)
        scaled = im.resize((TILE, TILE), Image.NEAREST)
        sheet.paste(scaled, (f * TILE, r * TILE), scaled)
    sheet.save(OUT_PNG / '_contact_sheet.png')
    print(f'{FINAL_SIZE}x{FINAL_SIZE} contact sheet saved.')

    # Stage 5: emit consolidated header.
    text = emit_header(STATES, N_FRAMES, encoded)
    OUT_H.write_text(text, encoding='utf-8')
    print(f'Header written to {OUT_H}')

    total = OUT_H.stat().st_size
    print(f'Header size: {total} bytes ({total/1024:.1f} KB)')


if __name__ == '__main__':
    main()