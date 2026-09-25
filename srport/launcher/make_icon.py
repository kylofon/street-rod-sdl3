"""Writes app.ico: the night road drawn in icon.cpp, for Explorer. Needs Pillow.

    python make_icon.py
"""
from pathlib import Path

from PIL import Image

tile = Image.new("RGBA", (16, 16))


def put(x, y, rgb):
    tile.putpixel((x, y), (rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF, 255))


def dark(y):  # the bands: two rows each near the bottom, one row towards the horizon
    return ((y - 12) // 2) % 2 == 1 if y >= 12 else y % 2 == 1


for y in range(16):
    for x in range(16):
        put(x, y, 0x000000 if y < 5 else 0xAA5500 if y == 5 else 0x00AA00 if dark(y) else 0x55FF55)
for x in (1, 5, 9, 13):  # the hills' dark tops
    put(x, 5, 0x550000)
for x, y, c in ((2, 1, 0xFFFFFF), (7, 3, 0xFFFF55), (12, 1, 0xFFFFFF), (14, 3, 0xAAAAAA), (5, 0, 0xAAAAAA)):
    put(x, y, c)  # stars
for y in range(6, 16):  # the road: two pixels wide at the horizon, the whole width at the bottom
    half = 1 + (y - 6) * 7 // 9
    for x in range(8 - half, 8 + half):
        put(x, y, 0xFFFFFF if x in (8 - half, 7 + half) else 0x000000)
for y in (8, 11, 12, 15):  # the centre line's dashes, longer nearer
    for x in (7, 8):
        put(x, y, 0xFFFFFF)

sizes = [16, 20, 24, 32, 48, 64, 256]
big = tile.resize((256, 256), Image.NEAREST)
big.save(Path(__file__).with_name("app.ico"), sizes=[(s, s) for s in sizes])
