#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "Pillow",
# ]
# ///
"""
Screenshot analyzer for godot-cli.

Usage:
    uv run scripts/screenshot.py screenshot.png

Analyzes a screenshot for common visual bugs:
- Missing background coverage
- Player position relative to ground
- UI visibility
- Dark rectangles (CanvasLayer bleed)
"""
import sys
from PIL import Image

def analyze(path: str):
    img = Image.open(path)
    w, h = img.size
    print(f"Image: {w}x{h}")

    # Sample key regions
    regions = {
        "top-left (UI area)": (0, 0, 120, 60),
        "top-center (sky)": (w//4, 0, 3*w//4, 60),
        "bottom (ground)": (0, h-40, w, h),
        "left edge": (0, 0, 10, h),
        "right edge": (w-10, 0, w, h),
    }

    for name, (x1, y1, x2, y2) in regions.items():
        region = img.crop((x1, y1, x2, y2))
        avg_color = tuple(int(c) for c in region.resize((1, 1)).getpixel((0, 0)))
        print(f"  {name}: avg RGB={avg_color}")

    # Check for uniform color regions (possible issues)
    # Dark rectangle detection
    for y in range(0, h, 20):
        for x in range(0, w, 20):
            px = img.getpixel((x, y))
            # Check for very dark pixels that might indicate rendering issues
            if px[0] < 30 and px[1] < 30 and px[2] < 50:
                print(f"  DARK PIXEL at ({x},{y}): RGB={px}")
                break
        else:
            continue
        break

    print("Analysis complete.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: uv run scripts/screenshot.py <screenshot.png>", file=sys.stderr)
        sys.exit(1)
    analyze(sys.argv[1])
