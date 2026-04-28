"""PPM (P6) reader and BMP writer.

The Cinema 4D bridge needs to display the rendered image
in its preview dialog. The renderer saves PPM (`Image::save_ppm`
in `src/image/Image.cpp`); Cinema 4D's `BaseBitmap.InitWith`
reliably loads BMP / PNG / JPG / TIFF / EXR / ... but PPM
support is unusual. To stay portable across C4D versions the
bridge converts PPM -> BMP and hands the BMP to the bitmap
loader.

Both the reader and the writer are pure Python with no Cinema
4D imports, so the standalone test harness can exercise them
under stock python3. The .pyp plugin imports them inside
Cinema 4D after a successful render.

BMP is preferred over PNG here because:
  - the format is trivial (no compression, no CRC chunks);
  - every Cinema 4D version since the SDK first supported
    bitmaps loads BMPs;
  - the byte layout is easy to spot-check in tests.

Per the project's dependency rules
(`docs/MODULE_MAP.md` + `integrations/c4d/README.md`):
nothing under `src/` may import this module.
"""

from __future__ import annotations

import os
import struct
from typing import Tuple

# PPM P6 magic bytes. The renderer always writes P6 (binary
# RGB) - `Image::save_ppm` uses `out << "P6\\n"`. ASCII PPM
# (`P3`) is not produced by the renderer and is intentionally
# not supported here.
_PPM_P6_MAGIC = b"P6"


class PpmDecodeError(Exception):
    """Raised when a `.ppm` file cannot be decoded as P6.

    The .pyp plugin catches this and surfaces the message in
    the preview dialog's response text area so a Python
    exception never escapes into the C4D plugin host.
    """
    pass


def read_ppm_p6(path: str) -> Tuple[int, int, bytes]:
    """Read a binary PPM (`P6`) file from disk.

    Returns `(width, height, rgb_bytes)` where `rgb_bytes` is
    a tightly-packed `width * height * 3`-byte RGB buffer
    (top-left origin, row-major). Pixels are 8-bit per channel
    regardless of the PPM's `maxval`: values are scaled to the
    `[0, 255]` range so downstream encoders can assume 8-bit
    samples without per-file branching.

    Raises `PpmDecodeError` for any malformed input (wrong
    magic, missing fields, truncated pixel data, unsupported
    `maxval`).
    """
    with open(path, "rb") as f:
        data = f.read()
    return decode_ppm_p6(data)


def decode_ppm_p6(data: bytes) -> Tuple[int, int, bytes]:
    """In-memory variant of `read_ppm_p6`. Useful for tests:
    the harness can pass a bytes literal without round-tripping
    through the filesystem.
    """
    if not data.startswith(_PPM_P6_MAGIC):
        raise PpmDecodeError("not a P6 PPM file")

    # Walk the header. PPM tokens are whitespace-separated;
    # comments (lines starting with '#') are stripped before
    # tokenisation. We need exactly four tokens
    # (P6 / width / height / maxval), then exactly ONE
    # whitespace byte separates the header from the binary
    # body.
    pos = 0
    n   = len(data)

    def is_ws(b):
        return b in (0x20, 0x09, 0x0A, 0x0B, 0x0C, 0x0D)

    def next_token():
        nonlocal pos
        while pos < n:
            b = data[pos]
            if is_ws(b):
                pos += 1
                continue
            if b == 0x23:  # '#'
                # Skip to end of line.
                while pos < n and data[pos] != 0x0A:
                    pos += 1
                continue
            break
        start = pos
        while pos < n and not is_ws(data[pos]) and data[pos] != 0x23:
            pos += 1
        if start == pos:
            raise PpmDecodeError("unexpected end of header")
        return data[start:pos]

    magic_tok = next_token()
    if magic_tok != _PPM_P6_MAGIC:
        raise PpmDecodeError("first token must be 'P6'")

    try:
        width  = int(next_token())
        height = int(next_token())
        maxval = int(next_token())
    except ValueError:
        raise PpmDecodeError("non-numeric header field")

    if width <= 0 or height <= 0:
        raise PpmDecodeError("width and height must be positive")
    if maxval <= 0 or maxval > 65535:
        raise PpmDecodeError("maxval must be in [1, 65535]")

    # Exactly one whitespace byte separates header from body.
    if pos >= n or not is_ws(data[pos]):
        raise PpmDecodeError("missing whitespace after header")
    pos += 1

    bytes_per_sample = 1 if maxval < 256 else 2
    expected = width * height * 3 * bytes_per_sample
    body = data[pos:pos + expected]
    if len(body) != expected:
        raise PpmDecodeError(
            "body short: expected {0} bytes, got {1}"
            .format(expected, len(body)))

    if bytes_per_sample == 1 and maxval == 255:
        # Most common case (the renderer's output): pixels
        # already in 0..255 form, no rescale needed.
        return (width, height, body)

    # Scale every sample to 0..255 so downstream encoders can
    # assume 8-bit. Tight inner loop: pull the values, divide,
    # cap at 255 so a slightly out-of-range pixel doesn't
    # overflow the byte slot.
    out = bytearray(width * height * 3)
    inv = 255.0 / float(maxval)
    if bytes_per_sample == 1:
        for i in range(len(body)):
            v = int(body[i] * inv + 0.5)
            out[i] = v if v <= 255 else 255
    else:
        # 16-bit big-endian per the PPM spec.
        n_samples = width * height * 3
        for i in range(n_samples):
            hi = body[i * 2]
            lo = body[i * 2 + 1]
            sample = (hi << 8) | lo
            v = int(sample * inv + 0.5)
            out[i] = v if v <= 255 else 255
    return (width, height, bytes(out))


# ---------------------------------------------------------------------------
# BMP writer (24-bit BI_RGB, bottom-up, BGR pixels, 4-byte row alignment).
# ---------------------------------------------------------------------------
#
# BMP is the simplest format the C4D bitmap loader reliably
# accepts. The encoder below writes the canonical Windows
# layout: 14-byte BITMAPFILEHEADER + 40-byte BITMAPINFOHEADER
# + pixel rows from bottom to top, each padded to a multiple
# of 4 bytes. No compression, no colour table, no extra
# headers.

_BMP_FILE_HEADER_SIZE = 14
_BMP_INFO_HEADER_SIZE = 40
_BMP_PIXEL_OFFSET     = _BMP_FILE_HEADER_SIZE + _BMP_INFO_HEADER_SIZE  # 54


def encode_bmp_24(width: int, height: int, rgb_bytes: bytes) -> bytes:
    """Encode an 8-bit RGB image to a 24-bit BMP byte string.

    `rgb_bytes` is `width * height * 3` bytes in row-major,
    top-left-origin RGB order (the layout `read_ppm_p6` returns).
    The output is a complete `.bmp` file body suitable for
    `open(path, 'wb').write(...)`.
    """
    if width <= 0 or height <= 0:
        raise ValueError("width and height must be positive")
    if len(rgb_bytes) != width * height * 3:
        raise ValueError(
            "rgb_bytes length {0} does not match width*height*3 = {1}"
            .format(len(rgb_bytes), width * height * 3))

    # BMP rows are 4-byte aligned. Compute padding per row.
    row_bytes      = width * 3
    padded_row     = (row_bytes + 3) & ~3
    pad            = padded_row - row_bytes
    image_size     = padded_row * height
    file_size      = _BMP_PIXEL_OFFSET + image_size

    out = bytearray(file_size)

    # BITMAPFILEHEADER.
    struct.pack_into("<2sIHHI", out, 0,
                     b"BM", file_size, 0, 0, _BMP_PIXEL_OFFSET)
    # BITMAPINFOHEADER.
    struct.pack_into("<IiiHHIIiiII", out, _BMP_FILE_HEADER_SIZE,
                     _BMP_INFO_HEADER_SIZE,
                     width, height,
                     1,            # planes
                     24,           # bits per pixel
                     0,            # BI_RGB (no compression)
                     image_size,
                     2835, 2835,   # 72 DPI in pixels/metre
                     0, 0)

    # Pixel rows: BMP is bottom-up; PPM is top-down. Reverse
    # the row order on the way in. Within each row, swap RGB
    # to BGR.
    src_stride = row_bytes
    dst_off    = _BMP_PIXEL_OFFSET
    for y in range(height):
        src_y = height - 1 - y
        src_off = src_y * src_stride
        for x in range(width):
            r = rgb_bytes[src_off + 0]
            g = rgb_bytes[src_off + 1]
            b = rgb_bytes[src_off + 2]
            out[dst_off + 0] = b
            out[dst_off + 1] = g
            out[dst_off + 2] = r
            src_off += 3
            dst_off += 3
        # Row padding bytes are zero; bytearray() is
        # zero-initialised, so we just skip past them.
        dst_off += pad

    return bytes(out)


def convert_ppm_to_bmp(ppm_path: str,
                       bmp_path: str = "") -> str:
    """Read a PPM file from disk, write a BMP next to it (or
    at the supplied `bmp_path`), return the absolute BMP path.

    The default destination is the PPM's own path with the
    extension swapped to `.bmp`. Parent directories are
    created if missing.
    """
    width, height, rgb = read_ppm_p6(ppm_path)
    out_path = bmp_path
    if not out_path:
        stem, _ = os.path.splitext(ppm_path)
        out_path = stem + ".bmp"
    out_path = os.path.abspath(out_path)
    parent   = os.path.dirname(out_path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent, exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(encode_bmp_24(width, height, rgb))
    return out_path
