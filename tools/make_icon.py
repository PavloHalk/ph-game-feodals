"""Generates res/feodals.ico: a small board of colored cells (16, 32, 48 px)."""
import struct
import os

PALETTE = [(220, 40, 40), (40, 90, 220), (30, 160, 60), (245, 140, 0)]
# 4x4 board: player index, or -1 for an empty cell.
BOARD = [
    [0, 0, 0, -1],
    [0, 1, 0, 2],
    [0, 0, 0, 2],
    [3, -1, 2, 2],
]


def render(size):
    grid = (230, 230, 230)
    empty = (255, 255, 255)
    border = (70, 74, 82)
    cell = size / 4.0
    pixels = []
    for y in range(size):
        row = []
        for x in range(size):
            if x == 0 or y == 0 or x == size - 1 or y == size - 1:
                row.append(border)
                continue
            cx, cy = int(x / cell), int(y / cell)
            line = size >= 32 and (x % int(cell) == 0 or y % int(cell) == 0)
            owner = BOARD[cy][cx]
            if line:
                row.append(grid)
            else:
                row.append(PALETTE[owner] if owner >= 0 else empty)
        pixels.append(row)
    return pixels


def bmp_entry(size):
    pixels = render(size)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    data = bytearray()
    for y in reversed(range(size)):
        for r, g, b in pixels[y]:
            data += bytes((b, g, r, 255))
    mask_row = ((size + 31) // 32) * 4
    data += bytes(mask_row * size)
    return header + bytes(data)


def main():
    sizes = [16, 32, 48]
    images = [bmp_entry(s) for s in sizes]
    out = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    for s, img in zip(sizes, images):
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    for img in images:
        out += img
    path = os.path.join(os.path.dirname(__file__), "..", "res", "feodals.ico")
    with open(path, "wb") as f:
        f.write(out)


if __name__ == "__main__":
    main()
