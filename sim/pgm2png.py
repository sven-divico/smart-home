#!/usr/bin/env python3
"""Convert every build/preview/*.pgm the host preview wrote into a 1x PNG and a
2x nearest-neighbour PNG (crisp for eyeballing 1-bit layouts)."""
import glob, os
from PIL import Image

for pgm in sorted(glob.glob("build/preview/*.pgm")):
    base = os.path.splitext(pgm)[0]
    im = Image.open(pgm).convert("L")
    im.save(base + ".png")
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(base + "@2x.png")
    print(f"{os.path.basename(pgm)} -> {os.path.basename(base)}.png ({im.size[0]}x{im.size[1]})")
