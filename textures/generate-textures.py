#!/usr/bin/env python3
"""Generate Swordfish's bundled seamless greyscale background textures.

Every tile wraps in both directions. The renderer treats a texture's average
grey as neutral, so these tiles only lighten or darken the theme colours below.
"""
import math
import os
import random
import struct
import sys
import zlib

SIZE = 256


def write_png(path, pixels):
    raw = b"".join(b"\0" + bytes(row) for row in pixels)

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    with open(path, "wb") as png:
        png.write(b"\x89PNG\r\n\x1a\n")
        png.write(chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 0, 0, 0, 0)))
        png.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        png.write(chunk(b"IEND", b""))


def smooth(t):
    return t * t * (3 - 2 * t)


def value_noise(rng, cells_x, cells_y=None):
    """Periodic value noise; returns f(x, y) in 0..1 for pixel coordinates."""
    cells_y = cells_y or cells_x
    grid = [[rng.random() for _ in range(cells_x)] for _ in range(cells_y)]

    def sample(x, y):
        gx = x * cells_x / SIZE
        gy = y * cells_y / SIZE
        x0, y0 = int(math.floor(gx)), int(math.floor(gy))
        fx, fy = smooth(gx - x0), smooth(gy - y0)
        x0 %= cells_x
        y0 %= cells_y
        x1, y1 = (x0 + 1) % cells_x, (y0 + 1) % cells_y
        top = grid[y0][x0] * (1 - fx) + grid[y0][x1] * fx
        bottom = grid[y1][x0] * (1 - fx) + grid[y1][x1] * fx
        return top * (1 - fy) + bottom * fy
    return sample


def fbm(rng, base, octaves, stretch_x=1):
    layers = [value_noise(rng, base * stretch_x * 2 ** i, base * 2 ** i) for i in range(octaves)]

    def sample(x, y):
        total = weight = 0.0
        amplitude = 1.0
        for layer in layers:
            total += layer(x, y) * amplitude
            weight += amplitude
            amplitude *= 0.5
        return total / weight
    return sample


def clamp(value):
    return max(0, min(255, int(round(value))))


def image(function):
    return [[clamp(function(x, y)) for x in range(SIZE)] for y in range(SIZE)]


def brick(rng):
    noise = fbm(rng, 8, 3)
    shades = {}
    height, width, mortar = 32, 64, 4

    def pixel(x, y):
        row = y // height
        shifted = (x + (width // 2 if row % 2 else 0)) % SIZE
        column = shifted // width
        u, v = shifted % width, y % height
        grain = (noise(x, y) - 0.5) * 34
        if u < mortar or v < mortar:
            return 190 + grain * 0.6
        tone = shades.setdefault((row, column), rng.uniform(-20, 20))
        bevel = 14 if v < mortar + 2 or u < mortar + 2 else -12 if v > height - 3 or u > width - 3 else 0
        return 128 + tone + grain + bevel
    return image(pixel)


def planks(rng):
    grain = fbm(rng, 4, 3)
    fine = value_noise(rng, 64, 128)
    height = 32
    joints = [rng.randrange(SIZE) for _ in range(SIZE // height)]
    tones = [rng.uniform(-16, 16) for _ in range(SIZE // height)]

    def pixel(x, y):
        plank = y // height
        v = y % height
        if v < 2 or (x - joints[plank]) % SIZE < 2:
            return 64
        # Whole periods across the tile keep the grain seamless vertically.
        rings = math.sin(2 * math.pi * 24 * y / SIZE + grain(x, y) * 30)
        return 132 + tones[plank] + rings * 18 + (fine(x, y) - 0.5) * 18 + (10 if v == 2 else 0)
    return image(pixel)


def pebbles(rng):
    stones = []
    for _ in range(150):
        rx, ry = rng.uniform(10, 21), rng.uniform(8, 16)
        stones.append((rng.uniform(0, SIZE), rng.uniform(0, SIZE), rx, ry, rng.uniform(0, math.pi),
                       rng.uniform(-22, 22)))
    noise = fbm(rng, 16, 2)
    grit = value_noise(rng, 128)

    def pixel(x, y):
        value = 78 + (grit(x, y) - 0.5) * 40
        # Later stones lie on top of earlier ones
        for cx, cy, rx, ry, angle, tone in stones:
            dx = (x - cx + SIZE / 2) % SIZE - SIZE / 2
            dy = (y - cy + SIZE / 2) % SIZE - SIZE / 2
            u = (dx * math.cos(angle) + dy * math.sin(angle)) / rx
            v = (-dx * math.sin(angle) + dy * math.cos(angle)) / ry
            r = u * u + v * v
            if r < 1:
                dome = math.sqrt(1 - r)
                light = -(dx * 0.35 + dy * 0.45) / max(rx, ry) * 22
                value = 104 + dome * 52 + light + tone + (noise(x, y) - 0.5) * 14
        return value
    return image(pixel)


def paper(rng):
    fibres = fbm(rng, 32, 3, stretch_x=4)
    cloud = fbm(rng, 4, 3)
    return image(lambda x, y: 128 + (fibres(x, y) - 0.5) * 30 + (cloud(x, y) - 0.5) * 22)


def weave(rng, thread):
    noise = value_noise(rng, 128)
    rows = [rng.uniform(-12, 12) for _ in range(SIZE // thread + 1)]
    cols = [rng.uniform(-12, 12) for _ in range(SIZE // thread + 1)]

    def pixel(x, y):
        tx, ty = x // thread, y // thread
        over = (tx + ty) % 2
        along = (y % thread) if over else (x % thread)
        profile = math.sin(math.pi * (along + 0.5) / thread) * 24 - 10
        tone = rows[ty] if over else cols[tx]
        return 128 + profile + tone + (noise(x, y) - 0.5) * 20
    return image(pixel)


def brushed_metal(rng):
    streaks = value_noise(rng, 4, 256)
    fine = value_noise(rng, 16, 256)
    sheen = fbm(rng, 2, 2)
    return image(lambda x, y: 128 + (streaks(x, y) - 0.5) * 34 + (fine(x, y) - 0.5) * 20 +
                 (sheen(x, y) - 0.5) * 26)


def slate(rng):
    layers = fbm(rng, 3, 5, stretch_x=3)
    cracks = fbm(rng, 8, 3)

    def pixel(x, y):
        crack = abs(cracks(x, y) - 0.5)
        return 128 + (layers(x, y) - 0.5) * 70 - (18 if crack < 0.012 else 0)
    return image(pixel)


def cork(rng):
    base = fbm(rng, 16, 3)
    specks = {(rng.randrange(SIZE), rng.randrange(SIZE)): rng.uniform(25, 60) for _ in range(900)}

    def pixel(x, y):
        value = 132 + (base(x, y) - 0.5) * 50
        value -= specks.get((x, y), 0)
        value -= specks.get(((x - 1) % SIZE, y), 0) * 0.45
        value -= specks.get((x, (y - 1) % SIZE), 0) * 0.45
        return value
    return image(pixel)


def concrete(rng):
    grit = value_noise(rng, 128)
    cloud = fbm(rng, 6, 4)
    pits = {(rng.randrange(SIZE), rng.randrange(SIZE)) for _ in range(260)}

    def pixel(x, y):
        value = 128 + (cloud(x, y) - 0.5) * 40 + (grit(x, y) - 0.5) * 26
        return value - 45 if (x, y) in pits else value
    return image(pixel)


TEXTURES = {
    "brick": brick,
    "planks": planks,
    "pebbles": pebbles,
    "paper": paper,
    "linen": lambda rng: weave(rng, 4),
    "canvas": lambda rng: weave(rng, 8),
    "brushed-metal": brushed_metal,
    "slate": slate,
    "cork": cork,
    "concrete": concrete,
}


def main():
    output = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    for index, (name, generator) in enumerate(TEXTURES.items()):
        write_png(os.path.join(output, name + ".png"), generator(random.Random(4200 + index)))
        print(name)


if __name__ == "__main__":
    main()
