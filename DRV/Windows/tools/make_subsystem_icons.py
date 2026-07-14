"""Generate the Windows Time Card subsystem icon family."""

from __future__ import annotations

import math
from pathlib import Path
from typing import Callable

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont


SIZE = 256
SCALE = 4
CANVAS = SIZE * SCALE
ICO_SIZES = [
    (16, 16),
    (20, 20),
    (24, 24),
    (32, 32),
    (40, 40),
    (48, 48),
    (64, 64),
    (128, 128),
    (256, 256),
]

BLUE_DARK = (24, 44, 102, 255)
GOLD = (218, 181, 57, 255)
WHITE = (250, 252, 255, 255)
SOFT_WHITE = (220, 229, 251, 255)


def s(value: float) -> int:
    return int(round(value * SCALE))


def box(values: tuple[float, float, float, float]) -> tuple[int, int, int, int]:
    return tuple(s(value) for value in values)


def points(values: list[tuple[float, float]]) -> list[tuple[int, int]]:
    return [(s(x), s(y)) for x, y in values]


def base_icon() -> Image.Image:
    return Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))


def add_glyph_outline(image: Image.Image) -> Image.Image:
    """Add contrast without introducing a tile or bounding shape."""
    alpha = image.getchannel("A")
    expanded = alpha.filter(ImageFilter.MaxFilter(s(7) * 2 + 1))
    outline_alpha = ImageChops.subtract(expanded, alpha)
    outline = Image.new("RGBA", image.size, BLUE_DARK)
    outline.putalpha(outline_alpha)
    return Image.alpha_composite(outline, image)


def symbol_layer() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    layer = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    return layer, ImageDraw.Draw(layer)


def draw_clock(draw: ImageDraw.ImageDraw) -> None:
    draw.ellipse(box((54, 54, 202, 202)), outline=WHITE, width=s(13))
    for angle in range(0, 360, 30):
        radians = math.radians(angle)
        inner = 65 if angle % 90 else 61
        outer = 73
        draw.line(
            points(
                [
                    (128 + inner * math.sin(radians), 128 - inner * math.cos(radians)),
                    (128 + outer * math.sin(radians), 128 - outer * math.cos(radians)),
                ]
            ),
            fill=GOLD,
            width=s(6),
        )
    draw.line(points([(128, 128), (128, 82)]), fill=WHITE, width=s(12))
    draw.line(points([(128, 128), (166, 146)]), fill=GOLD, width=s(12))
    draw.ellipse(box((119, 119, 137, 137)), fill=WHITE)


def phc_icon() -> Image.Image:
    image = base_icon()
    layer, draw = symbol_layer()
    draw_clock(draw)
    image.alpha_composite(layer)
    return image


def tod_icon() -> Image.Image:
    image = base_icon()
    layer, draw = symbol_layer()
    for angle in range(0, 360, 45):
        radians = math.radians(angle)
        draw.line(
            points(
                [
                    (128 + 76 * math.cos(radians), 128 + 76 * math.sin(radians)),
                    (128 + 97 * math.cos(radians), 128 + 97 * math.sin(radians)),
                ]
            ),
            fill=GOLD,
            width=s(11),
        )
    draw.ellipse(box((63, 63, 193, 193)), fill=GOLD, outline=WHITE, width=s(8))
    draw.line(points([(128, 128), (128, 88)]), fill=BLUE_DARK, width=s(12))
    draw.line(points([(128, 128), (162, 148)]), fill=WHITE, width=s(12))
    draw.ellipse(box((119, 119, 137, 137)), fill=BLUE_DARK)
    image.alpha_composite(layer)
    return image


def atom_icon() -> Image.Image:
    image = base_icon()
    layer, _ = symbol_layer()
    for angle in (0, 60, 120):
        orbit, orbit_draw = symbol_layer()
        orbit_draw.ellipse(box((48, 96, 208, 160)), outline=WHITE, width=s(9))
        orbit = orbit.rotate(angle, resample=Image.Resampling.BICUBIC, center=(CANVAS // 2, CANVAS // 2))
        layer.alpha_composite(orbit)
    draw = ImageDraw.Draw(layer)
    draw.ellipse(box((111, 111, 145, 145)), fill=GOLD, outline=WHITE, width=s(5))
    for x, y in ((48, 128), (168, 71), (168, 185)):
        draw.ellipse(box((x - 8, y - 8, x + 8, y + 8)), fill=GOLD)
    image.alpha_composite(layer)
    return image


def satellite_symbol(angle: float = -27) -> Image.Image:
    layer, draw = symbol_layer()
    draw.rounded_rectangle(box((103, 96, 153, 160)), radius=s(9), fill=GOLD, outline=WHITE, width=s(6))
    draw.polygon(points([(96, 103), (50, 77), (32, 110), (86, 137)]), fill=WHITE, outline=GOLD)
    draw.polygon(points([(160, 119), (206, 145), (224, 112), (170, 85)]), fill=WHITE, outline=GOLD)
    for start, end in (((56, 85), (44, 108)), ((75, 96), (62, 121)), ((181, 101), (168, 126)), ((201, 112), (187, 137))):
        draw.line(points([start, end]), fill=BLUE_DARK, width=s(5))
    draw.line(points([(128, 96), (128, 72)]), fill=WHITE, width=s(7))
    draw.arc(box((112, 47, 144, 79)), 200, 340, fill=GOLD, width=s(7))
    draw.ellipse(box((122, 64, 134, 76)), fill=WHITE)
    return layer.rotate(angle, resample=Image.Resampling.BICUBIC, center=(CANVAS // 2, CANVAS // 2))


def gnss_icon() -> Image.Image:
    image = base_icon()
    image.alpha_composite(satellite_symbol())
    return image


def gnss2_icon() -> Image.Image:
    image = base_icon()
    first = satellite_symbol(-24).resize((s(165), s(165)), Image.Resampling.LANCZOS)
    second = satellite_symbol(-24).resize((s(145), s(145)), Image.Resampling.LANCZOS)
    image.alpha_composite(first, (s(18), s(19)))
    image.alpha_composite(second, (s(91), s(92)))
    return image


def flash_icon() -> Image.Image:
    image = base_icon()
    layer, draw = symbol_layer()
    for offset in (65, 91, 117, 143, 169, 195):
        draw.line(points([(offset, 42), (offset, 65)]), fill=GOLD, width=s(9))
        draw.line(points([(offset, 191), (offset, 214)]), fill=GOLD, width=s(9))
        draw.line(points([(42, offset), (65, offset)]), fill=GOLD, width=s(9))
        draw.line(points([(191, offset), (214, offset)]), fill=GOLD, width=s(9))
    draw.rounded_rectangle(box((59, 59, 197, 197)), radius=s(20), fill=WHITE, outline=GOLD, width=s(8))
    draw.rounded_rectangle(box((82, 82, 174, 174)), radius=s(12), fill=BLUE_DARK)
    draw.polygon(points([(135, 91), (103, 137), (127, 137), (116, 170), (157, 120), (133, 120)]), fill=GOLD)
    image.alpha_composite(layer)
    return image


def sma_icon() -> Image.Image:
    image = base_icon()
    layer, draw = symbol_layer()
    hexagon = []
    for angle in range(0, 360, 60):
        radians = math.radians(angle - 30)
        hexagon.append((128 + 90 * math.cos(radians), 128 + 90 * math.sin(radians)))
    draw.polygon(points(hexagon), fill=GOLD, outline=WHITE)
    draw.ellipse(box((58, 58, 198, 198)), fill=BLUE_DARK, outline=WHITE, width=s(9))
    draw.ellipse(box((84, 84, 172, 172)), fill=WHITE, outline=GOLD, width=s(8))
    draw.ellipse(box((106, 106, 150, 150)), fill=GOLD, outline=BLUE_DARK, width=s(7))
    draw.ellipse(box((120, 120, 136, 136)), fill=WHITE)
    image.alpha_composite(layer)
    return image


def nmea_icon() -> Image.Image:
    image = base_icon()
    layer, draw = symbol_layer()
    wave = []
    for x in range(48, 172, 4):
        y = 128 - 31 * math.sin((x - 48) * math.pi / 38)
        wave.append((x, y))
    draw.line(points(wave), fill=WHITE, width=s(10), joint="curve")
    draw.line(points([(171, 128), (198, 128)]), fill=GOLD, width=s(10))
    draw.polygon(points([(198, 112), (224, 128), (198, 144)]), fill=GOLD)
    draw.arc(box((164, 70, 224, 130)), 225, 315, fill=SOFT_WHITE, width=s(7))
    draw.arc(box((154, 55, 239, 140)), 225, 315, fill=GOLD, width=s(7))
    image.alpha_composite(layer)
    return image


def timing_icon() -> Image.Image:
    image = base_icon()
    layer, draw = symbol_layer()
    draw.line(
        points([(38, 151), (68, 151), (68, 92), (111, 92), (111, 151), (154, 151), (154, 92), (199, 92), (199, 151), (220, 151)]),
        fill=WHITE,
        width=s(12),
        joint="curve",
    )
    draw.ellipse(box((29, 142, 47, 160)), fill=GOLD)
    draw.polygon(points([(217, 133), (239, 151), (217, 169)]), fill=GOLD)
    image.alpha_composite(layer)
    return image


def i2c_icon() -> Image.Image:
    image = base_icon()
    layer, draw = symbol_layer()
    draw.line(points([(39, 94), (217, 94)]), fill=WHITE, width=s(10))
    draw.line(points([(39, 162), (217, 162)]), fill=GOLD, width=s(10))
    for x in (63, 128, 193):
        draw.line(points([(x, 94), (x, 113)]), fill=WHITE, width=s(8))
        draw.line(points([(x, 143), (x, 162)]), fill=GOLD, width=s(8))
        draw.rounded_rectangle(box((45 + x - 63, 111, 81 + x - 63, 145)), radius=s(7), fill=BLUE_DARK, outline=WHITE, width=s(6))
        draw.ellipse(box((x - 6, 88, x + 6, 100)), fill=GOLD)
        draw.ellipse(box((x - 6, 156, x + 6, 168)), fill=WHITE)
    image.alpha_composite(layer)
    return image


def ptm_icon() -> Image.Image:
    image = base_icon()
    layer, draw = symbol_layer()
    draw.ellipse(box((45, 63, 163, 181)), outline=WHITE, width=s(11))
    draw.line(points([(104, 122), (104, 88)]), fill=GOLD, width=s(10))
    draw.line(points([(104, 122), (132, 139)]), fill=WHITE, width=s(10))
    draw.ellipse(box((96, 114, 112, 130)), fill=GOLD)
    draw.line(points([(151, 91), (207, 91)]), fill=GOLD, width=s(10))
    draw.polygon(points([(207, 76), (230, 91), (207, 106)]), fill=GOLD)
    draw.line(points([(211, 166), (155, 166)]), fill=WHITE, width=s(10))
    draw.polygon(points([(155, 151), (132, 166), (155, 181)]), fill=WHITE)
    image.alpha_composite(layer)
    return image


ICONS: dict[str, tuple[str, Callable[[], Image.Image]]] = {
    "phc": ("Precision Hardware Clock", phc_icon),
    "tod": ("GNSS Time-of-Day", tod_icon),
    "gnss": ("GNSS Receiver", gnss_icon),
    "gnss2": ("Secondary GNSS", gnss2_icon),
    "atomic": ("Atomic Clock", atom_icon),
    "nmea": ("NMEA Output", nmea_icon),
    "sma": ("SMA I/O", sma_icon),
    "timing": ("Timing I/O", timing_icon),
    "i2c": ("I2C Controller", i2c_icon),
    "flash": ("FPGA and Flash", flash_icon),
    "ptm": ("PCIe PTM", ptm_icon),
}


def load_font(size: int) -> ImageFont.ImageFont:
    candidates = [
        Path("C:/Windows/Fonts/segoeuib.ttf"),
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
    ]
    for candidate in candidates:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default()


def main() -> None:
    windows_dir = Path(__file__).resolve().parents[1]
    png_dir = windows_dir / "assets" / "subsystem-icons"
    png_dir.mkdir(parents=True, exist_ok=True)

    rendered: list[tuple[str, str, Image.Image]] = []
    for name, (label, factory) in ICONS.items():
        high_resolution = add_glyph_outline(factory())
        image = high_resolution.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
        image.save(windows_dir / f"timecard-{name}.ico", "ICO", sizes=ICO_SIZES)
        image.save(png_dir / f"timecard-{name}.png", "PNG", optimize=True)
        rendered.append((name, label, image))

    columns = 4
    cell_width = 280
    cell_height = 320
    rows = math.ceil(len(rendered) / columns)
    sheet = Image.new("RGB", (columns * cell_width, rows * cell_height), (245, 247, 252))
    sheet_draw = ImageDraw.Draw(sheet)
    font = load_font(23)
    for index, (_, label, image) in enumerate(rendered):
        column = index % columns
        row = index // columns
        x = column * cell_width + 12
        y = row * cell_height + 12
        sheet.paste(image, (x, y), image)
        text_box = sheet_draw.textbbox((0, 0), label, font=font)
        text_width = text_box[2] - text_box[0]
        sheet_draw.text(
            (column * cell_width + (cell_width - text_width) / 2, y + 266),
            label,
            font=font,
            fill=(31, 45, 82),
        )
    sheet.save(windows_dir / "assets" / "subsystem-icon-sheet.png", "PNG", optimize=True)


if __name__ == "__main__":
    main()
