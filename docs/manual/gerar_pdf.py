#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Manual do Usuário PDF — MM1-BLACK Trena 2D MIRA (cliente final)."""

from pathlib import Path

from fpdf import FPDF

OUT_DIR = Path(__file__).resolve().parent
OUT_PDF = OUT_DIR / "MM1-BLACK_Manual_Usuario.pdf"
FIG = OUT_DIR / "assets" / "figures"
PRODUCT = OUT_DIR / "assets" / "product"
HERO = PRODUCT / "produto-hero-dark.png"
if not HERO.is_file():
    HERO = FIG / "produto-hero.png"
if not HERO.is_file():
    HERO = OUT_DIR / "assets" / "mm1-black-hero.png"
STUDIO = PRODUCT / "produto-studio.png"
CLOSEUP = PRODUCT / "produto-closeup.png"
LOGO = FIG / "mira-horizontal.png"
if not LOGO.is_file():
    LOGO = FIG / "mira-principal.png"
if not LOGO.is_file():
    LOGO = OUT_DIR / "assets" / "mira-logo.png"
LOGO_MARK = FIG / "mira-principal.png"
if not LOGO_MARK.is_file():
    LOGO_MARK = LOGO

# MIRA brand
RED = (200, 32, 47)
CHARCOAL = (64, 64, 64)
MID = (90, 90, 90)
LIGHT = (247, 245, 242)
WHITE = (255, 255, 255)
BLACK = (26, 26, 26)

# LVGL palette (firmware)
LV_BG = (230, 237, 245)
LV_TOP = (255, 255, 255)
LV_TEXT = (18, 32, 44)
LV_GREY = (74, 98, 112)
LV_BORDER = (184, 198, 208)
LV_HDR = (15, 42, 74)
LV_ACCENT = (11, 92, 171)
LV_ROW_EVEN = (232, 240, 247)
LV_NAV = (215, 240, 236)
LV_SEL = (187, 222, 251)
LV_BTN_DEL = (198, 40, 40)
LV_BTN_SAVE = (27, 94, 32)
LV_BTN_TX = (11, 92, 171)
LV_BTN_CLR = (230, 81, 0)
LV_TEAL = (0, 121, 107)
LV_AIM = (249, 168, 37)

FONT_REG = [
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    "/Library/Fonts/Arial Unicode.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
]
FONT_BOLD = [
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/Library/Fonts/Arial Bold.ttf",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
]


def pick(cands):
    for p in cands:
        if Path(p).is_file():
            return p
    raise FileNotFoundError("Fonte TTF não encontrada")


class Manual(FPDF):
    def __init__(self):
        super().__init__("P", "mm", "A4")
        self.set_auto_page_break(True, 20)
        self.set_margins(16, 18, 16)
        self.add_font("B", "", pick(FONT_REG))
        self.add_font("B", "B", pick(FONT_BOLD))
        self.cover_mode = False

    def header(self):
        if self.cover_mode or self.page_no() <= 1:
            return
        self.set_font("B", "B", 9)
        self.set_text_color(*RED)
        self.set_xy(16, 10)
        self.cell(40, 5, "MIRA")
        self.set_font("B", "", 8)
        self.set_text_color(*MID)
        self.set_xy(16, 10)
        self.cell(178, 5, "MM1-BLACK  ·  Trena 2D  ·  Manual do usuário", align="R")
        self.set_draw_color(*RED)
        self.set_line_width(0.45)
        self.line(16, 16.5, 194, 16.5)
        self.set_y(20)

    def footer(self):
        if self.cover_mode or self.page_no() <= 1:
            return
        self.set_y(-14)
        self.set_draw_color(*CHARCOAL)
        self.set_line_width(0.2)
        self.line(16, self.get_y(), 194, self.get_y())
        self.set_y(-11)
        self.set_font("B", "", 8)
        self.set_text_color(*MID)
        self.cell(89, 6, "www.mirarobotica.com", align="L")
        self.cell(89, 6, str(self.page_no()), align="R")

    # ── helpers ──
    def h1(self, t):
        self.set_font("B", "B", 18)
        self.set_text_color(*CHARCOAL)
        self.multi_cell(0, 8, t)
        self.set_draw_color(*RED)
        self.set_line_width(0.8)
        y = self.get_y()
        self.line(16, y + 1, 50, y + 1)
        self.ln(6)

    def h2(self, t):
        self.ln(2)
        self.set_font("B", "B", 12)
        self.set_text_color(*RED)
        self.multi_cell(0, 6, t)
        self.ln(1)

    def body(self, t):
        self.set_font("B", "", 10)
        self.set_text_color(*BLACK)
        self.multi_cell(0, 5.2, t)
        self.ln(2)

    def bullet(self, items):
        self.set_font("B", "", 10)
        self.set_text_color(*BLACK)
        for it in items:
            x = self.get_x()
            self.set_x(18)
            self.cell(4, 5.2, "•")
            self.multi_cell(0, 5.2, it)
        self.ln(2)

    def callout(self, t, kind="info"):
        if kind == "warn":
            bg, border = (255, 243, 224), (230, 81, 0)
        else:
            bg, border = (232, 240, 247), LV_ACCENT
        self.set_fill_color(*bg)
        self.set_draw_color(*border)
        self.set_line_width(0.6)
        x, y = 16, self.get_y()
        self.set_font("B", "", 9.5)
        # estimate height
        self.set_xy(20, y + 3)
        self.multi_cell(170, 5, t)
        h = self.get_y() - y + 3
        self.rect(x, y, 178, h, "D")
        self.set_fill_color(*border)
        self.rect(x, y, 1.8, h, "F")
        self.set_y(y + h + 3)

    def step(self, n, t):
        self.set_font("B", "B", 11)
        self.set_text_color(*RED)
        self.cell(10, 6, f"{n:02d}")
        self.set_font("B", "", 10)
        self.set_text_color(*BLACK)
        self.multi_cell(0, 5.5, t)
        self.ln(1)

    def photo(self, path, w=140, caption=None, center=True):
        """Embed a product photo (or any Path). Returns False if missing."""
        path = Path(path)
        if not path.is_file():
            return False
        x = (210 - w) / 2 if center else self.l_margin
        y = self.get_y()
        try:
            from PIL import Image as _PILImage

            with _PILImage.open(path) as im:
                iw, ih = im.size
            h = w * ih / iw
        except Exception:
            h = w * 0.72
        if y + h + 16 > 275 and y > 40:
            self.add_page()
            y = self.get_y()
        try:
            self.image(str(path), x=x, y=y, w=w)
        except Exception:
            return False
        self.set_y(y + h + 2)
        if caption:
            self.set_font("B", "", 8)
            self.set_text_color(*MID)
            self.multi_cell(0, 4, caption, align="C")
            self.ln(2)
        return True

    def fig(self, name, w=None, caption=None, center=True):
        """Embed a PNG from assets/figures. Returns False if missing."""
        path = FIG / name
        if not path.is_file():
            return False
        if w is None:
            w = 90
        x = (210 - w) / 2 if center else self.l_margin
        y = self.get_y()
        # page-break if needed
        max_h = 250 - y
        # estimate height from aspect 480:800 = 0.6
        h_est = w * (800 / 480)
        if h_est > max_h and y > 40:
            self.add_page()
            y = self.get_y()
        try:
            self.image(str(path), x=x, y=y, w=w)
        except Exception:
            return False
        # advance Y by image height
        try:
            from PIL import Image as _PILImage

            with _PILImage.open(path) as im:
                iw, ih = im.size
            h = w * ih / iw
        except Exception:
            h = w * 800 / 480
        self.set_y(y + h + 2)
        if caption:
            self.set_font("B", "", 8)
            self.set_text_color(*MID)
            self.multi_cell(0, 4, caption, align="C")
            self.ln(2)
        return True

    def fig_pair(self, name_a, name_b, w=78, cap_a=None, cap_b=None):
        """Two UI screens side by side."""
        ya = self.get_y()
        h_est = w * 800 / 480
        if ya + h_est + 20 > 275:
            self.add_page()
            ya = self.get_y()
        gap = 8
        x0 = (210 - (2 * w + gap)) / 2
        ok = False
        for i, (name, cap) in enumerate([(name_a, cap_a), (name_b, cap_b)]):
            path = FIG / name
            x = x0 + i * (w + gap)
            if path.is_file():
                try:
                    self.image(str(path), x=x, y=ya, w=w)
                    ok = True
                except Exception:
                    pass
            if cap:
                self.set_xy(x, ya + h_est + 1)
                self.set_font("B", "", 7.5)
                self.set_text_color(*MID)
                self.cell(w, 4, cap, align="C")
        self.set_y(ya + h_est + (8 if (cap_a or cap_b) else 4))
        return ok

    # ── LVGL screen drawer (fallback if PNG missing) ──
    def draw_device(self, x, y, w, h, tab, content_fn, title_right="78%  SD  LINK  14:32"):
        """Draw phone-like bezel + LVGL chrome."""
        # bezel
        self.set_fill_color(28, 28, 28)
        self.rounded_rect(x, y, w, h, 3, "F")
        # screen
        sx, sy = x + 3, y + 5
        sw, sh = w - 6, h - 10
        self.set_fill_color(*LV_BG)
        self.rect(sx, sy, sw, sh, "F")
        # header
        self.set_fill_color(*LV_TOP)
        self.rect(sx, sy, sw, 7, "F")
        self.set_draw_color(*LV_ACCENT)
        self.set_line_width(0.5)
        self.line(sx, sy + 7, sx + sw, sy + 7)
        self.set_font("B", "B", 5.5)
        self.set_text_color(*LV_TEXT)
        self.set_xy(sx + 1.5, sy + 1.2)
        self.cell(28, 4, "MM1-BLACK")
        self.set_font("B", "", 4.5)
        self.set_text_color(*LV_GREY)
        self.set_xy(sx + 28, sy + 1.5)
        self.cell(sw - 30, 4, title_right, align="R")
        # tabs
        tabs = ["POINTS", "VIEW", "SENSOR", "FILE", "SETUP"]
        tw = sw / 5
        self.set_fill_color(*LV_TOP)
        self.rect(sx, sy + 7, sw, 6, "F")
        self.set_draw_color(*LV_BORDER)
        self.set_line_width(0.2)
        self.line(sx, sy + 13, sx + sw, sy + 13)
        for i, t in enumerate(tabs):
            self.set_font("B", "B", 4)
            active = t == tab
            self.set_text_color(*(LV_ACCENT if active else LV_GREY))
            self.set_xy(sx + i * tw, sy + 8)
            self.cell(tw, 4, t, align="C")
            if active:
                self.set_draw_color(*LV_ACCENT)
                self.set_line_width(0.6)
                self.line(sx + i * tw + 1, sy + 12.5, sx + (i + 1) * tw - 1, sy + 12.5)
        content_fn(sx, sy + 13.5, sw, sh - 13.5)

    def rounded_rect(self, x, y, w, h, r, style="F"):
        # fpdf2 has rounded_rect in recent versions; fallback to rect
        try:
            super().rounded_rect(x, y, w, h, r, style)
        except Exception:
            self.rect(x, y, w, h, style)

    def ui_points(self, sx, sy, sw, sh, aim=False, ok=False):
        # table header
        if aim:
            hdr = LV_AIM
            fg = LV_TEXT
        elif ok:
            hdr = (46, 125, 50)
            fg = WHITE
        else:
            hdr = LV_HDR
            fg = WHITE
        self.set_fill_color(*hdr)
        self.rect(sx + 1, sy + 1, sw - 2, 5, "F")
        self.set_font("B", "B", 4)
        self.set_text_color(*fg)
        cols = [("Ref#", 10), ("D(m)", 18), ("E", 8), ("Azm", 16), ("Inc", 14)]
        cx = sx + 2
        for name, cw in cols:
            self.set_xy(cx, sy + 1.5)
            self.cell(cw, 4, name)
            cx += cw
        rows = [
            (True, False, "12", "4.812", "", "212.4", "-3.1"),
            (False, True, "11", "3.105", "", "210.8", "-2.8"),
            (False, True, "10", "3.105", "", "210.8", "-2.8"),
            (False, True, "9", "3.105", "", "210.8", "-2.8"),
            (False, False, "8", "2.447", "", "98.2", "1.4"),
            (False, False, "7", "5.920", "", "45.0", "-12.6"),
            (False, False, "6", "-", "E", "-", "-"),
        ]
        ry = sy + 6
        for sel, nav, *vals in rows:
            if sel:
                self.set_fill_color(*LV_SEL)
            elif nav:
                self.set_fill_color(*LV_NAV)
            else:
                self.set_fill_color(*LV_BG if rows.index((sel, nav, *vals)) % 2 == 0 else LV_ROW_EVEN)
            # simpler alternate
            self.rect(sx + 1, ry, sw - 2, 4.2, "F")
            self.set_font("B", "", 4)
            self.set_text_color(*LV_ACCENT)
            cx = sx + 2
            widths = [10, 18, 8, 16, 14]
            for i, (v, cw) in enumerate(zip(vals, widths)):
                if i == 0:
                    self.set_text_color(*LV_ACCENT)
                    self.set_font("B", "B", 4)
                elif i == 2 and v == "E":
                    self.set_text_color(*LV_BTN_DEL)
                    self.set_font("B", "B", 4)
                else:
                    self.set_text_color(*LV_TEXT)
                    self.set_font("B", "", 4)
                self.set_xy(cx, ry + 0.5)
                self.cell(cw, 3.5, v)
                cx += cw
            ry += 4.2
        # buttons
        by = sy + sh - 8
        bw = (sw - 6) / 4
        for i, (label, col) in enumerate([
            ("DEL", LV_BTN_DEL), ("CLR", LV_BTN_CLR),
            ("SAVE", LV_BTN_SAVE), ("TX", LV_BTN_TX),
        ]):
            self.set_fill_color(*col)
            self.rect(sx + 1.5 + i * (bw + 0.5), by, bw, 5.5, "F")
            self.set_font("B", "B", 4.5)
            self.set_text_color(*WHITE)
            self.set_xy(sx + 1.5 + i * (bw + 0.5), by + 1)
            self.cell(bw, 4, label, align="C")
        if aim:
            self.set_font("B", "", 4)
            self.set_text_color(*LV_GREY)
            self.set_xy(sx + 2, by - 4)
            self.cell(sw - 4, 3.5, "AIM - press again to CAPTURE")

    def ui_view(self, sx, sy, sw, sh):
        # PLAN card
        self.set_fill_color(*WHITE)
        self.set_draw_color(*LV_BORDER)
        self.set_line_width(0.2)
        self.rect(sx + 2, sy + 2, sw - 4, sh * 0.45, "FD")
        self.set_font("B", "B", 4.5)
        self.set_text_color(*LV_TEXT)
        self.set_xy(sx + 3.5, sy + 3)
        self.cell(40, 4, "PLAN  azi   2 leg")
        # simple plan drawing
        ax, ay = sx + 12, sy + 28
        bx, by = sx + sw - 18, sy + 16
        self.set_draw_color(*LV_ACCENT)
        self.set_line_width(0.7)
        self.line(ax, ay, bx, by)
        self.set_fill_color(46, 125, 50)
        self.ellipse(ax - 1.5, ay - 1.5, 3, 3, "F")
        self.ellipse(bx - 1.5, by - 1.5, 3, 3, "F")
        self.set_draw_color(144, 164, 174)
        self.set_line_width(0.35)
        self.line(ax, ay, ax + 10, ay - 10)
        self.line(ax, ay, ax - 6, ay - 8)
        self.line(bx, by, bx + 8, by - 6)
        self.line(bx, by, bx + 10, by + 4)
        self.set_font("B", "", 3.5)
        self.set_text_color(*LV_GREY)
        self.set_xy(sx + sw - 16, sy + sh * 0.45 - 3)
        self.cell(10, 3, "5 m")
        # PROFILE card
        py = sy + sh * 0.48
        self.set_fill_color(*WHITE)
        self.set_draw_color(*LV_BORDER)
        self.rect(sx + 2, py, sw - 4, sh * 0.45, "FD")
        self.set_font("B", "B", 4.5)
        self.set_text_color(*LV_TEXT)
        self.set_xy(sx + 3.5, py + 1.5)
        self.cell(50, 4, "PROFILE  inc   6 splay")
        self.set_draw_color(*LV_TEAL)
        self.set_line_width(0.7)
        self.line(sx + 12, py + 22, sx + sw - 18, py + 14)
        self.set_fill_color(46, 125, 50)
        self.ellipse(sx + 10.5, py + 20.5, 3, 3, "F")
        self.ellipse(sx + sw - 19.5, py + 12.5, 3, 3, "F")

    def ui_sensor(self, sx, sy, sw, sh):
        cards = [
            ("LASER", "3.245 m", LV_TEAL),
            ("IMU", "Az 212.4\nInc -3.1", (63, 81, 181)),
            ("LINK", "laser OK\nimu OK", LV_ACCENT),
            ("BUTTON", "2-tap ready", (230, 81, 0)),
        ]
        cw = (sw - 5) / 2
        ch = 14
        for i, (title, val, col) in enumerate(cards):
            col_i, row_i = i % 2, i // 2
            x = sx + 2 + col_i * (cw + 1)
            y = sy + 2 + row_i * (ch + 1.5)
            self.set_fill_color(*col)
            self.rect(x, y, cw, ch, "F")
            self.set_font("B", "B", 4)
            self.set_text_color(*WHITE)
            self.set_xy(x + 1.5, y + 1.5)
            self.cell(cw - 3, 3.5, title)
            self.set_font("B", "B", 5)
            self.set_xy(x + 1.5, y + 5.5)
            self.multi_cell(cw - 3, 3.2, val.split("\n")[0] if "\n" in val else val)
        # temp full width
        self.set_fill_color(46, 125, 50)
        self.rect(sx + 2, sy + 2 + 2 * (ch + 1.5), sw - 4, 10, "F")
        self.set_font("B", "B", 4)
        self.set_text_color(*WHITE)
        self.set_xy(sx + 3.5, sy + 3 + 2 * (ch + 1.5))
        self.cell(sw - 6, 3.5, "TEMP / BAT")
        self.set_font("B", "B", 5)
        self.set_xy(sx + 3.5, sy + 7 + 2 * (ch + 1.5))
        self.cell(sw - 6, 3.5, "38.2 C   |   3.91 V  ·  78%")

    def ui_setup_measure(self, sx, sy, sw, sh):
        self.set_fill_color(*LV_TOP)
        self.rect(sx, sy, sw, 6, "F")
        subs = ["Bright", "Cal", "BT", "WiFi", "About"]
        tw = sw / 5
        for i, s in enumerate(subs):
            self.set_font("B", "B", 3.8)
            on = s == "Cal"
            if on:
                self.set_fill_color(*LV_ACCENT)
                self.rect(sx + i * tw + 0.5, sy + 0.5, tw - 1, 5, "F")
                self.set_text_color(*WHITE)
            else:
                self.set_text_color(*LV_GREY)
            self.set_xy(sx + i * tw, sy + 1.2)
            self.cell(tw, 3.5, s, align="C")
        self.set_font("B", "B", 6)
        self.set_text_color(*LV_ACCENT)
        self.set_xy(sx + 3, sy + 9)
        self.cell(sw - 6, 5, "Measure button")
        modes = ["2-tap", "1-tap", "Cont"]
        mw = (sw - 8) / 3
        for i, m in enumerate(modes):
            on = m == "2-tap"
            self.set_fill_color(*(LV_ACCENT if on else LV_TOP))
            self.set_draw_color(*LV_BORDER)
            self.rect(sx + 2 + i * (mw + 1), sy + 16, mw, 7, "FD")
            self.set_font("B", "B", 5)
            self.set_text_color(*(WHITE if on else LV_TEXT))
            self.set_xy(sx + 2 + i * (mw + 1), sy + 17.5)
            self.cell(mw, 4, m, align="C")
        self.set_fill_color(*LV_TEAL)
        self.rect(sx + 2, sy + 26, sw - 4, 7, "F")
        self.set_font("B", "B", 5)
        self.set_text_color(*WHITE)
        self.set_xy(sx + 2, sy + 27.5)
        self.cell(sw - 4, 4, "Hold 5s nav ON", align="C")
        self.set_font("B", "", 3.8)
        self.set_text_color(*LV_GREY)
        self.set_xy(sx + 3, sy + 36)
        self.multi_cell(sw - 6, 3.5, "2-tap: mira e mede. 1-tap: um toque. Cont: continuo. Hold 5s: avanca estacao.")

    # ── pages ──
    def page_cover(self):
        self.cover_mode = True
        self.add_page()
        self.set_fill_color(10, 10, 10)
        self.rect(0, 0, 210, 297, "F")
        self.set_fill_color(*RED)
        self.rect(0, 0, 210, 5, "F")
        if HERO.is_file():
            try:
                self.image(str(HERO), x=-8, y=28, w=226)
            except Exception:
                pass
        self.set_fill_color(10, 10, 10)
        self.rect(0, 188, 210, 109, "F")
        self.set_fill_color(*RED)
        self.rect(0, 188, 210, 1.2, "F")
        if LOGO_MARK.is_file():
            try:
                self.image(str(LOGO_MARK), x=16, y=198, w=28)
            except Exception:
                pass
        self.set_xy(16, 230)
        self.set_font("B", "B", 11)
        self.set_text_color(*RED)
        self.cell(0, 6, "MIRA  —  MAPEAMENTO E ROBÓTICA")
        self.set_xy(16, 238)
        self.set_font("B", "B", 34)
        self.set_text_color(*WHITE)
        self.cell(0, 14, "MM1-BLACK")
        self.set_xy(16, 253)
        self.set_font("B", "", 14)
        self.set_text_color(220, 220, 220)
        self.cell(0, 7, "Trena 2D  ·  Manual do usuário")
        self.set_xy(16, 262)
        self.set_font("B", "", 9)
        self.set_text_color(160, 160, 160)
        self.cell(0, 5, "Rev. 2026-09-19")
        self.set_xy(16, 272)
        self.set_font("B", "", 9)
        self.set_text_color(140, 140, 140)
        self.cell(0, 5, "www.mirarobotica.com")
        self.cover_mode = False

    def page_intro(self):
        self.add_page()
        self.h1("1. Sobre este manual")
        self.body(
            "Este manual contém as instruções necessárias para preparar, operar e configurar "
            "a MM1-BLACK. Leia as orientações de segurança antes do primeiro uso."
        )
        self.body(
            "Caminhos de menu são indicados por setas, como SETUP → Cal → Measure. "
            "Nomes em destaque identificam abas, botões e opções exibidos na tela."
        )
        self.photo(STUDIO, w=128, caption="MM1-BLACK — Trena 2D MIRA")
        self.h2("Recursos e especificações")
        self.body(
            "A MM1-BLACK registra distância, azimute e inclinação para levantamentos 2D em "
            "cavernas e minas. Os pontos podem ser visualizados, revisados e organizados no "
            "equipamento antes da transferência."
        )
        self.bullet([
            "Medição linear — alcance de até 50 m, com precisão de 2 mm.",
            "Orientação — azimute e inclinação com autocalibração e tratamento de ruído magnético.",
            "Interface — tela touch de 4,3″, resolução de 480 × 800 pixels.",
            "Visualização — planta e perfil atualizados durante a captura e ao abrir um arquivo.",
            "Autonomia — até 10 horas de operação, com recarga USB-C.",
            "Captura — modos 2-tap, 1-tap e contínuo.",
            "Atualizações — firmware atualizado pela internet.",
        ])

    def page_start(self):
        self.add_page()
        self.h1("2. Preparar para uso")
        self.body(
            "Execute estas etapas no início de cada levantamento. Ligue a MM1-BLACK e aguarde "
            "até que a tela principal seja exibida."
        )
        self.step(1, "Abra SENSOR. Acione o botão e confirme a resposta do laser e dos sensores de orientação. Verifique também a bateria.")
        self.step(2, "Abra FILE. Toque em NEW para criar um arquivo ou selecione um arquivo existente e toque em USE.")
        self.step(3, "Confirme o arquivo ativo. Use DEL somente quando quiser excluir permanentemente o item selecionado.")
        self.step(4, "Abra VIEW. Selecione PLAN para a vista em planta ou PROFILE para o perfil.")
        self.step(5, "Abra POINTS para revisar as leituras e acessar DEL, CLR, SAVE e TX.")
        self.step(6, "Abra SETUP quando precisar alterar o modo de captura ou verificar a autocalibração.")
        self.callout("IMPORTANTE — Toque em SAVE antes de desligar o equipamento ou selecionar outro arquivo.")

    def page_screen_legend(self):
        self.add_page()
        self.h1("3. Utilizar a interface")
        self.body("Os indicadores no topo da tela apresentam o estado atual do equipamento:")
        self.bullet([
            "Bateria — verde: carga adequada; amarelo: recarregue em breve; vermelho: recarregue.",
            "SD verde — armazenamento disponível.",
            "LINK — conexão Bluetooth ativa.",
            "Horário — relógio do equipamento.",
        ])
        self.body(
            "Abas: POINTS (leituras) · VIEW (visualização) · SENSOR (verificação) · "
            "FILE (arquivos) · SETUP (configuração)."
        )
        self.h2("POINTS — revisar e gravar")
        self.body(
            "Cada linha reúne distância, azimute e inclinação. DEL remove o ponto selecionado; "
            "CLR limpa a lista da sessão; SAVE grava a sessão e as alterações no arquivo ativo; "
            "TX transmite os pontos ao aplicativo conectado."
        )
        if not self.fig("ui-points.png", w=78, caption="Figura — aba POINTS (lista de medições)"):
            self.draw_device(55, self.get_y(), 100, 145, "POINTS",
                            lambda a, b, c, d: self.ui_points(a, b, c, d))
            self.set_y(self.get_y() + 150)

        self.add_page()
        self.h2("VIEW e SENSOR")
        self.body(
            "VIEW apresenta o levantamento durante a captura e ao abrir um arquivo. PLAN exibe "
            "a planta e PROFILE, o perfil. SENSOR permite verificar os sistemas de medição antes do uso."
        )
        if not self.fig_pair(
            "ui-view.png", "ui-sensor.png", w=78,
            cap_a="VIEW — PLAN e PROFILE",
            cap_b="SENSOR — laser, IMU, link, botão",
        ):
            self.draw_device(30, self.get_y(), 70, 105, "VIEW",
                            lambda a, b, c, d: self.ui_view(a, b, c, d))
            self.draw_device(110, self.get_y(), 70, 105, "SENSOR",
                            lambda a, b, c, d: self.ui_sensor(a, b, c, d))
            self.set_y(self.get_y() + 110)

    def page_measure(self):
        self.add_page()
        self.h1("4. Capturar pontos e estações")
        self.body(
            "Mantenha a MM1-BLACK estável durante a leitura. Capturas comuns são associadas à "
            "estação atual; capturas de navegação definem a próxima estação."
        )
        self.h2("Capturar um ponto")
        self.step(1, "Posicione o equipamento na estação atual e aponte o laser para o alvo.")
        self.step(2, "Acione o botão de acordo com o modo selecionado.")
        self.step(3, "Mantenha o equipamento imóvel até o sinal sonoro de sucesso ou falha.")
        self.step(4, "Confirme o registro em POINTS ou acompanhe sua posição em VIEW.")
        self.h2("Avançar para uma nova estação")
        self.step(1, "Abra SETUP → Cal → Measure e ative Hold 5s nav.")
        self.step(2, "Aponte o laser para a posição da próxima estação.")
        self.step(3, "Mantenha o botão pressionado por aproximadamente cinco segundos.")
        self.step(4, "Aguarde as três leituras. Quando são consistentes, elas formam a nova estação e atualizam o percurso em VIEW.")
        self.body(
            "Capturas comuns e o modo contínuo permanecem associados à estação atual. "
            "Somente o comando Hold 5s nav avança a estação."
        )
        self.h2("Escolher o modo de captura")
        self.bullet([
            "2-tap — o primeiro toque liga o laser; o segundo realiza a captura.",
            "1-tap — um toque liga o laser e inicia a captura.",
            "Contínuo — inicia uma sequência de capturas; pressione novamente para interromper.",
        ])
        self.callout(
            "LEITURA DESCARTADA — estabilize o equipamento, verifique SENSOR e repita a captura. "
            "Movimento brusco, baixa qualidade de orientação ou interferência elevada podem invalidar a leitura.",
            "warn",
        )
        self.h2("Modo 2-tap")
        ya = self.get_y()
        if ya + 95 > 270:
            self.add_page()
            ya = self.get_y()
        self.draw_device(28, ya, 72, 88, "POINTS",
                         lambda a, b, c, d: self.ui_points(a, b, c, d, aim=True))
        self.draw_device(110, ya, 72, 88, "POINTS",
                         lambda a, b, c, d: self.ui_points(a, b, c, d, ok=True))
        self.set_y(ya + 90)
        self.set_font("B", "", 8)
        self.set_text_color(*MID)
        self.cell(89, 5, "Primeiro toque — laser ativo", align="C")
        self.cell(89, 5, "Segundo toque — resultado", align="C", new_x="LMARGIN", new_y="NEXT")
        self.ln(3)

    def page_view_sensor(self):
        self.add_page()
        self.h1("5. VIEW e SENSOR")
        self.h2("VIEW — acompanhar o levantamento")
        self.body(
            "Use VIEW para acompanhar a distribuição dos pontos durante a captura ou consultar "
            "o layout de um arquivo já existente. PLAN apresenta a planta; PROFILE, o perfil. "
            "Pontos verdes representam estações, linhas espessas indicam o percurso e linhas finas, os demais pontos."
        )
        if not self.fig("ui-view.png", w=78, caption="Figura — aba VIEW"):
            self.draw_device(55, self.get_y(), 90, 130, "VIEW",
                            lambda a, b, c, d: self.ui_view(a, b, c, d))
            self.set_y(self.get_y() + 135)
        self.h2("SENSOR — verificar o equipamento")
        self.body(
            "Antes de iniciar o levantamento, confirme o funcionamento do laser, dos sensores "
            "de orientação, do botão de captura e da bateria."
        )
        if not self.fig("ui-sensor.png", w=78, caption="Figura — aba SENSOR"):
            self.draw_device(55, self.get_y(), 90, 120, "SENSOR",
                            lambda a, b, c, d: self.ui_sensor(a, b, c, d))
            self.set_y(self.get_y() + 125)

    def page_station(self):
        pass

    def page_settings(self):
        self.add_page()
        self.h1("5. Configurar a MM1-BLACK")
        self.body(
            "Abra SETUP para ajustar a interface, consultar a autocalibração, selecionar o modo "
            "de captura e gerenciar as conexões."
        )
        self.h2("Bright — ajustar tela e som")
        self.bullet([
            "Brilho — ajuste a iluminação às condições do ambiente; valores menores ajudam a ampliar a autonomia.",
            "Volume — define o nível dos sinais de confirmação e falha; 0 desativa os sons.",
            "Tema — alterna entre as interfaces clara e escura.",
        ])
        self.fig("ui-setup-bright.png", w=72, caption="Figura — SETUP → Bright")
        self.h2("Cal — verificar sensores e captura")
        self.bullet([
            "IMU — consulte a qualidade da autocalibração. Se necessário, afaste-se de grandes massas metálicas e faça movimentos em forma de “8” por aproximadamente 30 segundos.",
            "Laser — toque em Test para verificar o retorno da medição de distância.",
            "Trim — informe o ajuste fino da distância, em milímetros.",
            "Measure — selecione 2-tap, 1-tap ou contínuo e configure Hold 5s nav.",
        ])
        self.h2("About")
        self.body("Exibe o nome do produto, a versão instalada e o QR Code do instalador.")

    def page_phone(self):
        self.add_page()
        self.h1("6. Transferir dados ao celular")
        self.step(1, "Ative o Bluetooth do celular e abra o TopoDroid ou o SexyTopo.")
        self.step(2, "Na MM1-BLACK, acesse SETUP → BT.")
        self.step(3, "No aplicativo, selecione SAP6_0001.")
        self.step(4, "Aguarde até que o indicador LINK apareça no topo da tela.")
        self.step(5, "Realize uma captura de teste e confirme o recebimento no aplicativo.")
        self.step(6, "Para transmitir os pontos da sessão, abra POINTS e toque em TX.")
        if not self.fig("ui-bt.png", w=72, caption="Figura — SETUP → BT (SAP6_0001)"):
            pass
        self.callout(
            "Se a MM1-BLACK estiver operando como ponto de acesso Wi-Fi, desative esse modo "
            "antes do pareamento Bluetooth.",
            "warn",
        )
        self.h2("Atualizar o firmware")
        self.step(1, "Abra WiFi e conecte a MM1-BLACK a uma rede.")
        self.step(2, "Toque em Check para procurar uma nova versão.")
        self.step(3, "Se houver uma atualização, toque em Install e mantenha o equipamento ligado até a conclusão.")
        self.step(4, "Abra About para confirmar a versão instalada.")

    def page_safety_faq(self):
        self.add_page()
        self.h1("7. Segurança e conservação")
        self.bullet([
            "Laser: não olhe diretamente para o feixe nem o direcione para os olhos.",
            "Mantenha a janela do laser limpa. Use um pano macio e seco; não aplique produtos abrasivos.",
            "Não desmonte nem modifique o equipamento.",
            "Proteja a tela contra impactos e objetos pontiagudos.",
            "Verifique a carga da bateria antes de jornadas prolongadas.",
            "Para períodos longos sem uso, armazene o equipamento limpo e em local seco.",
        ])
        self.h1("8. Solução de problemas")
        faqs = [
            ("A captura não é concluída", "1. Limpe e desobstrua a janela do laser. 2. Evite incidência solar extrema. 3. Verifique SENSOR. 4. Estabilize o equipamento. 5. Consulte SETUP → Cal → IMU e repita."),
            ("Azimute ou inclinação inconsistentes", "Abra SETUP → Cal → IMU. Afaste o equipamento de fontes de interferência magnética e execute novamente a autocalibração."),
            ("Pontos não aparecem após religar", "A sessão não foi salva. Abra POINTS e toque em SAVE antes de desligar ou trocar de arquivo."),
            ("O celular não encontra a MM1-BLACK", "Confirme o Bluetooth do celular. Abra SETUP → BT, selecione Restart BLE e desative o ponto de acesso Wi-Fi do equipamento."),
            ("A tela está difícil de visualizar", "Abra SETUP → Bright. Ajuste o brilho e, se necessário, alterne entre os temas."),
        ]
        for q, a in faqs:
            self.set_x(self.l_margin)
            self.set_font("B", "B", 10)
            self.set_text_color(*CHARCOAL)
            self.multi_cell(0, 5, q)
            self.ln(0.5)
            self.set_x(self.l_margin)
            self.set_font("B", "", 9.5)
            self.set_text_color(*BLACK)
            self.multi_cell(0, 5, a)
            self.ln(2)

        self.h2("Contato MIRA")
        self.body(
            "MIRA — Mapeamento e Robótica Ltda.\n"
            "Belo Horizonte / MG\n"
            "contato@mirarobotica.com\n"
            "www.mirarobotica.com"
        )
        self.ln(4)
        self.set_font("B", "", 8)
        self.set_text_color(*MID)
        self.multi_cell(
            0, 4.5,
            "MM1-BLACK · Trena 2D · Manual do usuário · Rev. 2026-09-19\n"
            "© MIRA — Mapeamento e Robótica"
        )


def main():
    pdf = Manual()
    pdf.page_cover()
    pdf.page_intro()
    pdf.page_start()
    pdf.page_screen_legend()
    pdf.page_measure()
    pdf.page_settings()
    pdf.page_phone()
    pdf.page_safety_faq()
    pdf.output(str(OUT_PDF))
    print(f"OK {OUT_PDF} ({OUT_PDF.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
