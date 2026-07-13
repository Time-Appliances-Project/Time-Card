"""Generate the multi-resolution Windows TimeCard device icon."""

from pathlib import Path

from PIL import Image


SIZE = 256


def make_icon(source_path: Path) -> Image.Image:
    source = Image.open(source_path).convert("RGBA")
    alpha_bounds = source.getchannel("A").getbbox()
    if alpha_bounds is None:
        raise ValueError(f"source has no visible pixels: {source_path}")
    source = source.crop(alpha_bounds)
    source.thumbnail((SIZE - 8, SIZE - 8), Image.Resampling.LANCZOS)

    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    position = ((SIZE - source.width) // 2, (SIZE - source.height) // 2)
    image.alpha_composite(source, position)
    return image


def main() -> None:
    windows_dir = Path(__file__).resolve().parents[1]
    source = windows_dir / "assets" / "timecard-device-transparent.png"
    preview = windows_dir / "assets" / "timecard-icon.png"
    icon = windows_dir / "timecard.ico"
    preview.parent.mkdir(parents=True, exist_ok=True)

    image = make_icon(source)
    image.save(preview, "PNG", optimize=True)
    image.save(
        icon,
        "ICO",
        sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40),
               (48, 48), (64, 64), (128, 128), (256, 256)],
    )


if __name__ == "__main__":
    main()
