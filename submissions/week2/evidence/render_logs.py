"""Render the recorded build and CTest logs as readable PNG evidence."""

from pathlib import Path
import re
import textwrap

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]


def render(log_name: str, image_name: str, title: str, command: str) -> None:
    source = ROOT / "evidence" / log_name
    lines = source.read_text(encoding="utf-8-sig", errors="replace").splitlines()
    wrapped = []
    for line in lines:
        clean = re.sub(r"\x1b\[[0-9;]*m", "", line).rstrip()
        wrapped += textwrap.wrap(clean, 125, break_long_words=True) or [""]

    width = 1820
    line_height = 27
    height = 242 + len(wrapped) * line_height + 62
    im = Image.new("RGB", (width, height), "#0c1422")
    draw = ImageDraw.Draw(im)
    mono = ImageFont.load_default(size=21)
    heading = ImageFont.load_default(size=28)
    small = ImageFont.load_default(size=18)
    draw.rounded_rectangle((26, 20, width - 26, height - 20), radius=18,
                           fill="#111e30", outline="#3a526d", width=2)
    draw.rounded_rectangle((27, 21, width - 27, 88), radius=18, fill="#20314a")
    draw.rectangle((27, 67, width - 27, 88), fill="#20314a")
    for x, color in ((60, "#fa6760"), (87, "#fac14a"), (114, "#43cb78")):
        draw.ellipse((x - 9, 48, x + 9, 66), fill=color)
    draw.text((150, 41), title, font=heading, fill="#ecf4ff")
    draw.text((56, 109), "PS submissions/week2/code>",
              font=mono, fill="#8fcef6")
    draw.text((56, 144), command, font=mono, fill="#f5d276")
    draw.line((55, 188, width - 55, 188), fill="#465b71", width=2)
    y = 207
    for line in wrapped:
        lower = line.lower()
        color = "#a4edc2" if "passed" in lower or "build succeeded" in lower else "#dfe8f1"
        if "could not find wrapvulkanheaders" in lower:
            color = "#f7cb86"
        draw.text((56, y), line, font=mono, fill=color)
        y += line_height
    draw.text((56, height - 61), f"Recorded output: evidence/{log_name}  |  01 Oct 2026",
              font=small, fill="#92a7c0")
    im.save(ROOT / "visuals" / image_name, optimize=True)


render("build-2026-10-01.log", "06-clean-build.png", "WEEK 2 / CLEAN RELEASE BUILD",
       r".\build.ps1 -Config Release")
render("ctest-2026-10-01.log", "07-ctest-results.png", "WEEK 2 / CTEST RESULTS",
       r"ctest --test-dir .\build\release --output-on-failure")
