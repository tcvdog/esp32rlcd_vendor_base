#!/usr/bin/env python3
"""Generate a small transparent animated caduceus GIF."""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw


SIZE = 64
SCALE = 4
FRAMES = 8
OUTPUT = Path(__file__).with_name("caduceus_anim.gif")


def project(point: tuple[float, float, float], angle: float) -> tuple[float, float, float]:
    """Rotate a 3D point around the vertical axis and project it to 2D."""
    x, y, z = point
    cos_a = math.cos(angle)
    sin_a = math.sin(angle)
    xr = x * cos_a + z * sin_a
    zr = -x * sin_a + z * cos_a
    return 32 + xr, y, zr


def scaled(points: list[tuple[float, float]]) -> list[tuple[int, int]]:
    return [(round(x * SCALE), round(y * SCALE)) for x, y in points]


def draw_polyline(
    draw: ImageDraw.ImageDraw,
    points: list[tuple[float, float, float]],
    angle: float,
    width: int,
) -> None:
    projected = [project(point, angle) for point in points]
    draw.line(scaled([(x, y) for x, y, _ in projected]), fill=(0, 0, 0, 255), width=width)


def snake_segments(phase: float) -> list[tuple[float, list[tuple[float, float, float]]]]:
    segments: list[tuple[float, list[tuple[float, float, float]]]] = []
    turns = 2.55
    radius = 8.6
    y_top = 17.0
    y_bottom = 57.0
    steps = 92
    points: list[tuple[float, float, float]] = []

    for i in range(steps + 1):
        progress = i / steps
        theta = phase + progress * turns * math.tau
        y = y_top + progress * (y_bottom - y_top)
        # Slight taper keeps the lower coils compact at icon size.
        r = radius * (0.92 + 0.08 * math.sin(progress * math.pi))
        points.append((r * math.cos(theta), y, r * math.sin(theta)))

    for start in range(0, len(points) - 2, 2):
        piece = points[start : start + 3]
        avg_depth = sum(point[2] for point in piece) / len(piece)
        segments.append((avg_depth, piece))

    return segments


def wing_segments(side: int) -> list[tuple[float, list[tuple[float, float, float]], int]]:
    """Return simple feathered wing strokes for one side of the caduceus."""
    outer = [
        (side * 3, 17, 0),
        (side * 10, 10, 1),
        (side * 19, 8, 0),
        (side * 27, 12, -1),
    ]
    lower = [
        (side * 4, 18, 0),
        (side * 11, 17, 0),
        (side * 18, 19, -1),
        (side * 24, 22, -2),
    ]
    feathers = [
        [(side * 9, 16, 0), (side * 15, 12, 0), (side * 22, 12, -1)],
        [(side * 10, 17, 0), (side * 16, 16, -1), (side * 23, 17, -2)],
        [(side * 10, 18, 0), (side * 15, 20, -1), (side * 21, 22, -2)],
    ]
    result: list[tuple[float, list[tuple[float, float, float]], int]] = []
    for points, width in [(outer, 2), (lower, 2), *[(line, 1) for line in feathers]]:
        avg_depth = sum(point[2] for point in points) / len(points)
        result.append((avg_depth, points, width))
    return result


def draw_frame(frame_index: int) -> Image.Image:
    angle = frame_index * math.tau / FRAMES
    image = Image.new("RGBA", (SIZE * SCALE, SIZE * SCALE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    strokes: list[tuple[float, str, object, int]] = []

    # Draw individual snake pieces by depth so the two coils read as a helix.
    for phase in (-math.pi / 2, math.pi / 2):
        for depth, points in snake_segments(phase):
            rotated_depth = project((0, 0, depth), angle)[2]
            strokes.append((rotated_depth, "line", points, 2 * SCALE))

    for side in (-1, 1):
        for depth, points, width in wing_segments(side):
            rotated_depth = project((0, 0, depth), angle)[2]
            strokes.append((rotated_depth, "line", points, width * SCALE))

    # Back-to-front ordering creates a simple rotation cue.
    for _, kind, points, width in sorted(strokes, key=lambda item: item[0]):
        if kind == "line":
            draw_polyline(draw, points, angle, width)

    # Staff and finials stay crisp in front, anchoring the icon.
    draw_polyline(draw, [(0, 12, 0), (0, 59, 0)], angle, 2 * SCALE)
    draw.ellipse(
        (
            round((32 - 2.4) * SCALE),
            round((11 - 2.4) * SCALE),
            round((32 + 2.4) * SCALE),
            round((11 + 2.4) * SCALE),
        ),
        outline=(0, 0, 0, 255),
        width=SCALE,
    )
    draw.line(scaled([(27.5, 59), (36.5, 59)]), fill=(0, 0, 0, 255), width=SCALE)

    # Small snake heads face outward near the top.
    head_offset = math.cos(angle) * 5
    for side in (-1, 1):
        cx = 32 + side * (7 + head_offset * 0.18)
        cy = 18
        draw.ellipse(
            (
                round((cx - 2.2) * SCALE),
                round((cy - 1.8) * SCALE),
                round((cx + 2.2) * SCALE),
                round((cy + 1.8) * SCALE),
            ),
            outline=(0, 0, 0, 255),
            width=SCALE,
        )

    return image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def main() -> None:
    frames = [draw_frame(index) for index in range(FRAMES)]
    frames[0].save(
        OUTPUT,
        save_all=True,
        append_images=frames[1:],
        duration=90,
        loop=0,
        disposal=2,
        transparency=0,
    )
    print(f"Generated {OUTPUT}")


if __name__ == "__main__":
    main()
