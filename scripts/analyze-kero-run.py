#!/usr/bin/env python3
"""Summarise a Kero Blaster runner log directory (read-only).

Usage: analyze-kero-run.py RUN_DIR
Reports, by presented frame number: maps/scripts/music loaded, pad input
changes, stops/faults, and (from frame-NNNNN.png captures) whether the
Negativus sitting on the title logo's "o" is still present (red eyes in
its region) and whether the title screen is still shown.
"""
import glob
import os
import re
import sys

from PIL import Image

run = sys.argv[1]
log = os.path.join(run, "boot.txt")
frame = 0
events = []
with open(log, "rb") as f:
    for raw in f:
        line = raw.decode("latin1").rstrip()
        if line.startswith("GPU.PRESENT"):
            frame += 1
        elif line.startswith("FILE.OPEN path=") and "error=0" in line:
            path = line.split()[1][5:]
            if re.search(r"/(field|text|bgm)/", path) and "/localize/" not in path:
                events.append((frame, "charge " + path.replace("/app0/rsc_k/", "")))
        elif line.startswith("PAD.INPUT"):
            events.append((frame, line))
        elif re.match(r"\s+(STOP|FAULT|HALT)", line) or "terminate called" in line or line.startswith("FAIL"):
            events.append((frame, line.strip()))
print(f"{run}: {frame} frames présentées")
for n, what in events:
    if not what.startswith("PAD.INPUT"):
        print(f"  frame {n:5d}  {what}")


def title_state(path):
    im = Image.open(path).convert("RGB")
    px = im.load()
    # Logo "o" Negativus: red eyes around x 1050-1350, y 150-400 (1920x1080).
    eyes = sum(1 for y in range(150, 400, 2) for x in range(1050, 1350, 2)
               if px[x, y][0] > 200 and px[x, y][1] < 80 and px[x, y][2] < 80)
    # Title background: dominant dark blue wall colour in the upper half.
    blue = sum(1 for y in range(0, 500, 10) for x in range(0, 1920, 10)
               if px[x, y][2] > 100 and px[x, y][0] < 60 and px[x, y][1] < 90)
    # Logo in colour (active): bright yellow/white letters in the logo area;
    # the inactive grey logo has none. Studio Pixel text lies below y 760.
    bright = sum(1 for y in range(160, 560, 4) for x in range(340, 1580, 4)
                 if min(px[x, y]) > 200 or (px[x, y][0] > 220 and px[x, y][1] > 180 and px[x, y][2] < 90))
    return eyes, blue, bright


previous = None
for path in sorted(glob.glob(os.path.join(run, "frame-*.png"))):
    n = int(path[-9:-4])
    eyes, blue, bright = title_state(path)
    state = ("titre" if blue > 3000 else "autre écran") + (", Negativus du o présent" if eyes else ", o libéré") + \
            (", logo en couleurs" if bright > 400 else "")
    if state != previous:
        print(f"  capture {n:5d}  {state} (yeux={eyes}, fond={blue}, vif={bright})")
        previous = state
