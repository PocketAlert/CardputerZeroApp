#!/usr/bin/env python3
"""Cut Noto Sans CJK down to the characters the translated UI uses.

The device has no CJK font unless fonts-noto-cjk is installed (~90 MB), so the
app ships this subset (~100s of KB) to draw its own ja/ko/zh strings. Rerun it
after the translations change:

    pip install fonttools
    curl -LO https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf
    tools/subset-cjk-font.py NotoSansCJKsc-Regular.otf
"""

import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
I18N = ROOT / "assets" / "i18n"
OUT = ROOT / "assets" / "fonts" / "NotoSansCJK-UI.otf"
# Native language names appear in the Language picker whatever the UI language is.
ALWAYS = "日本語한국어简体中文"


def strings(node):
    if isinstance(node, str):
        yield node
    elif isinstance(node, dict):
        for value in node.values():
            yield from strings(value)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    chars = set(ALWAYS)
    for locale in ("ja", "ko", "zh"):
        for file in sorted((I18N / locale).glob("*.json")):
            for text in strings(json.loads(file.read_text(encoding="utf-8"))):
                chars.update(c for c in text if ord(c) > 0x2E7F)
    text_file = OUT.with_suffix(".chars.txt")
    text_file.write_text("".join(sorted(chars)), encoding="utf-8")
    try:
        subprocess.run(
            [
                "pyftsubset", sys.argv[1],
                f"--text-file={text_file}",
                f"--output-file={OUT}",
                "--layout-features=*",
                "--no-hinting",
                "--desubroutinize",
                "--name-IDs=*",
                "--name-legacy",
            ],
            check=True,
        )
    finally:
        text_file.unlink(missing_ok=True)
    print(f"{OUT.relative_to(ROOT)}: {len(chars)} characters, {OUT.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
