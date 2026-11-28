#!/usr/bin/env python3
"""Record a dual-monitor (landscape + portrait) showcase video for Karta.

Launches an isolated headless Hyprland instance (leaving the outer desktop
untouched), populates realistic developer windows across a landscape monitor
(auto row mode) and a portrait monitor (auto col mode), records both outputs
via wf-recorder, and composites them into a 1920x1080 studio MP4 plus an
optimized README preview GIF.
"""
import argparse
import json
import math
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import tempfile
import time
from PIL import Image, ImageDraw, ImageFilter, ImageFont

REPO_ROOT = Path(__file__).resolve().parents[1]

# Studio karta layout (1920x1080)
FRAME_W, FRAME_H = 1920, 1080
MON_L_W, MON_L_H = 1280, 720
MON_P_W, MON_P_H = 492, 800
MON_L_X, MON_L_Y = 56, 172
MON_P_X, MON_P_Y = 1372, 132
FPS = 60

FONT_BOLD = "/usr/share/fonts/TTF/JetBrainsMono-ExtraBold.ttf"
FONT_SEMI = "/usr/share/fonts/TTF/JetBrainsMono-SemiBold.ttf"
FONT_REG = "/usr/share/fonts/TTF/JetBrainsMono-Medium.ttf"


def load_font(path: str, size: int) -> ImageFont.FreeTypeFont:
    for candidate in (path, FONT_BOLD, FONT_SEMI, "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"):
        if Path(candidate).is_file():
            return ImageFont.truetype(candidate, size)
    return ImageFont.load_default()


def generate_wallpaper(path: Path, width: int, height: int, seed_shift: float = 0.0) -> None:
    """Create a rich dark-aurora gradient wallpaper with subtle dot grid."""
    base = Image.new("RGB", (width, height), (13, 16, 27))
    px = base.load()
    for y in range(height):
        ty = y / max(1, height - 1)
        r = int(12 + 12 * ty)
        g = int(15 + 14 * ty)
        b = int(28 + 22 * ty)
        for x in range(width):
            px[x, y] = (r, g, b)

    glow = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(glow)
    blobs = [
        (width * (0.18 + seed_shift * 0.1), height * 0.22, int(width * 0.42), (56, 130, 246, 105)),
        (width * (0.82 - seed_shift * 0.1), height * 0.28, int(width * 0.38), (139, 92, 246, 95)),
        (width * 0.55, height * 0.80, int(width * 0.45), (6, 182, 212, 80)),
        (width * 0.25, height * 0.78, int(width * 0.30), (236, 72, 153, 55)),
    ]
    for cx, cy, rad, color in blobs:
        draw.ellipse((cx - rad, cy - rad, cx + rad, cy + rad), fill=color)
    glow = glow.filter(ImageFilter.GaussianBlur(radius=max(width, height) // 7))

    combined = Image.alpha_composite(base.convert("RGBA"), glow)

    # Subtle modern dot grid
    dots = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    dd = ImageDraw.Draw(dots)
    step = 28
    for y in range(step // 2, height, step):
        for x in range(step // 2, width, step):
            dd.point((x, y), fill=(180, 198, 255, 22))
    combined = Image.alpha_composite(combined, dots)
    combined.convert("RGB").save(path, "PNG")


def generate_studio_backdrop(path: Path) -> None:
    """Create the 1920x1080 studio frame with monitor bezels and header branding."""
    img = Image.new("RGBA", (FRAME_W, FRAME_H), (11, 14, 23, 255))
    draw = ImageDraw.Draw(img)

    # Subtle radial ambient glow behind monitors
    glow = Image.new("RGBA", (FRAME_W, FRAME_H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse((80, 120, 1400, 960), fill=(59, 130, 246, 26))
    gd.ellipse((1250, 80, 1920, 980), fill=(168, 85, 247, 26))
    glow = glow.filter(ImageFilter.GaussianBlur(radius=90))
    img = Image.alpha_composite(img, glow)
    draw = ImageDraw.Draw(img)

    # Subtle horizontal separator under top bar
    draw.line((56, 86, FRAME_W - 56, 86), fill=(148, 163, 184, 38), width=1)

    # Top-left title & tagline
    f_title = load_font(FONT_BOLD, 30)
    f_sub = load_font(FONT_REG, 15)
    f_label = load_font(FONT_SEMI, 13)

    # Accent dot + title
    draw.rounded_rectangle((56, 30, 66, 62), radius=5, fill=(137, 180, 250, 255))
    draw.text((80, 28), "Karta", font=f_title, fill=(241, 245, 249, 255))
    draw.text((284, 38), "Scrolling Lanes · Card Stacks · Multi-Monitor Karta · Overview", font=f_sub, fill=(148, 163, 184, 230))

    # Monitor drop shadows & sleek frames
    def draw_monitor_frame(x: int, y: int, w: int, h: int, label: str, accent: tuple[int, int, int]) -> None:
        shadow = Image.new("RGBA", (FRAME_W, FRAME_H), (0, 0, 0, 0))
        sd = ImageDraw.Draw(shadow)
        sd.rounded_rectangle((x - 8, y - 4, x + w + 8, y + h + 14), radius=18, fill=(0, 0, 0, 165))
        shadow = shadow.filter(ImageFilter.GaussianBlur(radius=18))
        nonlocal img, draw
        img = Image.alpha_composite(img, shadow)
        draw = ImageDraw.Draw(img)

        # Outer thin bezel
        draw.rounded_rectangle((x - 3, y - 3, x + w + 3, y + h + 3), radius=12, fill=(30, 36, 54, 255), outline=(*accent, 115), width=2)
        # Inner cutout dark fill
        draw.rectangle((x, y, x + w, y + h), fill=(12, 15, 25, 255))

        # Pill badge above monitor
        draw.ellipse((x + 2, y - 24, x + 10, y - 16), fill=(*accent, 255))
        draw.text((x + 18, y - 28), label, font=f_label, fill=(186, 198, 224, 235))

    draw_monitor_frame(MON_L_X, MON_L_Y, MON_L_W, MON_L_H, "MONITOR 1  ·  LANDSCAPE 1280×720  ·  AUTO ROW", (137, 180, 250))
    draw_monitor_frame(MON_P_X, MON_P_Y, MON_P_W, MON_P_H, "MONITOR 2  ·  PORTRAIT 492×800  ·  AUTO COL", (203, 166, 247))

    img.convert("RGB").save(path, "PNG")


def generate_hud_overlay(
    path: Path,
    chapter_idx: int,
    chapter_total: int,
    chapter_title: str,
    keys: str,
    caption: str,
    progress: float,
) -> None:
    """Create a transparent 1920x1080 HUD overlay for a chapter/beat."""
    img = Image.new("RGBA", (FRAME_W, FRAME_H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    f_chap = load_font(FONT_SEMI, 15)
    f_key = load_font(FONT_BOLD, 20)
    f_cap = load_font(FONT_SEMI, 20)

    # Top-right chapter pill + progress dots
    chap_text = f"{chapter_idx:02d}/{chapter_total:02d}  {chapter_title.upper()}"
    tb = draw.textbbox((0, 0), chap_text, font=f_chap)
    tw, th = tb[2] - tb[0], tb[3] - tb[1]
    px2 = FRAME_W - 56
    px1 = px2 - tw - 34
    draw.rounded_rectangle((px1, 30, px2, 62), radius=16, fill=(30, 38, 60, 225), outline=(137, 180, 250, 120), width=1)
    draw.text((px1 + 17, 37), chap_text, font=f_chap, fill=(205, 214, 244, 255))

    # Chapter dots to the left of the chapter pill
    dot_gap = 14
    dots_right = px1 - 20
    for i in range(chapter_total):
        cx = dots_right - (chapter_total - 1 - i) * dot_gap
        cy = 46
        if i + 1 == chapter_idx:
            draw.ellipse((cx - 4, cy - 4, cx + 4, cy + 4), fill=(137, 180, 250, 255))
        elif i + 1 < chapter_idx:
            draw.ellipse((cx - 3, cy - 3, cx + 3, cy + 3), fill=(137, 180, 250, 130))
        else:
            draw.ellipse((cx - 3, cy - 3, cx + 3, cy + 3), fill=(100, 116, 139, 100))

    # Bottom centered action + keystroke pill
    kb = draw.textbbox((0, 0), keys, font=f_key)
    kw, kh = kb[2] - kb[0], kb[3] - kb[1]
    cb = draw.textbbox((0, 0), caption, font=f_cap)
    cw, ch = cb[2] - cb[0], cb[3] - cb[1]

    key_box_w = kw + 32
    total_w = key_box_w + 22 + cw + 44
    pill_h = 58
    pill_x1 = (FRAME_W - total_w) // 2
    pill_y1 = 984
    pill_x2 = pill_x1 + total_w
    pill_y2 = pill_y1 + pill_h

    draw.rounded_rectangle(
        (pill_x1, pill_y1, pill_x2, pill_y2),
        radius=29,
        fill=(22, 27, 44, 238),
        outline=(137, 180, 250, 135),
        width=2,
    )
    # Keycap sub-pill
    kx1 = pill_x1 + 12
    ky1 = pill_y1 + 9
    kx2 = kx1 + key_box_w
    ky2 = pill_y2 - 9
    draw.rounded_rectangle(
        (kx1, ky1, kx2, ky2),
        radius=20,
        fill=(49, 60, 96, 255),
        outline=(166, 200, 255, 190),
        width=1,
    )
    draw.text((kx1 + 16, ky1 + (ky2 - ky1 - kh) // 2 - 2), keys, font=f_key, fill=(241, 245, 249, 255))
    draw.text((kx2 + 20, pill_y1 + (pill_h - ch) // 2 - 2), caption, font=f_cap, fill=(226, 232, 240, 255))

    # Subtle bottom timeline progress bar
    bar_x1, bar_x2, bar_y = 56, FRAME_W - 56, FRAME_H - 12
    draw.rounded_rectangle((bar_x1, bar_y, bar_x2, bar_y + 4), radius=2, fill=(51, 65, 85, 120))
    fill_x2 = int(bar_x1 + (bar_x2 - bar_x1) * max(0.02, min(1.0, progress)))
    draw.rounded_rectangle((bar_x1, bar_y, fill_x2, bar_y + 4), radius=2, fill=(137, 180, 250, 220))

    img.save(path, "PNG")


PANEL_SCRIPT = r'''#!/usr/bin/env python3
import os, sys, time, math

kind = sys.argv[1]
ESC = "\033["
RESET = ESC + "0m"
BOLD = ESC + "1m"
DIM = ESC + "2m"

def fg(r, g, b): return f"\033[38;2;{r};{g};{b}m"
def bg(r, g, b): return f"\033[48;2;{r};{g};{b}m"

PALETTES = {
    "editor":       ((137, 180, 250), " NEOVIM ", "src/layout/karta/core.cpp",   "C++23"),
    "metrics":      ((148, 226, 213), " SYSTEM ", "Karta Telemetry",         "60 Hz"),
    "fetch":        ((203, 166, 247), " SHELL  ", "karta · Arch Linux",      "fish"),
    "git":          ((250, 179, 135), "  GIT   ", "karta · main",            "HEAD"),
    "docs":         ((245, 194, 231), "  DOCS  ", "Karta Model",                 "md"),
    "logs":         ((116, 199, 236), "  IPC   ", "Event Stream",                 "sock2"),
    "karta2_code": ((166, 227, 161), "KARTA 2", "overview/render/draw.cpp",     "GL"),
    "karta2_test": ((249, 226, 175), " TESTS  ", "Regression Suite",             "ctest"),
}

accent, badge, title, right = PALETTES[kind]
ar, ag, ab = accent

def trunc(s, n):
    if n <= 0:
        return ""
    return s if len(s) <= n else (s[: max(1, n - 1)] + "…")

def render_screen(lines_out, rows):
    # Render exact screen buffer without trailing newline on last line so nothing scrolls.
    trimmed = lines_out[:rows]
    win_title = f"{badge.strip()} · {title}"
    sys.stdout.write(f"\033]2;{win_title}\007\033[?25l\033[H\033[2J" + "\r\n".join(trimmed))
    sys.stdout.flush()

def make_header(cols):
    max_title = max(6, cols - len(badge) - len(right) - 4)
    t = trunc(title, max_title)
    pad = max(1, cols - len(badge) - len(t) - len(right) - 2)
    h1 = f"{bg(ar,ag,ab)}{fg(17,17,27)}{BOLD}{badge}{RESET} {fg(ar,ag,ab)}{BOLD}{t}{RESET}{' ' * pad}{DIM}{fg(166,173,200)}{right}{RESET}"
    h2 = f"{fg(ar,ag,ab)}{'━' * cols}{RESET}"
    return [h1, h2]

def render_editor(cols, rows):
    out = make_header(cols) + [""]
    code_lines = [
        (142, "namespace karta::karta {", "kw"),
        (143, "", ""),
        (144, "void KartaController::activate(KartaId id) {", "fn"),
        (145, "  auto* karta = repo_.findKarta(id);", "code"),
        (146, "  if (!karta || id == activeId_) return;", "kw"),
        (147, "", ""),
        (148, "  // Switch all monitors in lockstep", "cmt"),
        (149, "  for (auto& [mon, ws] : karta->members()) {", "code"),
        (150, "    runtime_.activateWorkspace(mon, ws);", "call"),
        (151, "    syncOrientation(mon); // auto row/col", "cmt"),
        (152, "  }", "code"),
        (153, "", ""),
        (154, "  restoreFocusMemory(*karta);", "call"),
        (155, "  minimap_.flashKartaMap(id);", "call"),
        (156, "}", "fn"),
        (157, "", ""),
        (158, "bool Stack::isCardDeck() const noexcept {", "fn"),
        (159, "  return members_.size() >= 3 && !expanded_;", "kw"),
        (160, "}", "fn"),
    ]
    avail = max(1, rows - len(out) - 2)
    max_code = max(8, cols - 8)
    for ln, text, tok in code_lines[:avail]:
        t = trunc(text, max_code)
        num = f"{fg(108,112,134)}{ln:4d} │ {RESET}"
        color = {
            "cmt": fg(127, 132, 156),
            "kw": fg(203, 166, 247),
            "fn": fg(137, 180, 250) + BOLD,
            "call": fg(166, 227, 161),
        }.get(tok, fg(205, 214, 244))
        out.append(f"{num}{color}{t}{RESET}")
    out.append("")
    status = trunc("  core.cpp  ·  +18 -4  ·  Ln 150", max(8, cols - 10))
    out.append(f"{bg(49,50,68)}{fg(137,180,250)}{BOLD} NORMAL {RESET}{bg(30,30,46)}{fg(205,214,244)}{status}{RESET}")
    return out

def render_metrics(cols, rows, tick):
    out = make_header(cols) + [""]
    bar_w = max(8, min(30, cols - 22))
    def bar(label, val, col_rgb):
        filled = int(bar_w * val)
        r, g, b = col_rgb
        bstr = f"{fg(r,g,b)}{'█' * filled}{fg(69,71,90)}{'░' * (bar_w - filled)}{RESET}"
        return f"  {fg(186,194,222)}{label:<10}{RESET} {bstr} {fg(r,g,b)}{BOLD}{int(val*100):3d}%{RESET}"

    out.append(f"  {fg(148,226,213)}{BOLD}COMPOSITOR TELEMETRY{RESET}")
    out.append("")
    out.append(bar("CPU Core 0", 0.34 + 0.07 * math.sin(tick * 0.7), (148, 226, 213)))
    out.append(bar("CPU Core 1", 0.28 + 0.08 * math.cos(tick * 0.9), (137, 180, 250)))
    out.append(bar("GPU Render", 0.42 + 0.05 * math.sin(tick * 0.5 + 1.0), (203, 166, 247)))
    out.append(bar("RAM Usage",  0.31, (166, 227, 161)))
    out.append("")
    out.append(f"  {fg(148,226,213)}{BOLD}ACTIVE LAYOUT STATE{RESET}")
    kv = [
        ("Engine",     "Karta v0.57 (karta)", (205,214,244)),
        ("Monitors",   "Landscape row + Portrait col", (137,180,250)),
        ("Card Stack", "3+ windows fan out cleanly", (166,227,161)),
        ("Karta Map", "Auto-HUD on switch & overview", (249,226,175)),
        ("Frame Pace", "16.6 ms (60.0 FPS locked)", (148,226,213)),
    ]
    for k, v, (r, g, b) in kv:
        v_t = trunc(v, max(8, cols - 15))
        out.append(f"  {fg(166,173,200)}{k:<10}:{RESET} {fg(r,g,b)}{v_t}{RESET}")
    return out

def render_fetch(cols, rows):
    out = make_header(cols) + [""]
    art = [
        "  ▄▄▄▄▄▄▄▄▄▄▄▄▄▄  ",
        " █ ┌────┐┌────┐ █ ",
        " █ │ C1 ││ C2 │ █ ",
        " █ └────┘└────┘ █ ",
        " █ ┌──────────┐ █ ",
        " █ │Karta│ █ ",
        " █ └──────────┘ █ ",
        "  ▀▀▀▀▀▀▀▀▀▀▀▀▀▀  ",
    ]
    info = [
        (f"{fg(203,166,247)}{BOLD}karta{RESET}@{fg(137,180,250)}hyprland{RESET}", 20),
        (f"{fg(108,112,134)}{'─' * min(22, max(8, cols - 22))}{RESET}", 22),
        (f"{fg(203,166,247)}Plugin  {RESET} Karta (C++23)", 26),
        (f"{fg(203,166,247)}WM      {RESET} Hyprland 0.57 Lua", 26),
        (f"{fg(203,166,247)}Lanes   {RESET} Auto row / col", 23),
        (f"{fg(203,166,247)}Stacks  {RESET} Fanned card deck", 25),
        (f"{fg(203,166,247)}Karta  {RESET} 2D synced spaces", 25),
        (f"{fg(203,166,247)}Overview{RESET} Live minimap", 21),
    ]
    for a, (b, _) in zip(art, info):
        out.append(f"{fg(137,180,250)}{a}{RESET} {b}")
    out.append("")
    swatches = [(243,139,168),(250,179,135),(249,226,175),(166,227,161),(148,226,213),(137,180,250),(203,166,247)]
    out.append("  " + " ".join(f"{bg(r,g,b)}   {RESET}" for r,g,b in swatches))
    out.append("")
    out.append(f"  {fg(166,227,161)}❯{RESET} {fg(205,214,244)}hsctl overview{RESET}")
    out.append(f"  {fg(166,227,161)}❯{RESET} {fg(205,214,244)}hsctl move-workspace r{RESET}")
    return out

def render_git(cols, rows):
    out = make_header(cols) + [""]
    commits = [
        ("5e1254d", "prioritize karta shortcuts"),
        ("128c786", "move workspace keys to lower row"),
        ("126b03b", "move windows between kartas"),
        ("845f64d", "ergonomic karta keybindings"),
        ("00a4f47", "restore source window memory"),
        ("b88a769", "render live overview cards"),
        ("9c4e21a", "overlapping card deck layout"),
    ]
    out.append(f"  {fg(250,179,135)}{BOLD}RECENT COMMITS{RESET}")
    max_msg = max(8, cols - 13)
    for sha, msg in commits[:max(3, rows - 11)]:
        out.append(f"  {fg(250,179,135)}* {sha}{RESET} {fg(205,214,244)}{trunc(msg, max_msg)}{RESET}")
    out.append("")
    out.append(f"  {fg(250,179,135)}{BOLD}REPOSITORY TREE{RESET}")
    for line in [
        "  src/",
        "  ├── layout/karta/  2D karta",
        "  ├── layout/lane/    row/col",
        "  ├── model/stack/    card deck",
        "  └── overview/       minimap",
    ]:
        out.append(f"{fg(186,194,222)}{trunc(line, cols - 1)}{RESET}")
    return out

def render_docs(cols, rows):
    out = make_header(cols) + [""]
    out.append(f"  {fg(245,194,231)}{BOLD}# Karta Model{RESET}")
    for line in [
        "  ┌──────────────────────┐",
        "  │ 2D Karta Workspace  │",
        "  │ (Monitors in sync)   │",
        "  └──────────┬───────────┘",
        "       ┌─────┴─────┐",
        "       ▼           ▼",
        "   Landscape    Portrait",
        "   Auto ROW     Auto COL",
        "   (← / →)      (↑ / ↓)",
    ]:
        out.append(f"{fg(205,214,244)}{trunc(line, cols - 1)}{RESET}")
    out.append("")
    out.append(f"  {fg(245,194,231)}{BOLD}## Ergonomic Keys{RESET}")
    for k, desc in [
        ("Win+HJKL",       "Focus window"),
        ("Win+Alt+HJKL",   "Move / stack"),
        ("Win+Shift+HJKL", "Swap stack"),
        ("Win+F",          "Expand stack"),
        ("Ctrl+Win+HJKL",  "Monitor"),
        ("Win+NM,.",       "2D karta"),
        ("Win+I",          "Overview"),
    ]:
        out.append(f"  {fg(137,180,250)}{k:<15}{RESET} {fg(186,194,222)}{trunc(desc, max(6, cols - 19))}{RESET}")
    return out

def render_logs(cols, rows, tick):
    out = make_header(cols) + [""]
    events = [
        ("17:12:01", "MONITOR",  "DEMO-L row 1280x720"),
        ("17:12:01", "MONITOR",  "DEMO-P col 492x800"),
        ("17:12:02", "STACK",    "3 wins -> card deck"),
        ("17:12:03", "FOCUS",    "stack[0] -> card #2"),
        ("17:12:04", "SWAP",     "stack[0] <-> [1]"),
        ("17:12:05", "EXPAND",   "stack fullscreen"),
        ("17:12:06", "HANDOFF",  "DEMO-L -> DEMO-P"),
        ("17:12:07", "KARTA",   "switch (0,0)->(1,0)"),
        ("17:12:08", "TRANSFER", "carry win -> (1,0)"),
        ("17:12:09", "OVERVIEW", "2D preview open"),
        ("17:12:10", "ACCEPT",   "focus restored"),
    ]
    out.append(f"  {fg(116,199,236)}{BOLD}KARTA EVENTS{RESET}")
    out.append("")
    shift = int(tick) % len(events)
    ordered = events[shift:] + events[:shift]
    max_msg = max(6, cols - 20)
    for ts, tag, msg in ordered[:max(1, rows - len(out))]:
        out.append(f" {fg(108,112,134)}{ts}{RESET} {fg(116,199,236)}{BOLD}{tag:<8}{RESET} {fg(205,214,244)}{trunc(msg, max_msg)}{RESET}")
    return out

def render_karta2_code(cols, rows):
    out = make_header(cols) + [""]
    lines = [
        (88,  "// Live workspace cards without altering", "cmt"),
        (89,  "// real tiled window geometry.", "cmt"),
        (90,  "void OverviewRenderer::drawCard(", "fn"),
        (91,  "    const KartaPreview& preview,", "code"),
        (92,  "    const CBox& viewport,", "code"),
        (93,  "    bool isSelected) {", "code"),
        (94,  "  drawBackdrop(viewport, isSelected);", "call"),
        (95,  "  for (const auto& s : preview.stacks) {", "kw"),
        (96,  "    drawStackDeck(s, viewport);", "call"),
        (97,  "  }", "kw"),
        (98,  "  if (isSelected)", "kw"),
        (99,  "    drawFocusRing(preview.selectedBox);", "call"),
        (100, "}", "fn"),
    ]
    max_code = max(8, cols - 8)
    for ln, text, tok in lines[:max(1, rows - len(out) - 2)]:
        t = trunc(text, max_code)
        num = f"{fg(108,112,134)}{ln:4d} │ {RESET}"
        color = {
            "cmt": fg(127, 132, 156),
            "kw": fg(166, 227, 161),
            "fn": fg(148, 226, 213) + BOLD,
            "call": fg(249, 226, 175),
        }.get(tok, fg(205, 214, 244))
        out.append(f"{num}{color}{t}{RESET}")
    out.append("")
    out.append(f"  {bg(166,227,161)}{fg(17,17,27)}{BOLD} KARTA 2 · WORKSPACE (1, 0) {RESET}")
    return out

def render_karta2_test(cols, rows):
    out = make_header(cols) + [""]
    tests = [
        ("core_logic_tests",     "42/42", "0.01s"),
        ("layout_logic_tests",   "68/68", "0.02s"),
        ("topology_route_tests", "24/24", "0.01s"),
        ("overview_session",     "31/31", "0.01s"),
        ("keybinds_tests",       "5/5",   "0.01s"),
        ("karta_transfer",      "PASS",  "2.40s"),
        ("shared_workspace",     "PASS",  "3.10s"),
    ]
    out.append(f"  {fg(249,226,175)}{BOLD}KARTA 2 · TEST SUITE{RESET}")
    out.append("")
    for name, res, dur in tests:
        out.append(f"  {fg(166,227,161)}✓{RESET} {fg(205,214,244)}{trunc(name, max(8, cols - 5))}{RESET}")
        out.append(f"    {fg(166,227,161)}{res:<7}{RESET} {fg(108,112,134)}{dur}{RESET}")
    out.append("")
    out.append(f"  {bg(166,227,161)}{fg(17,17,27)}{BOLD} ALL CHECKS PASSING {RESET}")
    return out

tick = 0.0
while True:
    sz = os.get_terminal_size()
    cols, rows = max(20, sz.columns), max(8, sz.lines)
    if kind == "editor": lines_out = render_editor(cols, rows)
    elif kind == "metrics": lines_out = render_metrics(cols, rows, tick)
    elif kind == "fetch": lines_out = render_fetch(cols, rows)
    elif kind == "git": lines_out = render_git(cols, rows)
    elif kind == "docs": lines_out = render_docs(cols, rows)
    elif kind == "logs": lines_out = render_logs(cols, rows, tick)
    elif kind == "karta2_code": lines_out = render_karta2_code(cols, rows)
    elif kind == "karta2_test": lines_out = render_karta2_test(cols, rows)
    else: lines_out = make_header(cols)
    render_screen(lines_out, rows)
    time.sleep(1.2 if kind in ("metrics", "logs") else 2.0)
    tick += 1.0
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", type=Path, default=REPO_ROOT / "Release/karta.so")
    parser.add_argument("--mp4", type=Path, default=REPO_ROOT / "videos/karta-showcase.mp4")
    parser.add_argument("--gif", type=Path, default=REPO_ROOT / "videos/karta-showcase.gif")
    parser.add_argument("--keep-tmp", action="store_true")
    args = parser.parse_args()

    plugin = args.plugin.resolve()
    assert plugin.is_file(), f"Missing plugin: {plugin}"
    args.mp4.parent.mkdir(parents=True, exist_ok=True)

    run = Path(tempfile.mkdtemp(prefix="karta-showcase-"))
    print(f"Working dir: {run}", flush=True)

    # 1. Prepare visual assets & helper scripts
    wp_l = run / "wp_landscape.png"
    wp_p = run / "wp_portrait.png"
    backdrop = run / "backdrop.png"
    generate_wallpaper(wp_l, MON_L_W, MON_L_H, seed_shift=0.0)
    generate_wallpaper(wp_p, MON_P_W, MON_P_H, seed_shift=0.8)
    generate_studio_backdrop(backdrop)

    panel_py = run / "panel.py"
    panel_py.write_text(PANEL_SCRIPT)
    panel_py.chmod(0o755)

    kitty_conf = run / "kitty.conf"
    kitty_conf.write_text(
        "font_family JetBrainsMono Nerd Font\n"
        "font_size 11.0\n"
        "window_padding_width 12\n"
        "background #181825\n"
        "foreground #cdd6f4\n"
        "background_opacity 0.93\n"
        "confirm_os_window_close 0\n"
        "enable_audio_bell no\n"
        "cursor_blink_interval 0\n"
        "shell_integration disabled\n"
    )

    hypr_cfg = run / "hyprland.lua"
    hypr_cfg.write_text(
        f"hl.plugin.load({json.dumps(str(plugin))})\n"
        "hl.config({\n"
        "  general = {\n"
        "    layout = 'karta',\n"
        "    gaps_in = 8,\n"
        "    gaps_out = 16,\n"
        "    border_size = 3,\n"
        "    ['col.active_border'] = 'rgb(89b4fa)',\n"
        "    ['col.inactive_border'] = 'rgb(45475a)',\n"
        "  },\n"
        "  decoration = {\n"
        "    rounding = 12,\n"
        "    active_opacity = 1.0,\n"
        "    inactive_opacity = 0.94,\n"
        "    shadow = { enabled = true, range = 18, render_power = 3, color = 'rgb(080a12)' },\n"
        "    blur = { enabled = true, size = 6, passes = 2 },\n"
        "  },\n"
        "  animations = {\n"
        "    enabled = true,\n"
        "  },\n"
        "  input = { follow_mouse = 0 },\n"
        "  cursor = { inactive_timeout = 1, hide_on_key_press = true },\n"
        "  misc = {\n"
        "    disable_hyprland_logo = true,\n"
        "    disable_splash_rendering = true,\n"
        "    background_color = 'rgb(0d101b)',\n"
        "  },\n"
        "  plugin = {\n"
        "    karta = {\n"
        "      column_default_width = '2',\n"
        "      karta_map = 1,\n"
        "      focus_wrap = 0,\n"
        "    },\n"
        "  },\n"
        "})\n"
        "hl.curve('smooth', { type = 'bezier', points = { {0.22, 1.0}, {0.36, 1.0} } })\n"
        "hl.animation({ leaf = 'windows', enabled = 1, speed = 5, bezier = 'smooth', style = 'slide' })\n"
        "hl.animation({ leaf = 'windowsMove', enabled = 1, speed = 5, bezier = 'smooth', style = 'slide' })\n"
        "hl.animation({ leaf = 'workspaces', enabled = 1, speed = 5, bezier = 'smooth', style = 'slide' })\n"
        "hl.animation({ leaf = 'fade', enabled = 1, speed = 4, bezier = 'smooth' })\n"
    )

    env = dict(os.environ)
    env.pop("HYPRLAND_INSTANCE_SIGNATURE", None)

    instance = None
    wl_socket = None
    awww_proc = None
    rec_l = None
    rec_p = None
    beats = []  # (t_start, chapter_idx, chapter_title, keys, caption)

    def ctl(*words: str) -> str:
        assert instance and instance != os.environ.get("HYPRLAND_INSTANCE_SIGNATURE")
        return subprocess.check_output(["hyprctl", "-i", instance, *words], text=True)

    def lua(code: str) -> None:
        res = ctl("eval", code)
        assert not res.lower().startswith("error"), f"Lua error for {code}: {res}"

    def query(name: str):
        return json.loads(ctl(name, "-j"))

    def karta(name: str, arg=None, wait: float = 0.55) -> None:
        call = f"hl.plugin.karta.{name}({json.dumps(arg) if arg is not None else ''})"
        lua(call)
        time.sleep(wait)

    WINDOW_TITLES = {
        "editor": "Neovim - core.cpp",
        "metrics": "System Telemetry",
        "fetch": "Shell - karta",
        "git": "Git - main",
        "docs": "Docs - Karta Model",
        "logs": "IPC Event Stream",
        "karta2_code": "Overview Renderer",
        "karta2_test": "Test Suite",
    }

    def launch(cls: str, kind: str) -> str:
        win_title = WINDOW_TITLES.get(kind, kind)
        cmd = f"kitty --config {kitty_conf} --class {cls} --title {shlex.quote(win_title)} python3 {panel_py} {kind}"
        lua(f"hl.dispatch(hl.dsp.exec_cmd({json.dumps(cmd)}))")
        for _ in range(100):
            client = next((c for c in query("clients") if c["class"] == cls), None)
            if client:
                time.sleep(0.35)
                return client["address"]
            time.sleep(0.08)
        raise RuntimeError(f"Window did not appear: {cls}")

    def focus_win(address: str, wait: float = 0.25) -> None:
        lua(f"hl.dispatch(hl.dsp.focus({{window={json.dumps('address:' + address)}}}))")
        time.sleep(wait)

    hypr_log = (run / "hyprland.log").open("w")
    hypr_proc = subprocess.Popen(["Hyprland", "-c", str(hypr_cfg)], env=env, stdout=hypr_log, stderr=hypr_log)

    try:
        for _ in range(100):
            instances = json.loads(subprocess.check_output(["hyprctl", "instances", "-j"], text=True))
            found = next((i for i in instances if i["pid"] == hypr_proc.pid), None)
            if found:
                instance = found["instance"]
                wl_socket = found.get("wl_socket")
                break
            assert hypr_proc.poll() is None, "Nested Hyprland exited unexpectedly"
            time.sleep(0.1)
        assert instance and wl_socket, "Failed to discover nested Hyprland"
        time.sleep(0.5)
        errs = ctl("configerrors").strip()
        assert not errs, f"Config errors: {errs}"

        # Create Landscape + Portrait headless outputs and remove initial WL-1
        ctl("output", "create", "headless", "DEMO-L")
        ctl("output", "create", "headless", "DEMO-P")
        lua(f'hl.monitor({{output="DEMO-L",mode="{MON_L_W}x{MON_L_H}@{FPS}",position="0x0",scale=1}})')
        lua(f'hl.monitor({{output="DEMO-P",mode="{MON_P_W}x{MON_P_H}@{FPS}",position="{MON_L_W}x0",scale=1}})')
        time.sleep(0.5)
        for m in query("monitors"):
            if m["name"] not in ("DEMO-L", "DEMO-P"):
                ctl("output", "remove", m["name"])
        time.sleep(0.5)

        nested_env = dict(env, WAYLAND_DISPLAY=wl_socket, HYPRLAND_INSTANCE_SIGNATURE=instance)

        # Apply gorgeous aurora wallpapers inside the isolated Wayland session
        awww_proc = subprocess.Popen(
            ["awww-daemon"],
            env=nested_env,
            stdout=open(run / "awww.log", "w"),
            stderr=subprocess.STDOUT,
        )
        time.sleep(0.8)
        subprocess.run(["awww", "img", "-o", "DEMO-L", str(wp_l), "--transition-type", "none"], env=nested_env, check=False)
        subprocess.run(["awww", "img", "-o", "DEMO-P", str(wp_p), "--transition-type", "none"], env=nested_env, check=False)
        time.sleep(0.5)

        # Pre-populate Karta 2 (to the right) first so Karta 2 is rich & ready
        lua('hl.dispatch(hl.dsp.focus({monitor="DEMO-L"}))')
        karta("switch_workspace", "r", wait=0.45)
        launch("demo-c2-code", "karta2_code")
        lua('hl.dispatch(hl.dsp.focus({monitor="DEMO-P"}))')
        launch("demo-c2-test", "karta2_test")
        time.sleep(0.3)

        # Return to Karta 1 and populate Landscape (3 stacks) + Portrait (2 stacks)
        lua('hl.dispatch(hl.dsp.focus({monitor="DEMO-L"}))')
        karta("switch_workspace", "l", wait=0.45)
        lua('hl.dispatch(hl.dsp.focus({monitor="DEMO-P"}))')
        w_docs = launch("demo-docs", "docs")
        w_logs = launch("demo-logs", "logs")
        focus_win(w_docs, wait=0.2)

        lua('hl.dispatch(hl.dsp.focus({monitor="DEMO-L"}))')
        w_editor = launch("demo-editor", "editor")
        w_metrics = launch("demo-metrics", "metrics")
        w_fetch = launch("demo-fetch", "fetch")
        w_git = launch("demo-git", "git")
        focus_win(w_editor, wait=0.4)
        # Clear any Hyprland startup banner notifications before recording begins
        ctl("dismissnotify", "-1")
        time.sleep(0.3)

        # Start synchronized dual-output recording
        raw_l = run / "raw_l.mp4"
        raw_p = run / "raw_p.mp4"
        rec_l = subprocess.Popen(
            ["wf-recorder", "-o", "DEMO-L", "-r", str(FPS), "-p", "crf=15", "-f", str(raw_l)],
            env=nested_env,
            stdout=open(run / "rec_l.log", "w"),
            stderr=subprocess.STDOUT,
        )
        rec_p = subprocess.Popen(
            ["wf-recorder", "-o", "DEMO-P", "-r", str(FPS), "-p", "crf=15", "-f", str(raw_p)],
            env=nested_env,
            stdout=open(run / "rec_p.log", "w"),
            stderr=subprocess.STDOUT,
        )
        t0 = time.monotonic()

        def beat(chap_idx: int, chap_title: str, keys: str, caption: str) -> None:
            beats.append((time.monotonic() - t0, chap_idx, chap_title, keys, caption))

        # Wait briefly for recorders to settle on first frames
        time.sleep(0.5)

        # ---- CHAPTER 1: Auto Orientation & Scrolling Lanes ----
        beat(1, "Auto Orientation & Scrolling Lanes", "Win + H / L", "Scroll horizontally across stacks on Landscape (auto row)")
        time.sleep(0.8)
        karta("move_focus", "r", wait=0.8)
        karta("move_focus", "r", wait=0.8)
        karta("move_focus", "r", wait=0.8)

        beat(1, "Auto Orientation & Scrolling Lanes", "Ctrl + Win + L  ·  Win + J / K", "Jump to Portrait (auto col) and scroll vertically")
        karta("focus_monitor", "r", wait=0.75)
        karta("move_focus", "d", wait=0.8)
        karta("move_focus", "u", wait=0.75)
        karta("focus_monitor", "l", wait=0.65)

        # ---- CHAPTER 2: Overlapping Card Stacks ----
        beat(2, "Overlapping Card Stacks", "Win + Alt + H", "Merge 3+ windows into a fanned overlapping card stack")
        focus_win(w_metrics, wait=0.35)
        karta("move_window", "l", wait=0.85)  # merge metrics into editor stack (2)
        focus_win(w_fetch, wait=0.35)
        karta("move_window", "l", wait=1.05)  # merge fetch into stack -> 3-card overlapping deck!

        beat(2, "Overlapping Card Stacks", "Win + J / K", "Cycle cards inside the stack — focused window pops to front")
        karta("move_focus", "u", wait=0.9)
        karta("move_focus", "u", wait=0.9)
        karta("move_focus", "d", wait=0.9)
        karta("move_focus", "d", wait=0.9)

        # ---- CHAPTER 3: Swap & Expand Stack ----
        beat(3, "Swap & Expand Stack", "Win + Shift + L", "Swap active windows across adjacent stacks in place")
        karta("swap_window", "r", wait=1.2)
        karta("swap_window", "l", wait=1.0)

        beat(3, "Swap & Expand Stack", "Win + F", "Expand the focused stack to fill the monitor, press again to restore")
        karta("toggle_fullscreen", wait=1.45)
        karta("toggle_fullscreen", wait=1.05)

        # ---- CHAPTER 4: Cross-Monitor Direct Transfer ----
        beat(4, "Cross-Monitor Transfer", "Ctrl + Win + Alt + L", "Send focused window straight to Portrait — auto-adapts to col")
        karta("move_focus", "r", wait=0.45)
        karta("move_window_to_monitor", "r", wait=1.5)

        beat(4, "Cross-Monitor Transfer", "Ctrl + Win + Alt + H", "Bring the window back to Landscape while keeping focus")
        karta("move_window_to_monitor", "l", wait=1.2)

        # ---- CHAPTER 5: Synchronized 2D Karta Workspaces ----
        beat(5, "Synchronized 2D Karta Workspaces", "Win + . / N", "Switch ALL monitors together as one 2D karta (with live Karta Map)")
        karta("switch_workspace", "r", wait=1.55)
        karta("switch_workspace", "l", wait=1.35)

        beat(5, "Synchronized 2D Karta Workspaces", "Win + Alt + .", "Carry focused window into an adjacent karta — all monitors follow")
        karta("move_window_to_workspace", "r", wait=1.7)

        # ---- CHAPTER 6: Interactive 2D Karta Overview ----
        beat(6, "Interactive 2D Overview", "Win + I", "Open live Karta Overview without disturbing tiled geometry")
        karta("toggle_overview", wait=1.6)

        beat(6, "Interactive 2D Overview", "Win + H / L  ·  Ctrl + Win + H", "Preview kartas & select stacks across monitors")
        karta("focus_monitor", "l", wait=1.35)
        karta("move_focus", "l", wait=1.15)
        karta("move_focus", "r", wait=1.15)

        beat(6, "Interactive 2D Overview", "Win + I", "Accept selection to land directly on the target karta & window")
        karta("toggle_overview", wait=1.8)

        total_dur = time.monotonic() - t0
        print(f"Recorded choreography duration: {total_dur:.2f}s ({len(beats)} beats)", flush=True)

        # Stop both recorders cleanly so MP4 moov atoms finalize
        rec_l.send_signal(signal.SIGINT)
        rec_p.send_signal(signal.SIGINT)
        rec_l.wait(timeout=8)
        rec_p.wait(timeout=8)
        rec_l = rec_p = None

    finally:
        for proc in (rec_l, rec_p):
            if proc and proc.poll() is None:
                proc.send_signal(signal.SIGINT)
                try:
                    proc.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    proc.kill()
        if awww_proc and awww_proc.poll() is None:
            awww_proc.terminate()
        hypr_proc.terminate()
        try:
            hypr_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            hypr_proc.kill()
        hypr_log.close()

    # Probe actual recorded durations and trim cleanly from 0.45s so both monitors are warm
    def probe_dur(p: Path) -> float:
        out = subprocess.check_output(
            ["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "default=nw=1:nk=1", str(p)],
            text=True,
        ).strip()
        return float(out)

    dur_l = probe_dur(raw_l)
    dur_p = probe_dur(raw_p)
    trim_start = 0.45
    clip_dur = min(dur_l, dur_p) - trim_start - 0.15
    print(f"Raw durations: L={dur_l:.2f}s, P={dur_p:.2f}s -> compositing {clip_dur:.2f}s", flush=True)

    # Render crisp HUD overlays for each beat
    hud_inputs = []
    hud_windows = []
    for idx, (ts, chap_idx, chap_title, keys, caption) in enumerate(beats):
        t_begin = max(0.0, ts - trim_start)
        t_end = (beats[idx + 1][0] - trim_start) if idx + 1 < len(beats) else clip_dur
        t_end = min(clip_dur, max(t_begin + 0.2, t_end))
        prog = (t_begin + t_end) / 2.0 / max(0.1, clip_dur)
        hud_png = run / f"hud_{idx:02d}.png"
        generate_hud_overlay(hud_png, chap_idx, 6, chap_title, keys, caption, prog)
        hud_inputs.append(hud_png)
        hud_windows.append((t_begin, t_end))

    # Build ffmpeg filter_complex:
    # [0] backdrop (looped), [1] raw_l, [2] raw_p, [3..N] HUD overlays
    cmd = [
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "warning",
        "-loop", "1", "-t", f"{clip_dur:.3f}", "-framerate", str(FPS), "-i", str(backdrop),
        "-ss", f"{trim_start:.3f}", "-t", f"{clip_dur:.3f}", "-i", str(raw_l),
        "-ss", f"{trim_start:.3f}", "-t", f"{clip_dur:.3f}", "-i", str(raw_p),
    ]
    for hp in hud_inputs:
        cmd += ["-i", str(hp)]

    filters = [
        f"[1:v]fps={FPS},scale={MON_L_W}:{MON_L_H},setsar=1[ml]",
        f"[2:v]fps={FPS},scale={MON_P_W}:{MON_P_H},setsar=1[mp]",
        f"[0:v][ml]overlay={MON_L_X}:{MON_L_Y}:shortest=1[s1]",
        f"[s1][mp]overlay={MON_P_X}:{MON_P_Y}:shortest=1[s2]",
    ]
    prev = "s2"
    for i, (t1, t2) in enumerate(hud_windows):
        inp = i + 3
        nxt = f"h{i}"
        filters.append(f"[{prev}][{inp}:v]overlay=0:0:enable='between(t,{t1:.3f},{t2:.3f})'[{nxt}]")
        prev = nxt
    fade_out_t = max(0.5, clip_dur - 0.6)
    filters.append(f"[{prev}]fade=t=in:st=0:d=0.4,fade=t=out:st={fade_out_t:.3f}:d=0.6,format=yuv420p[vout]")

    cmd += [
        "-filter_complex", ";".join(filters),
        "-map", "[vout]",
        "-c:v", "libx264", "-preset", "slow", "-crf", "19",
        "-movflags", "+faststart",
        str(args.mp4),
    ]
    print("Compositing 1920x1080 MP4...", flush=True)
    subprocess.check_call(cmd)

    # Generate compact, crisp palette-optimized README preview GIF (840px wide, 10fps)
    palette = run / "palette.png"
    gif_vf = "fps=10,scale=840:-1:flags=lanczos"
    print("Generating optimized README GIF...", flush=True)
    subprocess.check_call([
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "warning",
        "-i", str(args.mp4),
        "-vf", f"{gif_vf},palettegen=max_colors=128:stats_mode=diff",
        "-frames:v", "1", "-update", "1",
        str(palette),
    ])
    subprocess.check_call([
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "warning",
        "-i", str(args.mp4), "-i", str(palette),
        "-lavfi", f"{gif_vf}[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=3:diff_mode=rectangle",
        str(args.gif),
    ])

    # Extract 6 review keyframes (1 per chapter) so the result can be visually inspected
    frames_dir = run / "review_frames"
    frames_dir.mkdir(exist_ok=True)
    seen_chaps = set()
    for (t1, t2), (_, chap_idx, chap_title, _, _) in zip(hud_windows, beats):
        if chap_idx in seen_chaps:
            continue
        seen_chaps.add(chap_idx)
        mid = min(clip_dur - 0.2, t1 + min(1.0, (t2 - t1) * 0.65))
        subprocess.check_call([
            "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
            "-ss", f"{mid:.3f}", "-i", str(args.mp4),
            "-frames:v", "1", str(frames_dir / f"chap{chap_idx}.png"),
        ])

    mp4_mb = args.mp4.stat().st_size / (1024 * 1024)
    gif_mb = args.gif.stat().st_size / (1024 * 1024)
    print(f"DONE! MP4: {args.mp4} ({mp4_mb:.2f} MB), GIF: {args.gif} ({gif_mb:.2f} MB)", flush=True)
    print(f"Review frames: {frames_dir}", flush=True)

    if not args.keep_tmp:
        # Leave review frames path printed if keep-tmp; otherwise caller inspects before cleanup
        pass


if __name__ == "__main__":
    main()
