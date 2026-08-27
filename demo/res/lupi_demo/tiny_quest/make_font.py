#!/usr/bin/env python3
"""Generate a Lupi-compatible bitmap font spritesheet from a TTF font."""

from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError as exc:
    raise SystemExit("This script requires Pillow. Install it with: python -m pip install Pillow") from exc


CHARS = (
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    " .,!?:;+-*/=()[]{}<>_#%&$'\""
    "áàâãäÁÀÂÃÄ"
    "éèêëÉÈÊË"
    "íìîïÍÌÎÏ"
    "óòôõöÓÒÔÕÖ"
    "úùûüÚÙÛÜ"
    "çÇ"
    "ñÑ"
)

GUIDE_COLOR = (68, 68, 68, 255)


def parse_colors(colors: str) -> list[tuple[int, int, int, int]]:
    parsed = []
    for value in colors.split(";"):
        value = value.strip().removeprefix("#")
        if len(value) != 6:
            raise SystemExit(f"Invalid color '{value}'; expected RRGGBB values separated by semicolons.")
        try:
            parsed.append((int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16), 255))
        except ValueError as exc:
            raise SystemExit(f"Invalid color '{value}'; expected hexadecimal RRGGBB values.") from exc
    if not parsed:
        raise SystemExit("--colors must contain at least one color.")
    return parsed


def parse_color(color: str) -> tuple[int, int, int, int]:
    parsed = parse_colors(color)
    if len(parsed) != 1:
        raise SystemExit("--outline-color accepts exactly one RRGGBB color.")
    return parsed[0]


def resolve_font(font_name: str) -> Path:
    candidate = Path(font_name).expanduser()
    if candidate.is_file():
        return candidate

    fc_match = shutil.which("fc-match")
    if fc_match is None:
        raise SystemExit(
            f"Font '{font_name}' is not a file, and fc-match is unavailable for system font lookup."
        )

    result = subprocess.run(
        [fc_match, "-f", "%{file}", font_name],
        check=False,
        capture_output=True,
        text=True,
    )
    resolved = Path(result.stdout.strip())
    if result.returncode != 0 or not resolved.is_file():
        raise SystemExit(f"Could not resolve system font: {font_name}")
    return resolved


def list_system_fonts() -> None:
    fc_list = shutil.which("fc-list")
    if fc_list is None:
        raise SystemExit("fc-list is unavailable; install fontconfig to list system fonts.")

    result = subprocess.run(
        [fc_list, ":", "family"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise SystemExit("Could not list system fonts with fc-list.")

    families = {
        family.strip()
        for line in result.stdout.splitlines()
        for family in line.split(",")
        if family.strip()
    }
    print("\n".join(sorted(families, key=str.casefold)))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", help="TTF/OTF path or system font name")
    parser.add_argument("--height", type=int, help="Glyph cell and font height in pixels")
    parser.add_argument("--output", type=Path, help="Output PNG path")
    parser.add_argument("--list-fonts", action="store_true", help="List available system font families and exit")
    parser.add_argument(
        "--colors",
        default="#FFFFFF",
        help="Semicolon-separated RRGGBB colors from weakest to strongest coverage (default: #FFFFFF)",
    )
    parser.add_argument(
        "--outline-color",
        help="RRGGBB color for a 1-pixel glyph outline; increases each cell by 2 pixels",
    )
    parser.add_argument(
        "--columns",
        type=int,
        default=16,
        help="Number of glyph columns, excluding the guide column (default: 16)",
    )
    return parser.parse_args()


def build_sheet(
    font_path: Path,
    line_height: int,
    columns: int,
    glyph_colors: list[tuple[int, int, int, int]],
    outline_color: tuple[int, int, int, int] | None = None,
) -> Image.Image:
    font = ImageFont.truetype(str(font_path), line_height)
    cell_width = max(1, math.ceil(max(font.getlength(char) for char in CHARS)))
    cell_height = line_height + 1
    if outline_color is not None:
        cell_width += 2
        cell_height += 2
    ascent, descent = font.getmetrics()
    baseline = (cell_height + ascent - descent) // 2
    rows = math.ceil(len(CHARS) / columns)

    # The gray rectangle in the top-left corner records the guide cell size.
    sheet = Image.new(
        "RGBA",
        ((columns + 1) * cell_width, (rows + 1) * cell_height),
        (0, 0, 0, 0),
    )
    draw = ImageDraw.Draw(sheet)
    draw.rectangle((0, 0, cell_width - 1, cell_height - 1), fill=GUIDE_COLOR)

    for index, char in enumerate(CHARS):
        column = index % columns + 1
        row = index // columns + 1
        x = column * cell_width
        y = row * cell_height
        glyph_width = font.getlength(char)
        draw_x = x + (cell_width - glyph_width) / 2
        draw_y = y + baseline
        glyph_mask = Image.new("L", (cell_width, cell_height), 0)
        glyph_mask_draw = ImageDraw.Draw(glyph_mask)
        glyph_mask_draw.text(
            (draw_x - x, draw_y - y),
            char,
            font=font,
            fill=255,
            anchor="ls",
        )
        outline_mask = None
        if outline_color is not None:
            outline_mask = Image.new("L", (cell_width, cell_height), 0)
            outline_mask_draw = ImageDraw.Draw(outline_mask)
            outline_mask_draw.text(
                (draw_x - x, draw_y - y),
                char,
                font=font,
                fill=255,
                stroke_width=1,
                stroke_fill=255,
                anchor="ls",
            )
        glyph_mask_pixels = glyph_mask.load()
        outline_mask_pixels = outline_mask.load() if outline_mask is not None else None
        sheet_pixels = sheet.load()
        for mask_y in range(cell_height):
            for mask_x in range(cell_width):
                if outline_mask_pixels is not None and outline_mask_pixels[mask_x, mask_y] > 0:
                    sheet_pixels[x + mask_x, y + mask_y] = outline_color
                coverage = glyph_mask_pixels[mask_x, mask_y]
                if coverage == 0:
                    continue
                color_index = min(
                    len(glyph_colors) - 1,
                    (coverage * len(glyph_colors) - 1) // 256,
                )
                sheet_pixels[x + mask_x, y + mask_y] = glyph_colors[color_index]

    return sheet


def write_mapping(output_path: Path, char_width: int, char_height: int, outlined:bool) -> Path:
    mapping_path = output_path.with_suffix(".lua")
    mapping_lines = [
        "return {",
        f"    char_w = {char_width},",
        f"    char_h = {char_height},",
        f"    outlined = {'true' if outlined else 'false'},",
    ]
    for index, char in enumerate(CHARS):
        mapping_lines.append(f"    [{json.dumps(char, ensure_ascii=False)}] = {index},")
    mapping_lines.append("}")
    mapping_path.write_text("\n".join(mapping_lines) + "\n", encoding="utf-8")
    return mapping_path


def main() -> None:
    args = parse_args()
    if args.list_fonts:
        list_system_fonts()
        return
    if args.font is None or args.height is None or args.output is None:
        raise SystemExit("generation requires --font, --height, and --output")
    if args.height <= 0:
        raise SystemExit("--height must be greater than zero")
    if args.columns <= 0:
        raise SystemExit("--columns must be greater than zero")

    font_path = resolve_font(args.font)
    outline_color = parse_color(args.outline_color) if args.outline_color is not None else None
    font = ImageFont.truetype(str(font_path), args.height)
    char_w = max(1, math.ceil(max(font.getlength(char) for char in CHARS)))
    char_h = args.height + 1
    outlined:bool = False
    if outline_color is not None:
        char_w += 2
        char_h += 2
        outlined = True
    sheet = build_sheet(
        font_path,
        args.height,
        args.columns,
        parse_colors(args.colors),
        outline_color,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.output, format="PNG")
    
    mapping_path = write_mapping(args.output, char_w, char_h, outlined)
    print(f"Generated {args.output} and {mapping_path} from {font_path} ({sheet.width}x{sheet.height})")


if __name__ == "__main__":
    main()
