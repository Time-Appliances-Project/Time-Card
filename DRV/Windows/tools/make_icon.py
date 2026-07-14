"""Generate the Time Card class and controller Windows icons."""

from collections import deque
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


def remove_border_white(source_path: Path, cutoff: int = 224) -> Image.Image:
    """Remove only near-white pixels connected to the image border."""
    source = Image.open(source_path).convert("RGB")
    width, height = source.size
    pixels = list(source.getdata())
    eligible = bytearray(width * height)
    connected = bytearray(width * height)

    for index, (red, green, blue) in enumerate(pixels):
        if min(red, green, blue) >= cutoff and max(red, green, blue) - min(red, green, blue) <= 12:
            eligible[index] = 1

    queue: deque[int] = deque()
    for x in range(width):
        queue.extend((x, (height - 1) * width + x))
    for y in range(height):
        queue.extend((y * width, y * width + width - 1))

    while queue:
        index = queue.popleft()
        if connected[index] or not eligible[index]:
            continue
        connected[index] = 1
        x = index % width
        y = index // width
        if x:
            queue.append(index - 1)
        if x + 1 < width:
            queue.append(index + 1)
        if y:
            queue.append(index - width)
        if y + 1 < height:
            queue.append(index + width)

    output = Image.new("RGBA", source.size, (0, 0, 0, 0))
    output_pixels = []
    for index, color in enumerate(pixels):
        if not connected[index]:
            output_pixels.append((*color, 255))
            continue

        whiteness = min(color)
        alpha = round((255 - whiteness) * 255 / (255 - cutoff))
        alpha = max(0, min(255, alpha))
        if alpha == 0:
            output_pixels.append((0, 0, 0, 0))
            continue

        recovered = tuple(
            max(0, min(255, round((channel * 255 - 255 * (255 - alpha)) / alpha)))
            for channel in color
        )
        output_pixels.append((*recovered, alpha))

    output.putdata(output_pixels)
    return output


def fit_icon(source: Image.Image) -> Image.Image:
    alpha_bounds = source.getchannel("A").getbbox()
    if alpha_bounds is None:
        raise ValueError("source has no visible pixels")
    source = source.crop(alpha_bounds)
    source.thumbnail((SIZE - 8, SIZE - 8), Image.Resampling.LANCZOS)
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    position = ((SIZE - source.width) // 2, (SIZE - source.height) // 2)
    image.alpha_composite(source, position)
    return image


def save_icon(image: Image.Image, destination: Path) -> None:
    image.save(
        destination,
        "ICO",
        sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40),
               (48, 48), (64, 64), (128, 128), (256, 256)],
    )


def make_app_icon(source_path: Path) -> Image.Image:
    """Crop the detailed app artwork to a recognizable square taskbar mark."""
    source = Image.open(source_path).convert("RGBA")
    side = min(source.width, source.height)
    source = source.crop((0, 0, side, side))
    return source.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def main() -> None:
    windows_dir = Path(__file__).resolve().parents[1]
    assets_dir = windows_dir / "assets"
    assets_dir.mkdir(parents=True, exist_ok=True)

    class_image = make_icon(assets_dir / "timecard-device-transparent.png")
    class_image.save(assets_dir / "timecard-icon.png", "PNG", optimize=True)
    save_icon(class_image, windows_dir / "timecard-class.ico")

    controller_cutout = remove_border_white(
        assets_dir / "ocp-timecard-controller-source.png"
    )
    controller_cutout.save(
        assets_dir / "ocp-timecard-controller-transparent.png",
        "PNG",
        optimize=True,
    )
    controller_image = fit_icon(controller_cutout)
    controller_image.save(
        assets_dir / "ocp-timecard-controller-icon.png",
        "PNG",
        optimize=True,
    )
    save_icon(controller_image, windows_dir / "timecard.ico")

    app_image = make_app_icon(assets_dir / "timecard-logo.png")
    app_image.save(assets_dir / "timecard-app-icon.png", "PNG", optimize=True)
    save_icon(app_image, assets_dir / "timecard-app.ico")


if __name__ == "__main__":
    main()
