#!/usr/bin/env python3
"""Convert introscreen.png to raw Apple IIgs Super Hi-Res data.

Outputs:
  screen.bin   - 32000 bytes of 320x200 pixel data, 2 pixels per byte
                 (left nibble first), ready to place at $2000 of bank $01.
  palette.bin  - 16-color IIgs palette (palette 0), 2 bytes per entry ($0RGB),
                 ready to place at $9E00 of bank $01.
  image_data.h - C tables (screen_data[], palette_data[]) for embedding.

The image is expected to be an indexed 320x200 PNG with a 16-color Apple IIgs
palette; its existing palette is preserved.
"""
from PIL import Image
import struct

img = Image.open("introscreen.png").convert("P")

width, height = img.size
print(f"Image: {width}x{height}")

if width != 320 or height != 200:
    raise SystemExit("Expected a 320x200 image for Apple IIgs SHR.")

palette = img.getpalette()  # RGB triplets (possibly up to 256 entries)

# Build the 16-color IIgs palette in $0RGB format (R,G,B are 4-bit).
colors_iigs = []
for i in range(16):
    r = palette[i * 3] >> 4
    g = palette[i * 3 + 1] >> 4
    b = palette[i * 3 + 2] >> 4
    color = (r << 8) | (g << 4) | b   # %0RGB
    colors_iigs.append(color)

pixels = list(img.getdata())

# Write pixel data: 2 pixels per byte, left nibble = even x.
with open("screen.bin", "wb") as f:
    for y in range(200):
        for x in range(0, 320, 2):
            left = pixels[y * 320 + x] & 0x0F
            right = pixels[y * 320 + (x + 1)] & 0x0F
            f.write(bytes([(left << 4) | right]))

# Write the palette (little-endian 16-bit entries).
with open("palette.bin", "wb") as f:
    for c in colors_iigs:
        f.write(struct.pack("<H", c))

# Write the C header.
with open("image_data.h", "w") as f:
    f.write("/* Auto-generated from introscreen.png - do not edit. */\n\n")
    f.write("static const unsigned char screen_data[] = {\n")
    with open("screen.bin", "rb") as s:
        data = s.read()
    for i in range(0, len(data), 16):
        f.write("  " + ",".join(f"0x{b:02X}" for b in data[i:i+16]) + ",\n")
    f.write("};\n\n")
    f.write("static const unsigned int palette_data[] = {\n")
    for c in colors_iigs:
        f.write(f"  0x{c:04X},\n")
    f.write("};\n")

print("Palette (IIgs $0RGB):")
for i, c in enumerate(colors_iigs):
    print(f"  {i}: ${c:04X}")
print(f"Wrote screen.bin ({len(data)} px bytes), palette.bin, image_data.h")
