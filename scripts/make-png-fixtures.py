#!/usr/bin/env python3
"""Write PNG fixtures and their PIL-decoded RGBA8 references for PngDecoderTest.

Usage: make-png-fixtures.py OUT_DIR [GAME_DIR]
Game PNGs are only read; references for them go to OUT_DIR. Each line of
OUT_DIR/manifest.txt is: png_path rgba_path alpha(0-255) [interlaced]
"""
import os, random, struct, sys, zlib
from PIL import Image

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
rng = random.Random(61)
lines = []

def reference(img, alpha):
    if img.mode == "I;16":  # 16-bit grey: high byte (PIL's RGBA conversion clips instead)
        raw = img.tobytes("raw", "I;16")
        return b"".join(bytes((raw[i + 1],) * 3 + (alpha,)) for i in range(0, len(raw), 2))
    has_alpha = img.mode in ("RGBA", "LA", "PA") or "transparency" in img.info
    rgba = img.convert("RGBA")
    if not has_alpha:
        rgba.putalpha(alpha)
    return rgba.tobytes()

def add(name, img, alpha=0xff, **save):
    path = os.path.join(out, name + ".png")
    img.save(path, **save)
    ref = os.path.join(out, name + ".rgba")
    with Image.open(path) as back:
        open(ref, "wb").write(reference(back, alpha))
    lines.append(f"{path} {ref} {alpha}")

w, h = 37, 23
pixels = [(rng.randrange(256), rng.randrange(256), rng.randrange(256), rng.randrange(256)) for _ in range(w * h)]
rgba = Image.new("RGBA", (w, h)); rgba.putdata(pixels)
add("rgba8", rgba)
add("rgb8", rgba.convert("RGB"), alpha=0x80)
add("rgb8_fast", rgba.convert("RGB"), compress_level=1)
add("rgb8_stored", rgba.convert("RGB"), compress_level=0)
add("gray8", rgba.convert("L"))
add("graya8", rgba.convert("LA"))
pal = rgba.convert("RGB").quantize(colors=200)
add("palette8", pal)
pal_t = pal.copy(); pal_t.info["transparency"] = bytes(rng.randrange(256) for _ in range(150))
add("palette8_trns", pal_t, transparency=pal_t.info["transparency"])
for bits, colors in ((1, 2), (2, 4), (4, 16)):
    p = rgba.convert("RGB").quantize(colors=colors)
    add(f"palette{bits}", p, bits=bits)
add("gray16", rgba.convert("L").convert("I").point(lambda v: v * 257).convert("I;16"))
for bits in (1, 2, 4):
    # Grey sub-byte images are written by hand (PIL has no writer option).
    data = bytearray()
    vals = [rng.randrange(1 << bits) for _ in range(w * h)]
    for y in range(h):
        data.append(0)  # filter type None
        acc = 0; n = 0; rowbytes = bytearray()
        for x in range(w):
            acc = (acc << bits) | vals[y * w + x]; n += bits
            if n == 8: rowbytes.append(acc); acc = 0; n = 0
        if n: rowbytes.append(acc << (8 - n))
        data += rowbytes
    def chunk(t, b): return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, bits, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(bytes(data), 9)) + chunk(b"IEND", b"")
    path = os.path.join(out, f"gray{bits}.png"); open(path, "wb").write(png)
    ref = os.path.join(out, f"gray{bits}.rgba")
    with Image.open(path) as back: open(ref, "wb").write(reference(back, 0xff))
    lines.append(f"{path} {ref} 255")
inter = os.path.join(out, "interlaced.png")
# PIL cannot write Adam7; patch the IHDR interlace byte of a valid file (CRC fixed).
src = open(os.path.join(out, "rgb8.png"), "rb").read()
ihdr = bytearray(src[12:29]); ihdr[16] = 1
patched = src[:12] + bytes(ihdr) + struct.pack(">I", zlib.crc32(bytes(ihdr)) & 0xffffffff) + src[33:]
open(inter, "wb").write(patched)
lines.append(f"{inter} - 255 interlaced")

if len(sys.argv) > 2:
    game = sys.argv[2]
    for root, _, files in os.walk(game):
        for f in sorted(files):
            if f.lower().endswith(".png"):
                path = os.path.join(root, f)
                ref = os.path.join(out, "game-" + os.path.relpath(path, game).replace("/", "_") + ".rgba")
                with Image.open(path) as img: open(ref, "wb").write(reference(img, 0xff))
                lines.append(f"{path} {ref} 255")
open(os.path.join(out, "manifest.txt"), "w").write("\n".join(lines) + "\n")
print(f"{len(lines)} fixtures")
