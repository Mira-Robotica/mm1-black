#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Render LVGL-style MM1-BLACK UI screens as PNG figures for the user manual."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent / "assets" / "figures"
W, H = 960, 1600  # 2x of 480x800 for crisp PDF
S = 2  # scale vs logical 480x800

# Firmware palette (src/main.cpp)
BG = (0xE6, 0xED, 0xF5)
TOP = (0xFF, 0xFF, 0xFF)
TEXT = (0x12, 0x20, 0x2C)
GREY = (0x4A, 0x62, 0x70)
BORDER = (0xB8, 0xC6, 0xD0)
HDR = (0x0F, 0x2A, 0x4A)
ACCENT = (0x0B, 0x5C, 0xAB)
ROW_EVEN = (0xE8, 0xF0, 0xF7)
ROW_ODD = (0xFF, 0xFF, 0xFF)
NAV = (0xD7, 0xF0, 0xEC)
SEL = (0xBB, 0xDE, 0xFB)
WHITE = (0xFF, 0xFF, 0xFF)
BTN_DEL = (0xC6, 0x28, 0x28)
BTN_SAVE = (0x1B, 0x5E, 0x20)
BTN_TX = (0x0B, 0x5C, 0xAB)
BTN_NEW = (0x00, 0x79, 0x6B)
BTN_USE = (0x45, 0x27, 0xA0)
TEAL = (0x00, 0x79, 0x6B)
AIM = (0xF9, 0xA8, 0x25)
SENS_LZR = (0x00, 0x79, 0x6B)
SENS_IMU = (0x3F, 0x51, 0xB5)
SENS_LNK = (0x0B, 0x5C, 0xAB)
SENS_BTN = (0xE6, 0x51, 0x00)
SENS_TMP = (0x2E, 0x7D, 0x32)
SD_ON = (0x2E, 0x7D, 0x32)
FILE_ACT = (0xC8, 0xE6, 0xC9)
GREEN_PT = (0x2E, 0x7D, 0x32)


def hex_rgb(h):
    return ((h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF)


def find_font(size, bold=False):
    cands = []
    if bold:
        cands += [
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
            "/Library/Fonts/Arial Bold.ttf",
            "/System/Library/Fonts/Supplemental/Arial Black.ttf",
        ]
    cands += [
        "/Library/Fonts/Montserrat-Regular.ttf",
        "/Library/Fonts/Montserrat.ttf",
        str(Path.home() / "Library/Fonts/Montserrat-Regular.ttf"),
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    ]
    for p in cands:
        if Path(p).is_file():
            try:
                return ImageFont.truetype(p, size)
            except OSError:
                continue
    return ImageFont.load_default()


def font(sz, bold=False):
    return find_font(int(sz * S), bold)


def rounded_rect(draw, xy, r, fill=None, outline=None, width=1):
    draw.rounded_rectangle(xy, radius=r, fill=fill, outline=outline, width=width)


def text_w(draw, t, f):
    b = draw.textbbox((0, 0), t, font=f)
    return b[2] - b[0]


def text_h(draw, t, f):
    b = draw.textbbox((0, 0), t, font=f)
    return b[3] - b[1]


def new_screen():
    img = Image.new("RGB", (W, H), BG)
    draw = ImageDraw.Draw(img)
    return img, draw


def draw_header(draw, status="78%  SD  LINK  14:32"):
    """Top bar: MM1-BLACK + status icons/text."""
    hh = 56 * S // 2  # ~56 logical? use 48
    hh = 48 * S // 2 if S == 1 else 56
    # Keep proportional: header ~48px at 480 → 96 at 960
    hh = 48 * S
    draw.rectangle([0, 0, W, hh], fill=TOP)
    draw.line([(0, hh - 2), (W, hh - 2)], fill=ACCENT, width=3)
    f_title = font(15, bold=True)
    f_stat = font(12)
    draw.text((12 * S, 14 * S), "MM1-BLACK", fill=TEXT, font=f_title)
    tw = text_w(draw, status, f_stat)
    draw.text((W - 12 * S - tw, 16 * S), status, fill=GREY, font=f_stat)
    # tiny SD green dot
    draw.ellipse([W - 12 * S - tw - 18 * S, 18 * S, W - 12 * S - tw - 8 * S, 28 * S], fill=SD_ON)
    return hh


def draw_tabs(draw, y0, active):
    tabs = ["POINTS", "VIEW", "SENSOR", "FILES", "SETUP"]
    th = 40 * S
    draw.rectangle([0, y0, W, y0 + th], fill=TOP)
    draw.line([(0, y0 + th - 1), (W, y0 + th - 1)], fill=BORDER, width=1)
    tw = W / len(tabs)
    f = font(11, bold=True)
    for i, t in enumerate(tabs):
        x = int(i * tw)
        on = t == active or (active == "FILE" and t == "FILES")
        col = ACCENT if on else GREY
        label = t if t != "FILES" else ("FILE" if active == "FILE" else "FILES")
        # Prefer firmware FILES; manual often says FILE — show FILES
        label = t
        lw = text_w(draw, label, f)
        draw.text((x + (tw - lw) / 2, y0 + 10 * S), label, fill=col, font=f)
        if on:
            draw.line(
                [(x + 8 * S, y0 + th - 4), (x + tw - 8 * S, y0 + th - 4)],
                fill=ACCENT,
                width=4,
            )
    return y0 + th


def draw_setup_subs(draw, y0, active):
    subs = ["Bright", "Cal", "BT", "WiFi", "About"]
    th = 44 * S
    draw.rectangle([0, y0, W, y0 + th], fill=TOP)
    tw = W / len(subs)
    f = font(12, bold=True)
    for i, s in enumerate(subs):
        x = int(i * tw)
        on = s == active
        if on:
            rounded_rect(
                draw,
                [x + 4 * S, y0 + 4 * S, x + tw - 4 * S, y0 + th - 4 * S],
                8,
                fill=ACCENT,
            )
            col = WHITE
        else:
            col = GREY
        lw = text_w(draw, s, f)
        draw.text((x + (tw - lw) / 2, y0 + 12 * S), s, fill=col, font=f)
    return y0 + th


def pill_btn(draw, x, y, w, h, label, fill, f=None):
    f = f or font(13, bold=True)
    rounded_rect(draw, [x, y, x + w, y + h], 10, fill=fill)
    lw = text_w(draw, label, f)
    lh = text_h(draw, label, f)
    draw.text((x + (w - lw) / 2, y + (h - lh) / 2 - 1), label, fill=WHITE, font=f)


def screen_points(aim=False):
    img, draw = new_screen()
    y = draw_header(draw)
    y = draw_tabs(draw, y, "POINTS")

    # table header
    th = 36 * S
    hdr_col = AIM if aim else HDR
    tcol = TEXT if aim else WHITE
    cols = [("Ref#", 90), ("D(m)", 150), ("E", 70), ("Azm", 140), ("Inc", 120)]
    f_h = font(12, bold=True)
    draw.rectangle([8 * S, y + 6 * S, W - 8 * S, y + 6 * S + th], fill=hdr_col)
    cx = 16 * S
    for name, cw in cols:
        draw.text((cx, y + 14 * S), name, fill=tcol, font=f_h)
        cx += cw

    rows = [
        (True, False, "12", "4.812", "", "212.4", "-3.1"),
        (False, True, "11", "3.105", "", "210.8", "-2.8"),
        (False, True, "10", "3.105", "", "210.8", "-2.8"),
        (False, True, "9", "3.105", "", "210.8", "-2.8"),
        (False, False, "8", "2.447", "", "98.2", "1.4"),
        (False, False, "7", "5.920", "", "45.0", "-12.6"),
        (False, False, "6", "-", "E", "-", "-"),
        (False, False, "5", "1.882", "", "180.0", "0.2"),
        (False, False, "4", "6.401", "", "33.7", "5.5"),
        (False, False, "3", "2.110", "", "275.1", "-8.0"),
    ]
    ry = y + 6 * S + th
    rh = 42 * S
    f_r = font(13)
    f_rb = font(13, bold=True)
    widths = [90, 150, 70, 140, 120]
    for i, (sel, nav, *vals) in enumerate(rows):
        if sel:
            fill = SEL
        elif nav:
            fill = NAV
        else:
            fill = ROW_EVEN if i % 2 else ROW_ODD
        draw.rectangle([8 * S, ry, W - 8 * S, ry + rh], fill=fill)
        cx = 16 * S
        for j, (v, cw) in enumerate(zip(vals, widths)):
            if j == 0:
                col, ff = ACCENT, f_rb
            elif j == 2 and v == "E":
                col, ff = BTN_DEL, f_rb
            else:
                col, ff = TEXT, f_r
            draw.text((cx, ry + 10 * S), v, fill=col, font=ff)
            cx += cw
        ry += rh

    # pager
    f_p = font(12)
    draw.text((W // 2 - 30 * S, ry + 10 * S), "1 / 2", fill=GREY, font=f_p)

    # status / AIM hint
    by = H - 70 * S
    if aim:
        f_a = font(12, bold=True)
        msg = "AIM - press again to CAPTURE"
        mw = text_w(draw, msg, f_a)
        draw.text(((W - mw) / 2, by - 36 * S), msg, fill=AIM, font=f_a)

    # action buttons
    labels = [("DEL", BTN_DEL), ("CLR", BTN_DEL), ("SAVE", BTN_SAVE), ("TX", BTN_TX)]
    gap = 8 * S
    bw = (W - 16 * S - 3 * gap) // 4
    bh = 52 * S
    for i, (lab, col) in enumerate(labels):
        pill_btn(draw, 8 * S + i * (bw + gap), by, bw, bh, lab, col)

    return img


def screen_view():
    img, draw = new_screen()
    y = draw_header(draw)
    y = draw_tabs(draw, y, "VIEW")
    pad = 12 * S
    card_h = (H - y - 3 * pad) // 2

    def plot_card(top, title, plan=True):
        rounded_rect(
            draw,
            [pad, top, W - pad, top + card_h],
            16,
            fill=WHITE,
            outline=BORDER,
            width=2,
        )
        f = font(14, bold=True)
        draw.text((pad + 14 * S, top + 12 * S), title, fill=TEXT, font=f)
        # axes-ish
        ax, ay = pad + 80 * S, top + card_h - 70 * S
        bx, by = W - pad - 100 * S, top + 90 * S
        if not plan:
            ay = top + card_h // 2 + 40 * S
            by = top + card_h // 2 - 30 * S
        draw.line([(ax, ay), (bx, by)], fill=ACCENT if plan else TEAL, width=5)
        # splay rays
        rays = [
            (ax, ay, ax + 50 * S, ay - 70 * S),
            (ax, ay, ax - 40 * S, ay - 55 * S),
            (ax, ay, ax + 20 * S, ay - 90 * S),
            (bx, by, bx + 55 * S, by - 40 * S),
            (bx, by, bx + 70 * S, by + 30 * S),
            (bx, by, bx - 10 * S, by - 60 * S),
        ]
        for x0, y0, x1, y1 in rays:
            draw.line([(x0, y0), (x1, y1)], fill=(144, 164, 174), width=2)
        r = 10 * S
        draw.ellipse([ax - r, ay - r, ax + r, ay + r], fill=GREEN_PT)
        draw.ellipse([bx - r, by - r, bx + r, by + r], fill=GREEN_PT)
        f_s = font(11)
        draw.text((W - pad - 50 * S, top + card_h - 28 * S), "5 m", fill=GREY, font=f_s)

    plot_card(y + pad, "PLAN  azi   2 leg", True)
    plot_card(y + pad + card_h + pad, "PROFILE  inc   6 splay", False)
    return img


def screen_sensor():
    img, draw = new_screen()
    y = draw_header(draw)
    y = draw_tabs(draw, y, "SENSOR")
    pad = 12 * S
    cards = [
        ("LASER", "3.245 m", SENS_LZR),
        ("IMU", "Az 212.4\nInc -3.1", SENS_IMU),
        ("LINK", "laser OK\nimu OK", SENS_LNK),
        ("BUTTON", "2-tap ready", SENS_BTN),
    ]
    cw = (W - 3 * pad) // 2
    ch = 160 * S
    f_t = font(14, bold=True)
    f_v = font(22, bold=True)
    for i, (title, val, col) in enumerate(cards):
        col_i, row_i = i % 2, i // 2
        x = pad + col_i * (cw + pad)
        yy = y + pad + row_i * (ch + pad)
        rounded_rect(draw, [x, yy, x + cw, yy + ch], 14, fill=col)
        draw.text((x + 16 * S, yy + 16 * S), title, fill=WHITE, font=f_t)
        lines = val.split("\n")
        for li, line in enumerate(lines):
            draw.text((x + 16 * S, yy + 55 * S + li * 36 * S), line, fill=WHITE, font=f_v)

    # TEMP / BAT full width
    yy = y + pad + 2 * (ch + pad)
    rounded_rect(draw, [pad, yy, W - pad, yy + 120 * S], 14, fill=SENS_TMP)
    draw.text((pad + 16 * S, yy + 20 * S), "TEMP / BAT", fill=WHITE, font=f_t)
    draw.text((pad + 16 * S, yy + 60 * S), "38.2 C   |   3.91 V  ·  78%", fill=WHITE, font=f_v)
    return img


def screen_file():
    img, draw = new_screen()
    y = draw_header(draw)
    y = draw_tabs(draw, y, "FILES")
    pad = 12 * S
    f_h = font(13, bold=True)
    # file list header
    th = 40 * S
    draw.rectangle([pad, y + pad, W - pad, y + pad + th], fill=HDR)
    draw.text((pad + 12 * S, y + pad + 10 * S), "File", fill=WHITE, font=f_h)
    draw.text((W // 2, y + pad + 10 * S), "Pts", fill=WHITE, font=f_h)
    draw.text((W - pad - 140 * S, y + pad + 10 * S), "Active", fill=WHITE, font=f_h)

    files = [
        ("mm1_black_001.csv", "42", True),
        ("mm1_black_002.csv", "18", False),
        ("mm1_black_003.csv", "7", False),
        ("mm1_black_004.csv", "0", False),
    ]
    rh = 48 * S
    ry = y + pad + th
    f_r = font(13)
    for i, (name, pts, act) in enumerate(files):
        fill = FILE_ACT if act else (ROW_EVEN if i % 2 else ROW_ODD)
        draw.rectangle([pad, ry, W - pad, ry + rh], fill=fill)
        draw.text((pad + 12 * S, ry + 12 * S), name, fill=TEXT, font=f_r)
        draw.text((W // 2, ry + 12 * S), pts, fill=TEXT, font=f_r)
        if act:
            draw.text((W - pad - 100 * S, ry + 12 * S), "USE", fill=TEAL, font=font(13, bold=True))
        ry += rh

    f_s = font(12)
    draw.text((pad, ry + 16 * S), "Ready", fill=GREY, font=f_s)

    by = H - 70 * S
    labels = [("NEW", BTN_NEW), ("USE", BTN_USE), ("DEL", BTN_DEL)]
    gap = 10 * S
    bw = (W - 2 * pad - 2 * gap) // 3
    for i, (lab, col) in enumerate(labels):
        pill_btn(draw, pad + i * (bw + gap), by, bw, 52 * S, lab, col)
    return img


def screen_setup_measure():
    img, draw = new_screen()
    y = draw_header(draw)
    y = draw_tabs(draw, y, "SETUP")
    y = draw_setup_subs(draw, y, "Cal")
    pad = 16 * S
    f_t = font(18, bold=True)
    draw.text((pad, y + 20 * S), "Measure button", fill=ACCENT, font=f_t)

    modes = ["2-tap", "1-tap", "Cont"]
    mw = (W - 2 * pad - 2 * 10 * S) // 3
    mh = 56 * S
    my = y + 70 * S
    f_b = font(15, bold=True)
    for i, m in enumerate(modes):
        on = m == "2-tap"
        x = pad + i * (mw + 10 * S)
        if on:
            rounded_rect(draw, [x, my, x + mw, my + mh], 10, fill=ACCENT)
            col = WHITE
        else:
            rounded_rect(draw, [x, my, x + mw, my + mh], 10, fill=TOP, outline=BORDER, width=2)
            col = TEXT
        lw = text_w(draw, m, f_b)
        draw.text((x + (mw - lw) / 2, my + 16 * S), m, fill=col, font=f_b)

    # Hold 5s
    hy = my + mh + 24 * S
    pill_btn(draw, pad, hy, W - 2 * pad, 56 * S, "Hold 5s nav ON", TEAL, f_b)

    f_h = font(12)
    hint = (
        "2-tap: aim then capture. 1-tap: one press. Cont: tap start/stop.\n"
        "Hold 5s (1/2-tap only) writes nav x3."
    )
    draw.multiline_text((pad, hy + 72 * S), hint, fill=GREY, font=f_h, spacing=6)
    return img


def screen_setup_bright():
    img, draw = new_screen()
    y = draw_header(draw)
    y = draw_tabs(draw, y, "SETUP")
    y = draw_setup_subs(draw, y, "Bright")
    pad = 16 * S
    f_t = font(18, bold=True)
    f_v = font(16, bold=True)
    f_h = font(12)

    def slider(yy, label, value_txt, pct):
        draw.text((pad, yy), label, fill=ACCENT, font=f_t)
        draw.text((pad, yy + 40 * S), value_txt, fill=TEXT, font=f_v)
        track_y = yy + 90 * S
        track_h = 14 * S
        rounded_rect(
            draw,
            [pad, track_y, W - pad, track_y + track_h],
            8,
            fill=BORDER,
        )
        fill_w = int((W - 2 * pad) * pct / 100)
        rounded_rect(
            draw,
            [pad, track_y, pad + fill_w, track_y + track_h],
            8,
            fill=ACCENT,
        )
        # knob
        kx = pad + fill_w
        r = 16 * S
        draw.ellipse([kx - r, track_y + track_h // 2 - r, kx + r, track_y + track_h // 2 + r], fill=ACCENT)
        return track_y + 50 * S

    y = slider(y + 24 * S, "Display brightness", "Brightness 80%", 80)
    draw.text((pad, y), "Saved automatically when you release the slider.", fill=GREY, font=f_h)
    y = slider(y + 50 * S, "Speaker volume", "Volume 60%", 60)
    draw.text((pad, y), "0 = mute. Saved when you release the slider.", fill=GREY, font=f_h)

    y += 50 * S
    draw.text((pad, y), "Theme", fill=ACCENT, font=f_t)
    bw = (W - 2 * pad - 12 * S) // 2
    bh = 52 * S
    by = y + 44 * S
    rounded_rect(draw, [pad, by, pad + bw, by + bh], 10, fill=ACCENT)
    f_b = font(15, bold=True)
    lw = text_w(draw, "Light", f_b)
    draw.text((pad + (bw - lw) / 2, by + 14 * S), "Light", fill=WHITE, font=f_b)
    rounded_rect(
        draw,
        [pad + bw + 12 * S, by, pad + 2 * bw + 12 * S, by + bh],
        10,
        fill=TOP,
        outline=BORDER,
        width=2,
    )
    lw = text_w(draw, "Dark", f_b)
    draw.text((pad + bw + 12 * S + (bw - lw) / 2, by + 14 * S), "Dark", fill=TEXT, font=f_b)
    return img


def screen_bt():
    img, draw = new_screen()
    y = draw_header(draw, "78%  SD  LINK  14:32")
    y = draw_tabs(draw, y, "SETUP")
    y = draw_setup_subs(draw, y, "BT")
    pad = 16 * S
    f_t = font(15, bold=True)
    f_v = font(14)
    f_s = font(13)

    draw.text((pad, y + 16 * S), "Bluetooth: advertising SAP6_0001 (BLE)", fill=ACCENT, font=f_t)

    rows = [
        ("Name", "SAP6_0001"),
        ("MAC", "AA:BB:CC:DD:EE:01"),
        ("Bond", "none"),
        ("Peer", "-"),
    ]
    yy = y + 60 * S
    for k, v in rows:
        draw.text((pad, yy), k, fill=GREY, font=f_s)
        draw.text((pad + 120 * S, yy), v, fill=TEXT, font=f_v)
        yy += 40 * S

    yy += 20 * S
    gap = 10 * S
    bw = (W - 2 * pad - gap) // 2
    bh = 52 * S
    pill_btn(draw, pad, yy, bw, bh, "Measure", BTN_TX)
    pill_btn(draw, pad + bw + gap, yy, bw, bh, "TX", BTN_TX)
    yy += bh + 16 * S
    pill_btn(draw, pad, yy, bw, bh, "Restart BLE", BTN_TX)
    pill_btn(draw, pad + bw + gap, yy, bw, bh, "Unpair", BTN_DEL)

    f_h = font(12)
    draw.text((pad, yy + bh + 24 * S), "BLE OK - look for SAP6_0001 in nRF/TopoDroid", fill=GREY, font=f_h)
    return img


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    jobs = [
        ("ui-points.png", lambda: screen_points(False)),
        ("ui-points-aim.png", lambda: screen_points(True)),
        ("ui-view.png", screen_view),
        ("ui-sensor.png", screen_sensor),
        ("ui-file.png", screen_file),
        ("ui-setup-measure.png", screen_setup_measure),
        ("ui-setup-bright.png", screen_setup_bright),
        ("ui-bt.png", screen_bt),
    ]
    for name, fn in jobs:
        img = fn()
        path = OUT / name
        img.save(path, "PNG", optimize=True)
        print(f"OK {path.name} {path.stat().st_size} bytes {img.size}")


if __name__ == "__main__":
    main()
