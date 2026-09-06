#!/usr/bin/env python3
"""Generate the QR matrix embedded by the passport firmware."""

from __future__ import annotations

import argparse
from pathlib import Path
from urllib.parse import urlparse

import qrcode


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_URL = "https://github.com/EdenWu3698/folotoy-ai-passport-dashboard"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default=DEFAULT_URL, help="URL encoded in the QR code")
    parser.add_argument("--output", type=Path, default=ROOT / "main" / "passport_qr.h")
    args = parser.parse_args()

    parsed = urlparse(args.url)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise SystemExit("--url must be an absolute HTTP(S) URL")

    qr = qrcode.QRCode(
        version=None,
        error_correction=qrcode.constants.ERROR_CORRECT_M,
        box_size=1,
        border=0,
    )
    qr.add_data(args.url)
    qr.make(fit=True)
    matrix = qr.get_matrix()
    size = len(matrix)
    if size > 64:
        raise SystemExit(f"URL is too long for the uint64_t row format ({size} modules)")

    rows = []
    for row in matrix:
        value = 0
        for bit in row:
            value = (value << 1) | int(bit)
        rows.append(value)

    c_url = args.url.replace("\\", "\\\\").replace('"', '\\"')
    args.output.write_text(
        "#pragma once\n\n"
        "#include <stdint.h>\n\n"
        f"#define PASSPORT_QR_SIZE {size}\n"
        f"#define PASSPORT_QR_URL \"{c_url}\"\n\n"
        "static const uint64_t passport_qr_rows[PASSPORT_QR_SIZE] = {\n"
        + "".join(f"    UINT64_C(0x{value:x}),\n" for value in rows)
        + "};\n",
        encoding="utf-8",
    )
    print(f"generated {args.output} ({size}x{size}) -> {args.url}")


if __name__ == "__main__":
    main()
