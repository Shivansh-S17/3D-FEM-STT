"""Render recorded PowerShell output as reviewable PNG evidence."""

from pathlib import Path
import re
import textwrap

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "evidence"
VISUALS = ROOT / "visuals"


def font(size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.load_default(size=size)


def render(log_name: str, output_name: str, title: str, command: str) -> None:
    raw = (EVIDENCE / log_name).read_text(encoding="utf-8-sig", errors="replace")
    lines = [re.sub(r"\x1b\[[0-9;]*m", "", line).rstrip() for line in raw.splitlines()]
    wrapped = []
    for line in lines:
        wrapped.extend(textwrap.wrap(line, width=127, break_long_words=True) or [""])

    width = 1840
    line_height = 27
    height = 230 + line_height * len(wrapped) + 74
    image = Image.new("RGB", (width, height), "#0b1220")
    draw = ImageDraw.Draw(image)

    draw.rounded_rectangle((30, 25, width - 30, height - 25), radius=20,
                           fill="#101b2b", outline="#3a526b", width=2)
    draw.rounded_rectangle((31, 26, width - 31, 92), radius=20, fill="#1d2c3e")
    draw.rectangle((31, 68, width - 31, 92), fill="#1d2c3e")
    for x, color in ((65, "#ff665e"), (93, "#ffbd2e"), (121, "#28c840")):
        draw.ellipse((x - 9, 51, x + 9, 69), fill=color)
    draw.text((155, 43), title, font=font(26), fill="#e7eef8")
    draw.text((62, 113), "PS submissions/week1/code>",
              font=font(22), fill="#80c7f4")
    draw.text((62, 146), command, font=font(24), fill="#f3d06c")
    draw.line((60, 189, width - 60, 189), fill="#3a526b", width=2)

    y = 207
    for line in wrapped:
        color = "#a5f0bd" if "succeeded" in line.lower() or "package ready" in line.lower() else "#d9e3ee"
        if "Could NOT find WrapVulkanHeaders" in line:
            color = "#f1c27d"
        draw.text((62, y), line, font=font(21), fill=color)
        y += line_height

    draw.text((62, height - 63), f"Recorded output: evidence/{log_name}  •  01 Oct 2026",
              font=font(18), fill="#8da3ba")
    image.save(VISUALS / output_name, optimize=True)


render("build-2026-10-01.log", "screenshots/build-evidence.png",
       "WEEK 1  /  CLEAN RELEASE BUILD", r".\build.ps1 -Config Release")
render("package-2026-10-01.log", "screenshots/distribution-evidence.png",
       "WEEK 1  /  STANDALONE DISTRIBUTION", r".\packaging\package-release.ps1")
