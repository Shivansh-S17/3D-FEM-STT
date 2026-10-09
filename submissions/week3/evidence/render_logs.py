"""Make readable visual copies of the recorded Week 3 text logs.

These PNGs are rendered from the log files; they are not terminal screenshots.
"""

from pathlib import Path
import re
import textwrap

from PIL import Image, ImageDraw, ImageFont


PACKET = Path(__file__).resolve().parents[1]



def render(log_name: str, image_name: str, title: str, command: str) -> None:
    raw = (PACKET / "evidence" / log_name).read_text(encoding="utf-8-sig", errors="replace")
    lines = []
    for line in raw.splitlines():
        line = re.sub(r"\x1b\[[0-9;]*m", "", line).strip("\r")
        lines.extend(textwrap.wrap(line, width=106, break_long_words=True) or [""])

    width, line_height = 1510, 27
    height = max(380, 238 + len(lines) * line_height + 55)
    image = Image.new("RGB", (width, height), "#0c1422")
    draw = ImageDraw.Draw(image)
    mono = ImageFont.load_default(size=20)
    heading = ImageFont.load_default(size=27)
    small = ImageFont.load_default(size=17)
    draw.rounded_rectangle((20, 18, width - 20, height - 18), radius=17,
                           fill="#111e30", outline="#3a526d", width=2)
    draw.rectangle((21, 19, width - 21, 79), fill="#20314a")
    for x, color in ((53, "#fa6760"), (80, "#fac14a"), (107, "#43cb78")):
        draw.ellipse((x - 8, 45, x + 8, 61), fill=color)
    draw.text((142, 37), title, font=heading, fill="#ecf4ff")
    draw.text((48, 99), "Week 3 source snapshot · 8 October 2026", font=mono, fill="#8fcef6")
    draw.text((48, 136), command, font=mono, fill="#f5d276")
    draw.line((48, 180, width - 48, 180), fill="#465b71", width=2)
    y = 197
    for line in lines:
        lowered = line.lower()
        color = "#a4edc2" if "passed" in lowered or "linking cxx executable" in lowered else "#dfe8f1"
        draw.text((48, y), line, font=mono, fill=color)
        y += line_height
    draw.text((48, height - 51), f"Rendered from evidence/{log_name} · not a live terminal screenshot",
              font=small, fill="#92a7c0")
    image.save(PACKET / "visuals" / image_name, optimize=True)


render("build-2026-10-08.log", "07-clean-build.png", "WEEK 3 / FRESH MSVC BUILD",
       ".\build.ps1 -Config Release")
render("ctest-2026-10-08.log", "08-ctest-results.png", "WEEK 3 / CTEST RESULTS",
       r"ctest --test-dir .\build\release --output-on-failure")
render("native-gui-2026-10-08.log", "09-native-gui-checks.png", "WEEK 3 / NATIVE GUI CHECKS",
       "FemGuiTests.exe [4 named native GUI cases]")
