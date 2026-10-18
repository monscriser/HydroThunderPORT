"""Builds icon.ico (16-256 px) from steam_art/icon.png and a preview of the Steam
library page (hero + logo, portrait capsule, wide capsule, icon).

usage: steam_art_build.py <steam_art dir>
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

d = Path(sys.argv[1])


def art(name):
    # Same rule as the shortcut exporter: the steam_art/monscriser pack wins.
    own = d / "monscriser" / name
    return own if own.exists() else d / name


icon = Image.open(art("icon.png")).convert("RGBA")
icon.save(d / "icon.ico", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])

hero = Image.open(art("hero.png")).convert("RGBA").resize((1920, 620))
logo = Image.open(art("logo.png")).convert("RGBA")
# Same box the shortcut writes to <id>.json: upper-left, 36% x 47% of the hero.
box_w, box_h = int(1920 * 0.36), int(620 * 0.47)
scale = min(box_w / logo.width, box_h / logo.height)
logo = logo.resize((int(logo.width * scale), int(logo.height * scale)))
hero.alpha_composite(logo, (0, 0))
grid = Image.open(art("grid.png")).convert("RGBA").resize((300, 450))
banner = Image.open(art("banner.png")).convert("RGBA").resize((460, 215))
ico = icon.resize((96, 96))

W = 1920
page = Image.new("RGBA", (W, 620 + 40 + 450 + 40), (27, 40, 56, 255))
page.alpha_composite(hero, (0, 0))
y = 660
draw = ImageDraw.Draw(page)
draw.text((40, y - 28), "Capa (biblioteca)", fill=(220, 220, 220, 255))
page.alpha_composite(grid, (40, y))
draw.text((380, y - 28), "Banner (recentes)", fill=(220, 220, 220, 255))
page.alpha_composite(banner, (380, y))
draw.text((880, y - 28), "Icone", fill=(220, 220, 220, 255))
page.alpha_composite(ico, (880, y))
page.convert("RGB").save(d / "previa_steam.jpg", quality=90)
print("ok", d / "icon.ico", d / "previa_steam.jpg")
