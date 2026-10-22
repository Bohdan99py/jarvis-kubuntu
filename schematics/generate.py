#!/usr/bin/env python3
"""Example circuits of the Electronics skill: one place for the drawing (SVG),
the parts, the nets and a SPICE deck of every circuit, so they never disagree.

Run from the repository root after editing: python3 schematics/generate.py
It writes schematics/<id>.svg and schematics/examples.json (both committed and
bundled into Jarvis). Pin numbers and symbols follow the KiCad libraries, so
the exported netlist can be imported into Pcbnew (File → Import → Netlist).
Symbols are drawn in the IEC/ГОСТ style: a resistor is a rectangle."""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent

BG, WIRE, INK, REF, VALUE, NET = "#0f1724", "#8fb3d9", "#e6eef8", "#7fd1c7", "#f2c46d", "#c792ea"
FONT = "DejaVu Sans, Noto Sans, sans-serif"


class Sheet:
    def __init__(self, width, height):
        self.w, self.h, self.items = width, height, []

    def add(self, item):
        self.items.append(item)

    def wire(self, *points):
        path = " ".join(f"{x},{y}" for x, y in points)
        self.add(f'<polyline points="{path}" fill="none" stroke="{WIRE}" stroke-width="2" stroke-linejoin="round"/>')

    def dot(self, x, y):
        self.add(f'<circle cx="{x}" cy="{y}" r="3.5" fill="{WIRE}"/>')

    def text(self, x, y, text, color=INK, size=12, anchor="middle", weight="normal"):
        text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        self.add(f'<text x="{x}" y="{y}" fill="{color}" font-size="{size}" font-family="{FONT}" '
                 f'text-anchor="{anchor}" font-weight="{weight}">{text}</text>')

    def labels(self, x, y, ref, value, side):
        """Reference and value next to a part: above it, or right/left of it."""
        if side == "above":
            self.text(x, y - 22, ref, REF, 12, "middle", "bold")
            self.text(x, y + 26, value, VALUE, 11)
        elif side == "right":
            self.text(x + 18, y - 3, ref, REF, 12, "start", "bold")
            self.text(x + 18, y + 12, value, VALUE, 11, "start")
        else:
            self.text(x - 18, y - 3, ref, REF, 12, "end", "bold")
            self.text(x - 18, y + 12, value, VALUE, 11, "end")

    def two(self, kind, p1, p2, ref, value, side=None):
        """A two-pin part between p1 and p2 (a horizontal or vertical line).
        Polarised parts: p1 is the anode / plus."""
        (x1, y1), (x2, y2) = p1, p2
        cx, cy = (x1 + x2) / 2, (y1 + y2) / 2
        horizontal = y1 == y2
        length = abs(x2 - x1) if horizontal else abs(y2 - y1)
        half = BODY[kind][0]
        sign = 1 if (x2 > x1 if horizontal else y2 > y1) else -1
        angle = (0 if sign > 0 else 180) if horizontal else (90 if sign > 0 else -90)
        if length / 2 > half:
            if horizontal:
                self.wire((x1, y1), (cx - sign * half, cy))
                self.wire((cx + sign * half, cy), (x2, y2))
            else:
                self.wire((x1, y1), (cx, cy - sign * half))
                self.wire((cx, cy + sign * half), (x2, y2))
        self.add(f'<g transform="translate({cx},{cy}) rotate({angle})" stroke="{INK}" stroke-width="2" fill="none">'
                 + BODY[kind][1] + "</g>")
        self.labels(cx, cy, ref, value, side or ("above" if horizontal else "right"))

    def ground(self, x, y):
        self.wire((x, y), (x, y + 8))
        self.add(f'<g stroke="{INK}" stroke-width="2"><line x1="{x-12}" y1="{y+8}" x2="{x+12}" y2="{y+8}"/>'
                 f'<line x1="{x-7}" y1="{y+13}" x2="{x+7}" y2="{y+13}"/><line x1="{x-3}" y1="{y+18}" x2="{x+3}" y2="{y+18}"/></g>')

    def supply(self, x, y, name):
        """A power flag: a bar with the net name above it; wires go down from (x, y)."""
        self.add(f'<line x1="{x-10}" y1="{y}" x2="{x+10}" y2="{y}" stroke="{INK}" stroke-width="2"/>')
        self.text(x, y - 7, name, NET, 12, "middle", "bold")

    def flag(self, x, y, name, anchor="start"):
        """A net label on a wire end."""
        self.text(x + (6 if anchor == "start" else -6), y + 4, name, NET, 12, anchor, "bold")

    def box(self, x, y, w, h, ref, value, pins):
        """An IC: pins are (side, offset, number, name); returns {number: (x, y)} of the pin ends."""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="4" fill="#16233a" stroke="{INK}" stroke-width="2"/>')
        if any(p[0] == "top" for p in pins):  # the top is taken: the names go beside the box
            self.text(x + w + 8, y + 8, ref, REF, 12, "start", "bold")
            self.text(x + w + 8, y + 22, value, VALUE, 11, "start")
        else:
            self.text(x + w / 2, y - 22, ref, REF, 12, "middle", "bold")
            self.text(x + w / 2, y - 8, value, VALUE, 11)
        ends = {}
        for side, offset, number, name in pins:
            if side == "left":
                px, py, ex, ey = x, y + offset, x - 20, y + offset
                self.text(x + 6, py + 4, name, INK, 10, "start")
                self.text(x - 4, py - 4, str(number), WIRE, 9, "end")
            elif side == "right":
                px, py, ex, ey = x + w, y + offset, x + w + 20, y + offset
                self.text(x + w - 6, py + 4, name, INK, 10, "end")
                self.text(x + w + 4, py - 4, str(number), WIRE, 9, "start")
            elif side == "top":
                px, py, ex, ey = x + offset, y, x + offset, y - 20
                self.text(px, y + 14, name, INK, 10)
                self.text(px + 4, y - 6, str(number), WIRE, 9, "start")
            else:
                px, py, ex, ey = x + offset, y + h, x + offset, y + h + 20
                self.text(px, y + h - 6, name, INK, 10)
                self.text(px + 4, y + h + 12, str(number), WIRE, 9, "start")
            self.add(f'<line x1="{px}" y1="{py}" x2="{ex}" y2="{ey}" stroke="{INK}" stroke-width="2"/>')
            ends[number] = (ex, ey)
        return ends

    def npn(self, cx, cy, ref, value):
        """Pins: B (left), C (top), E (bottom)."""
        self.add(f'<g stroke="{INK}" stroke-width="2" fill="none"><circle cx="{cx}" cy="{cy}" r="20"/>'
                 f'<line x1="{cx-30}" y1="{cy}" x2="{cx-8}" y2="{cy}"/><line x1="{cx-8}" y1="{cy-12}" x2="{cx-8}" y2="{cy+12}" stroke-width="3"/>'
                 f'<line x1="{cx-8}" y1="{cy-5}" x2="{cx+8}" y2="{cy-16}"/><line x1="{cx+8}" y1="{cy-16}" x2="{cx+8}" y2="{cy-30}"/>'
                 f'<line x1="{cx-8}" y1="{cy+5}" x2="{cx+8}" y2="{cy+16}"/><line x1="{cx+8}" y1="{cy+16}" x2="{cx+8}" y2="{cy+30}"/></g>'
                 f'<polygon points="{cx+8},{cy+16} {cx-1},{cy+15} {cx+3},{cy+8}" fill="{INK}"/>')
        self.labels(cx + 14, cy, ref, value, "right")
        return {"B": (cx - 30, cy), "C": (cx + 8, cy - 30), "E": (cx + 8, cy + 30)}

    def nmos(self, cx, cy, ref, value):
        """Pins: G (left), D (top), S (bottom); the body diode is drawn."""
        self.add(f'<g stroke="{INK}" stroke-width="2" fill="none"><circle cx="{cx}" cy="{cy}" r="22"/>'
                 f'<line x1="{cx-30}" y1="{cy+10}" x2="{cx-11}" y2="{cy+10}"/><line x1="{cx-11}" y1="{cy-12}" x2="{cx-11}" y2="{cy+12}"/>'
                 f'<line x1="{cx-5}" y1="{cy-14}" x2="{cx-5}" y2="{cy-6}" stroke-width="3"/><line x1="{cx-5}" y1="{cy-4}" x2="{cx-5}" y2="{cy+4}" stroke-width="3"/>'
                 f'<line x1="{cx-5}" y1="{cy+6}" x2="{cx-5}" y2="{cy+14}" stroke-width="3"/>'
                 f'<polyline points="{cx-5},{cy-10} {cx+8},{cy-10} {cx+8},{cy-30}"/><polyline points="{cx-5},{cy+10} {cx+8},{cy+10} {cx+8},{cy+30}"/>'
                 f'<line x1="{cx-5}" y1="{cy}" x2="{cx+8}" y2="{cy}"/><line x1="{cx+8}" y1="{cy}" x2="{cx+8}" y2="{cy+10}"/></g>'
                 f'<polygon points="{cx-5},{cy} {cx+1},{cy-4} {cx+1},{cy+4}" fill="{INK}"/>')
        self.labels(cx + 16, cy, ref, value, "right")
        return {"G": (cx - 30, cy + 10), "D": (cx + 8, cy - 30), "S": (cx + 8, cy + 30)}

    def opamp(self, cx, cy, ref, value, plus_top=False):
        """Pins: minus and plus inputs (left), out (right)."""
        top, bottom = ("+", "−") if plus_top else ("−", "+")
        self.add(f'<polygon points="{cx-28},{cy-34} {cx-28},{cy+34} {cx+32},{cy}" fill="#16233a" stroke="{INK}" stroke-width="2"/>'
                 f'<g stroke="{INK}" stroke-width="2"><line x1="{cx-48}" y1="{cy-16}" x2="{cx-28}" y2="{cy-16}"/>'
                 f'<line x1="{cx-48}" y1="{cy+16}" x2="{cx-28}" y2="{cy+16}"/><line x1="{cx+32}" y1="{cy}" x2="{cx+50}" y2="{cy}"/></g>')
        self.text(cx - 19, cy - 11, top, INK, 15, "middle", "bold")
        self.text(cx - 19, cy + 21, bottom, INK, 15, "middle", "bold")
        self.text(cx + 14, cy - 34, ref, REF, 12, "start", "bold")
        self.text(cx, cy + 50, value, VALUE, 11)
        upper, lower = (cx - 48, cy - 16), (cx - 48, cy + 16)
        return {"+": upper if plus_top else lower, "-": lower if plus_top else upper, "out": (cx + 50, cy)}

    def svg(self, title):
        body = "\n".join(self.items)
        return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {self.w} {self.h}" width="{self.w}" height="{self.h}">\n'
                f'<title>{title}</title>\n<rect width="{self.w}" height="{self.h}" rx="14" fill="{BG}"/>\n{body}\n</svg>\n')


# Two-pin bodies drawn along +x around (0, 0): half length, SVG.
ARROWS = ('<g stroke-width="1.6"><line x1="2" y1="-14" x2="10" y2="-24"/><line x1="8" y1="-12" x2="16" y2="-22"/></g>'
          '<polygon points="10,-24 5,-22 9,-19" fill="#e6eef8" stroke="none"/><polygon points="16,-22 11,-20 15,-17" fill="#e6eef8" stroke="none"/>')
BODY = {
    "R": (22, '<rect x="-22" y="-8" width="44" height="16" fill="#16233a"/>'),
    "C": (5, '<line x1="-5" y1="-14" x2="-5" y2="14"/><line x1="5" y1="-14" x2="5" y2="14"/>'),
    "CP": (5, '<line x1="-5" y1="-14" x2="-5" y2="14"/><rect x="3" y="-14" width="5" height="28" fill="#e6eef8"/>'
              '<text x="-14" y="-12" fill="#e6eef8" stroke="none" font-size="12" font-family="sans-serif" text-anchor="middle">+</text>'),
    "D": (10, '<polygon points="-10,-11 -10,11 9,0" fill="#16233a"/><line x1="10" y1="-11" x2="10" y2="11"/>'),
    "LED": (10, '<polygon points="-10,-11 -10,11 9,0" fill="#16233a"/><line x1="10" y1="-11" x2="10" y2="11"/>' + ARROWS),
    "Z": (10, '<polygon points="-10,-11 -10,11 9,0" fill="#16233a"/><polyline points="5,-14 10,-11 10,11 15,14"/>'),
    "L": (24, '<path d="M-24,0 a6,6 0 0 1 12,0 a6,6 0 0 1 12,0 a6,6 0 0 1 12,0 a6,6 0 0 1 12,0"/>'),
    "SW": (18, '<circle cx="-14" cy="0" r="3"/><circle cx="14" cy="0" r="3"/><line x1="-18" y1="-9" x2="18" y2="-9"/>'
               '<line x1="0" y1="-9" x2="0" y2="-19"/><line x1="-6" y1="-19" x2="6" y2="-19"/>'),
    "M": (16, '<circle cx="0" cy="0" r="16" fill="#16233a"/><text x="0" y="5" fill="#e6eef8" stroke="none" font-size="14" '
              'font-family="sans-serif" text-anchor="middle" transform="rotate(-90)">M</text>'),
    "F": (16, '<rect x="-16" y="-6" width="32" height="12" fill="#16233a"/><line x1="-16" y1="0" x2="16" y2="0"/>'),
}

# KiCad symbols: lib_id, pins {number: function}.
SYMBOLS = {
    "R": ("Device:R", {"1": "", "2": ""}),
    "C": ("Device:C", {"1": "", "2": ""}),
    "CP": ("Device:C_Polarized", {"1": "+", "2": "-"}),
    "D": ("Device:D", {"1": "K", "2": "A"}),
    "LED": ("Device:LED", {"1": "K", "2": "A"}),
    "Z": ("Device:D_Zener", {"1": "K", "2": "A"}),
    "SW": ("Switch:SW_Push", {"1": "1", "2": "2"}),
    "M": ("Motor:Motor_DC", {"1": "+", "2": "-"}),
    "J2": ("Connector:Conn_01x02_Pin", {"1": "Pin_1", "2": "Pin_2"}),
    "J4": ("Connector:Conn_01x04_Pin", {"1": "Pin_1", "2": "Pin_2", "3": "Pin_3", "4": "Pin_4"}),
    "NPN": ("Device:Q_NPN_BCE", {"1": "B", "2": "C", "3": "E"}),
    "NMOS": ("Device:Q_NMOS_GDS", {"1": "G", "2": "D", "3": "S"}),
    "555": ("Timer:NE555P", {"1": "GND", "2": "TR", "3": "Q", "4": "R", "5": "CV", "6": "THR", "7": "DIS", "8": "VCC"}),
    "LM358": ("Amplifier_Operational:LM358", {"1": "", "2": "-", "3": "+", "4": "V-", "8": "V+"}),
    "AMS1117": ("Regulator_Linear:AMS1117-3.3", {"1": "GND", "2": "VO", "3": "VI"}),
    "7805": ("Regulator_Linear:L7805", {"1": "IN", "2": "GND", "3": "OUT"}),
    "BRIDGE": ("Diode_Bridge:DB107", {"1": "+", "2": "~", "3": "~", "4": "-"}),
}

FOOTPRINTS = {
    "R": "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
    "C": "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm",
    "CP": "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm",
    "D": "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal",
    "D4148": "Diode_THT:D_DO-35_SOD27_P7.62mm_Horizontal",
    "LED": "LED_THT:LED_D5.0mm",
    "Z": "Diode_THT:D_DO-35_SOD27_P7.62mm_Horizontal",
    "SW": "Button_Switch_THT:SW_PUSH_6mm",
    "M": "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
    "J2": "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
    "J4": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    "NPN": "Package_TO_SOT_THT:TO-92_Inline",
    "NMOS": "Package_TO_SOT_THT:TO-220-3_Vertical",
    "555": "Package_DIP:DIP-8_W7.62mm",
    "LM358": "Package_DIP:DIP-8_W7.62mm",
    "AMS1117": "Package_TO_SOT_SMD:SOT-223-3_TabPin2",
    "7805": "Package_TO_SOT_THT:TO-220-3_Vertical",
    "BRIDGE": "Diode_THT:Diode_Bridge_DIP-4_W7.62mm_P5.08mm",
}

EXAMPLES = []


def part(ref, kind, value, footprint=None):
    lib, pins = SYMBOLS[kind]
    return {"ref": ref, "value": value, "symbol": lib, "footprint": footprint or FOOTPRINTS[kind], "pins": pins}


def example(id, title, title_en, category, description, description_en, sheet, parts, nets, spice="", formulas=(), tags=()):
    EXAMPLES.append({"id": id, "title": title, "title_en": title_en, "category": category,
                     "description": description, "description_en": description_en,
                     "parts": parts, "nets": [{"name": n, "nodes": nodes} for n, nodes in nets],
                     "spice": spice.strip() + "\n" if spice else "", "formulas": list(formulas), "tags": list(tags),
                     "svg": sheet.svg(title_en)})


# ---- 1. LED with a current-limiting resistor -------------------------------------------------
s = Sheet(460, 260)
j = s.box(40, 90, 60, 70, "J1", "5 V", [("right", 20, 1, "+"), ("right", 50, 2, "−")])
s.wire(j[1], (170, 110))
s.two("R", (170, 110), (270, 110), "R1", "150 Ω")
s.wire((270, 110), (340, 110))
s.two("LED", (340, 110), (340, 200), "D1", "LED red, 20 mA", "left")
s.wire((340, 200), (340, 220), (120, 220), (120, 140), j[2])
s.ground(230, 220)
s.dot(230, 220)
s.supply(150, 80, "+5V")
s.wire((150, 80), (150, 110))
s.dot(150, 110)
example("led_resistor", "Светодиод с токоограничивающим резистором", "LED with a current-limiting resistor", "basics",
        "Простейшая цепь: резистор ограничивает ток светодиода. R = (U − Uf)/I = (5 − 2)/0,02 = 150 Ом, мощность резистора 60 мВт. "
        "Для синего и белого светодиода Uf ≈ 3 В → 100 Ом. Без резистора светодиод сгорит: его ток почти не зависит от напряжения после открытия.",
        "The simplest circuit: the resistor limits the LED current. R = (U − Uf)/I = (5 − 2)/0.02 = 150 Ω, the resistor dissipates 60 mW. "
        "Blue and white LEDs have Uf ≈ 3 V → 100 Ω. Without the resistor the LED burns: once it conducts, its current barely depends on the voltage.",
        s, [part("J1", "J2", "Power 5V"), part("R1", "R", "150"), part("D1", "LED", "LED_Red")],
        [("+5V", ["J1.1", "R1.1"]), ("Net-(D1-A)", ["R1.2", "D1.2"]), ("GND", ["D1.1", "J1.2"])],
        """* LED with a resistor
V1 vcc 0 DC 5
R1 vcc a 150
D1 a 0 LEDRED
.model LEDRED D(IS=1e-19 N=1.7 RS=2)
.op
.end""", ["R = (U − Uf) / I", "P = I²·R"], ["светодиод", "led", "резистор", "resistor"])

# ---- 2. Voltage divider ------------------------------------------------------------------------
s = Sheet(460, 300)
s.supply(160, 50, "+12V")
s.wire((160, 50), (160, 70))
s.two("R", (160, 70), (160, 150), "R1", "10 kΩ")
s.two("R", (160, 150), (160, 230), "R2", "4.7 kΩ")
s.ground(160, 230)
s.dot(160, 150)
s.wire((160, 150), (300, 150))
s.flag(300, 150, "VOUT ≈ 3.84 V")
s.text(330, 220, "Vout = Vin·R2/(R1+R2)", INK, 12)
example("voltage_divider", "Делитель напряжения", "Voltage divider", "basics",
        "Два резистора делят напряжение: Vout = 12·4,7/(10 + 4,7) ≈ 3,84 В. Делитель годится для измерения (вход АЦП, обратная связь), "
        "но не для питания: его выходное сопротивление R1‖R2 ≈ 3,2 кОм, и нагрузка проседает напряжение. Нагрузка должна быть в 10+ раз больше R2.",
        "Two resistors divide the voltage: Vout = 12·4.7/(10 + 4.7) ≈ 3.84 V. A divider suits measuring (an ADC input, feedback) "
        "but not powering things: its output resistance R1‖R2 ≈ 3.2 kΩ, so a load pulls the voltage down. The load should be 10+ times larger than R2.",
        s, [part("R1", "R", "10k"), part("R2", "R", "4.7k")],
        [("+12V", ["R1.1"]), ("VOUT", ["R1.2", "R2.1"]), ("GND", ["R2.2"])],
        """* Voltage divider
V1 vin 0 DC 12
R1 vin out 10k
R2 out 0 4.7k
.op
.end""", ["Vout = Vin·R2/(R1 + R2)", "Rout = R1·R2/(R1 + R2)"], ["делитель", "divider"])

# ---- 3. RC low-pass filter ---------------------------------------------------------------------
s = Sheet(480, 260)
s.flag(60, 110, "IN", "end")
s.wire((60, 110), (120, 110))
s.two("R", (120, 110), (240, 110), "R1", "10 kΩ")
s.wire((240, 110), (330, 110))
s.dot(290, 110)
s.two("C", (290, 110), (290, 200), "C1", "100 nF")
s.ground(290, 200)
s.flag(330, 110, "OUT")
s.text(240, 240, "fc = 1/(2π·R·C) ≈ 159 Hz", INK, 12)
example("rc_lowpass", "RC-фильтр нижних частот", "RC low-pass filter", "filters",
        "Пропускает низкие частоты и ослабляет высокие на 20 дБ/декаду выше частоты среза fc = 1/(2πRC) = 159 Гц. "
        "На частоте среза выход −3 дБ (0,707), сдвиг фазы −45°. Применения: сглаживание ШИМ в аналоговое напряжение, антиалиасинг перед АЦП, подавление дребезга.",
        "Passes low frequencies and attenuates high ones by 20 dB/decade above the cutoff fc = 1/(2πRC) = 159 Hz. "
        "At the cutoff the output is −3 dB (0.707) with −45° phase. Uses: turning PWM into a voltage, anti-aliasing before an ADC, debouncing.",
        s, [part("R1", "R", "10k"), part("C1", "C", "100n")],
        [("IN", ["R1.1"]), ("OUT", ["R1.2", "C1.1"]), ("GND", ["C1.2"])],
        """* RC low-pass filter: AC sweep
V1 in 0 AC 1
R1 in out 10k
C1 out 0 100n
.ac dec 20 1 100k
.end""", ["fc = 1 / (2π·R·C)", "τ = R·C"], ["фильтр", "rc", "filter", "lowpass"])

# ---- 4. NE555 astable blinker ------------------------------------------------------------------
s = Sheet(560, 380)
u = s.box(250, 110, 110, 150, "U1", "NE555", [
    ("top", 30, 8, "VCC"), ("top", 80, 4, "R"), ("left", 25, 7, "DIS"), ("left", 65, 6, "THR"), ("left", 105, 2, "TR"),
    ("right", 40, 3, "Q"), ("right", 110, 5, "CV"), ("bottom", 55, 1, "GND")])
s.supply(170, 40, "+9V")
s.wire((170, 40), (170, 60))
s.dot(170, 70)
s.wire((170, 60), (170, 70), (280, 70), u[8])
s.wire(u[4], (u[4][0], 70), (280, 70))
s.dot(280, 70)
s.two("R", (170, 70), (170, 135), "R1", "1 kΩ", "left")
s.wire((170, 135), u[7])
s.dot(170, 135)
s.two("R", (170, 135), (170, 205), "R2", "10 kΩ", "left")
s.wire((170, 205), (170, 215), u[2])
s.dot(170, 215)
s.wire(u[6], (200, 175), (200, 215))
s.dot(200, 215)
s.two("CP", (170, 215), (170, 300), "C1", "10 µF", "left")
s.ground(170, 300)
s.wire(u[1], (305, 300))
s.ground(305, 300)
s.two("C", (380, 220), (380, 300), "C2", "10 nF")
s.wire(u[5], (380, 220))
s.ground(380, 300)
s.wire(u[3], (420, 150))
s.two("R", (420, 150), (500, 150), "R3", "470 Ω")
s.two("LED", (500, 150), (500, 300), "D1", "LED", "left")
s.ground(500, 300)
s.text(280, 360, "f = 1.44/((R1+2·R2)·C1) ≈ 6.9 Hz", INK, 12)
example("ne555_astable", "Мигалка на NE555", "NE555 LED blinker", "timers",
        "NE555 в автоколебательном режиме: C1 заряжается через R1 + R2 до 2/3 питания и разряжается через R2 до 1/3. "
        "f = 1,44/((R1 + 2·R2)·C1) ≈ 6,9 Гц, скважность (R1 + R2)/(R1 + 2·R2) ≈ 52 %. Вывод 4 (сброс) на питании, на 5 — 10 нФ от помех. "
        "Выход 3 отдаёт до 200 мА — хватает на светодиод с резистором 470 Ом.",
        "The NE555 as an oscillator: C1 charges through R1 + R2 to 2/3 of the supply and discharges through R2 to 1/3. "
        "f = 1.44/((R1 + 2·R2)·C1) ≈ 6.9 Hz, duty (R1 + R2)/(R1 + 2·R2) ≈ 52 %. Pin 4 (reset) goes to the supply, pin 5 gets 10 nF against noise. "
        "Output 3 drives up to 200 mA — enough for an LED with 470 Ω.",
        s, [part("U1", "555", "NE555"), part("R1", "R", "1k"), part("R2", "R", "10k"), part("C1", "CP", "10u"),
            part("C2", "C", "10n"), part("R3", "R", "470"), part("D1", "LED", "LED")],
        [("+9V", ["U1.8", "U1.4", "R1.1"]), ("Net-(U1-DIS)", ["R1.2", "R2.1", "U1.7"]),
         ("Net-(U1-THR)", ["R2.2", "U1.6", "U1.2", "C1.1"]), ("Net-(U1-CV)", ["U1.5", "C2.1"]),
         ("Net-(U1-Q)", ["U1.3", "R3.1"]), ("Net-(D1-A)", ["R3.2", "D1.2"]), ("GND", ["U1.1", "C1.2", "C2.2", "D1.1"])],
        "", ["f = 1,44 / ((R1 + 2·R2)·C)", "tH = 0,693·(R1 + R2)·C", "tL = 0,693·R2·C"], ["555", "ne555", "мигалка", "blinker", "таймер", "timer"])

# ---- 5. NPN switch for a motor or relay --------------------------------------------------------
s = Sheet(500, 340)
s.supply(320, 40, "+12V")
s.wire((320, 40), (320, 60))
s.dot(320, 60)
s.wire((320, 60), (400, 60))
s.two("D", (400, 150), (400, 60), "D1", "1N4007")
s.two("M", (320, 60), (320, 150), "M1", "12 V motor", "left")
s.wire((320, 150), (400, 150))
s.dot(320, 150)
q = s.npn(312, 200, "Q1", "BD139 / TIP120")
s.wire((320, 150), q["C"])
s.flag(80, 200, "GPIO", "end")
s.wire((80, 200), (120, 200))
s.two("R", (120, 200), (220, 200), "R1", "330 Ω")
s.wire((220, 200), q["B"])
s.dot(250, 200)
s.two("R", (250, 200), (250, 290), "R2", "10 kΩ", "left")
s.ground(250, 290)
s.wire(q["E"], (320, 290))
s.ground(320, 290)
example("npn_motor", "Транзисторный ключ NPN для мотора или реле", "NPN switch for a motor or relay", "switching",
        "Микроконтроллер управляет нагрузкой 12 В через NPN-транзистор. Ток базы с запасом для насыщения: Ib ≈ (3,3 − 0,7)/330 ≈ 8 мА; "
        "R2 держит транзистор закрытым, пока вывод МК не настроен. Диод D1 обязателен: при выключении индуктивность мотора или катушки реле "
        "даёт выброс напряжения, который пробьёт транзистор. Для токов больше 0,5 А удобнее MOSFET (пример «MOSFET-ключ»).",
        "An MCU switches a 12 V load through an NPN transistor. Base current with margin for saturation: Ib ≈ (3.3 − 0.7)/330 ≈ 8 mA; "
        "R2 keeps the transistor off while the MCU pin is not configured. D1 is a must: when switched off, the motor or relay coil inductance "
        "makes a voltage spike that would break the transistor. Above 0.5 A a MOSFET is easier (see the MOSFET switch example).",
        s, [part("J1", "J2", "GPIO"), part("R1", "R", "330"), part("R2", "R", "10k"), part("Q1", "NPN", "BD139"),
            part("M1", "M", "Motor_12V"), part("D1", "D", "1N4007")],
        [("GPIO", ["J1.1", "R1.1"]), ("Net-(Q1-B)", ["R1.2", "R2.1", "Q1.1"]), ("+12V", ["M1.1", "D1.1"]),
         ("Net-(Q1-C)", ["M1.2", "D1.2", "Q1.2"]), ("GND", ["Q1.3", "R2.2", "J1.2"])],
        """* NPN low-side switch, the motor as R+L
V1 vcc 0 DC 12
VG gpio 0 PULSE(0 3.3 1m 1u 1u 5m 10m)
R1 gpio b 330
R2 b 0 10k
Q1 c b 0 QNPN
RM vcc m 24
LM m c 10m
D1 c vcc D1N4007
.model QNPN NPN(BF=100 IS=1e-14 VAF=100)
.model D1N4007 D(IS=7e-9 N=1.8 RS=0.04 BV=1000)
.tran 10u 20m
.end""", ["Ib = (Uупр − 0,7) / Rб", "Ib ≥ 3…10·Iк / β"], ["транзистор", "npn", "ключ", "мотор", "реле", "motor", "relay", "switch"])

# ---- 6. N-MOSFET low-side switch ---------------------------------------------------------------
s = Sheet(500, 340)
s.supply(320, 40, "+12V")
s.wire((320, 40), (320, 60))
s.two("R", (320, 60), (320, 150), "LOAD", "LED strip 2 A", "left")
s.dot(320, 150)
s.wire((320, 150), (320, 160))
q = s.nmos(312, 190, "Q1", "IRLZ44N / AO3400")
s.wire((320, 160), q["D"])
s.flag(70, 200, "PWM", "end")
s.wire((70, 200), (110, 200))
s.two("R", (110, 200), (200, 200), "R1", "100 Ω")
s.wire((200, 200), (282, 200))
s.dot(230, 200)
s.two("R", (230, 200), (230, 290), "R2", "100 kΩ", "left")
s.ground(230, 290)
s.wire(q["S"], (320, 290))
s.ground(320, 290)
example("mosfet_lowside", "MOSFET-ключ на стороне земли", "N-MOSFET low-side switch", "switching",
        "Логический (logic-level) N-MOSFET коммутирует нагрузку между ней и землёй. Нужен транзистор, у которого Rds(on) указано при Ugs = 2,5–4,5 В "
        "(IRLZ44N, AO3400), иначе от 3,3 В он откроется не полностью и перегреется. R1 = 100 Ом ограничивает ток заряда затвора и звон, "
        "R2 = 100 кОм держит ключ закрытым при сбросе МК. Потери P = I²·Rds(on): 2 А и 25 мОм → 0,1 Вт. Для мотора добавьте диод параллельно нагрузке.",
        "A logic-level N-MOSFET switches the load between it and ground. Pick one whose Rds(on) is specified at Ugs = 2.5–4.5 V "
        "(IRLZ44N, AO3400), otherwise 3.3 V turns it on only partly and it overheats. R1 = 100 Ω limits the gate charging current and ringing, "
        "R2 = 100 kΩ keeps it off while the MCU resets. Loss P = I²·Rds(on): 2 A and 25 mΩ → 0.1 W. For a motor add a diode across the load.",
        s, [part("J1", "J2", "PWM"), part("R1", "R", "100"), part("R2", "R", "100k"), part("Q1", "NMOS", "IRLZ44N"),
            part("J2", "J2", "LED_strip", FOOTPRINTS["J2"])],
        [("PWM", ["J1.1", "R1.1"]), ("Net-(Q1-G)", ["R1.2", "R2.1", "Q1.1"]), ("+12V", ["J2.1"]),
         ("Net-(Q1-D)", ["J2.2", "Q1.2"]), ("GND", ["Q1.3", "R2.2", "J1.2"])],
        """* Logic-level MOSFET switching a 6 ohm load
V1 vcc 0 DC 12
VG pwm 0 PULSE(0 3.3 0 100n 100n 50u 100u)
R1 pwm g 100
R2 g 0 100k
RL vcc d 6
M1 d g 0 0 NLOGIC
.model NLOGIC NMOS(VTO=1.5 KP=20 LAMBDA=0.01)
.tran 100n 300u
.end""", ["P = I²·Rds(on)", "t ≈ Qg / Ig"], ["mosfet", "мосфет", "ключ", "шим", "pwm", "switch"])

# ---- 7. Non-inverting op-amp amplifier ---------------------------------------------------------
s = Sheet(520, 320)
o = s.opamp(260, 140, "U1", "LM358 / MCP6002", plus_top=True)
s.flag(100, 124, "IN", "end")
s.wire((100, 124), o["+"])
s.wire(o["out"], (420, 140))
s.flag(420, 140, "OUT = 11·IN")
s.dot(380, 140)
s.wire((380, 140), (380, 230))
s.two("R", (380, 230), (240, 230), "R2", "10 kΩ")
s.wire((240, 230), (180, 230), (180, 156), o["-"])
s.dot(180, 230)
s.two("R", (180, 230), (180, 300), "R1", "1 kΩ", "left")
s.ground(180, 300)
s.supply(260, 80, "+5V")
s.wire((260, 80), (260, 108))
example("opamp_noninverting", "Неинвертирующий усилитель на ОУ", "Non-inverting op-amp amplifier", "analog",
        "Коэффициент усиления K = 1 + R2/R1 = 11, высокое входное сопротивление, фаза не меняется. Отрицательная обратная связь держит "
        "напряжения на входах равными. Однополярное питание 5 В: LM358 не доходит до +5 В на выходе (≈ 3,5 В), rail-to-rail MCP6002 — почти до шин. "
        "Не забудьте 100 нФ у вывода питания ОУ.",
        "Gain K = 1 + R2/R1 = 11, a high input impedance, no phase inversion. Negative feedback keeps the two inputs at equal voltages. "
        "On a single 5 V supply the LM358 output tops out near 3.5 V, a rail-to-rail MCP6002 gets close to the rails. "
        "Do not forget 100 nF at the op-amp supply pin.",
        s, [part("U1", "LM358", "LM358"), part("R1", "R", "1k"), part("R2", "R", "10k")],
        [("IN", ["U1.3"]), ("Net-(U1--)", ["U1.2", "R1.1", "R2.2"]), ("OUT", ["U1.1", "R2.1"]), ("+5V", ["U1.8"]), ("GND", ["U1.4", "R1.2"])],
        """* Non-inverting amplifier, gain 11 (ideal op-amp)
V1 in 0 SIN(0.2 0.1 1k)
E1 out 0 in inm 100k
R1 inm 0 1k
R2 out inm 10k
.tran 10u 3m
.end""", ["K = 1 + R2/R1"], ["оу", "операционный", "усилитель", "opamp", "op-amp", "amplifier"])

# ---- 8. 3.3 V linear regulator -----------------------------------------------------------------
s = Sheet(520, 280)
u = s.box(220, 90, 90, 60, "U1", "AMS1117-3.3", [("left", 30, 3, "VI"), ("right", 30, 2, "VO"), ("bottom", 45, 1, "GND")])
s.supply(90, 70, "+5V")
s.wire((90, 70), (90, 120), u[3])
s.dot(150, 120)
s.two("C", (150, 120), (150, 210), "C1", "10 µF")
s.ground(150, 210)
s.wire(u[1], (265, 210))
s.ground(265, 210)
s.wire(u[2], (440, 120))
s.dot(380, 120)
s.two("C", (380, 120), (380, 210), "C2", "22 µF")
s.ground(380, 210)
s.supply(440, 100, "+3V3")
s.wire((440, 100), (440, 120))
example("ldo_regulator", "Стабилизатор 3,3 В (LDO)", "3.3 V linear regulator (LDO)", "power",
        "AMS1117-3.3 делает 3,3 В из 5 В (USB). Падение на нём ~1,1 В, поэтому вход ≥ 4,5 В. Мощность нагрева P = (Uвх − Uвых)·I: "
        "при 500 мА — 0,85 Вт, корпус SOT-223 без большой площадки меди нагреется сильно. Конденсаторы по даташиту: 10 мкФ на входе и 22 мкФ "
        "на выходе (тантал или керамика с последовательным сопротивлением). При перепаде больше 2–3 В выгоднее импульсный понижающий преобразователь.",
        "The AMS1117-3.3 makes 3.3 V from 5 V (USB). It drops ~1.1 V, so the input must be ≥ 4.5 V. Heat P = (Vin − Vout)·I: "
        "0.85 W at 500 mA — a SOT-223 without a large copper pad gets hot. Capacitors per the datasheet: 10 µF in, 22 µF out "
        "(tantalum, or ceramic with some ESR). For a drop over 2–3 V a buck converter wastes less.",
        s, [part("U1", "AMS1117", "AMS1117-3.3"), part("C1", "C", "10u"), part("C2", "C", "22u")],
        [("+5V", ["U1.3", "C1.1"]), ("+3V3", ["U1.2", "C2.1"]), ("GND", ["U1.1", "C1.2", "C2.2"])],
        "", ["P = (Uвх − Uвых)·I", "Uвх ≥ Uвых + Udrop"], ["стабилизатор", "ldo", "ams1117", "питание", "regulator", "3.3"])

# ---- 9. Button with a pull-up and RC debounce --------------------------------------------------
s = Sheet(500, 300)
s.supply(200, 40, "+3V3")
s.wire((200, 40), (200, 60))
s.two("R", (200, 60), (200, 130), "R1", "10 kΩ", "left")
s.dot(200, 130)
s.wire((200, 130), (260, 130))
s.two("R", (260, 130), (340, 130), "R2", "1 kΩ")
s.wire((340, 130), (430, 130))
s.flag(430, 130, "GPIO")
s.dot(380, 130)
s.two("C", (380, 130), (380, 230), "C1", "100 nF")
s.ground(380, 230)
s.two("SW", (200, 130), (200, 230), "SW1", "button", "left")
s.ground(200, 230)
example("button_debounce", "Кнопка с подтяжкой и подавлением дребезга", "Button with pull-up and debounce", "mcu",
        "R1 подтягивает вход к 3,3 В — без нажатия на входе 1, нажатие замыкает на землю (0). Контакты кнопки «дребезжат» 1–10 мс; "
        "RC-цепь R2·C1 = 0,1 мс… с R1: заряд через R1 + R2 = 11 кОм → τ ≈ 1,1 мс, разряд через R2 → 0,1 мс. Для надёжности вход с триггером Шмитта "
        "или программная проверка: состояние держится 20 мс. Вместо R1 можно включить встроенную подтяжку МК (20–50 кОм).",
        "R1 pulls the input up to 3.3 V — released reads 1, pressed shorts it to ground (0). Button contacts bounce for 1–10 ms; "
        "the RC network smooths it: charging through R1 + R2 = 11 kΩ gives τ ≈ 1.1 ms, discharging through R2 0.1 ms. Use a Schmitt-trigger input "
        "or check in software that the state holds for 20 ms. The MCU's internal pull-up (20–50 kΩ) can replace R1.",
        s, [part("R1", "R", "10k"), part("R2", "R", "1k"), part("C1", "C", "100n"), part("SW1", "SW", "SW_Push"), part("J1", "J2", "GPIO")],
        [("+3V3", ["R1.1"]), ("Net-(SW1-1)", ["R1.2", "SW1.1", "R2.1"]), ("GPIO", ["R2.2", "C1.1", "J1.1"]), ("GND", ["SW1.2", "C1.2", "J1.2"])],
        """* Pull-up, debounce: the button closes at 1 ms with 3 bounces
V1 vcc 0 DC 3.3
R1 vcc n 10k
S1 n 0 ctl 0 SBTN
VC ctl 0 PWL(0 0 1m 0 1.0001m 1 1.2m 1 1.2001m 0 1.5m 0 1.5001m 1 1.8m 1 1.8001m 0 2m 0 2.0001m 1)
R2 n gpio 1k
C1 gpio 0 100n
.model SBTN SW(VT=0.5 RON=0.1 ROFF=10Meg)
.tran 5u 5m
.end""", ["τ = R·C"], ["кнопка", "дребезг", "подтяжка", "button", "debounce", "pull-up"])

# ---- 10. I2C bus with pull-ups -----------------------------------------------------------------
s = Sheet(560, 320)
m = s.box(60, 120, 100, 100, "U1", "MCU (STM32, ESP32…)", [("right", 30, 1, "SDA"), ("right", 60, 2, "SCL")])
d = s.box(400, 120, 100, 100, "U2", "Sensor (BME280…)", [("left", 30, 1, "SDA"), ("left", 60, 2, "SCL")])
s.wire(m[1], d[1])
s.wire(m[2], d[2])
s.supply(270, 40, "+3V3")
s.wire((270, 40), (270, 50), (240, 50), (240, 60))
s.wire((270, 50), (300, 50), (300, 60))
s.dot(270, 50)
s.two("R", (240, 60), (240, 150), "R1", "4.7 kΩ", "left")
s.two("R", (300, 60), (300, 180), "R2", "4.7 kΩ")
s.dot(240, 150)
s.dot(300, 180)
s.text(270, 290, "SDA, SCL: open drain — pull-ups are required", INK, 12)
example("i2c_pullups", "Шина I2C с подтягивающими резисторами", "I2C bus with pull-up resistors", "mcu",
        "Выходы I2C — открытый сток: устройства только прижимают линию к земле, а единицу создают подтягивающие резисторы. "
        "4,7 кОм — для 100 кГц и коротких проводов; для 400 кГц или ёмкости шины ~200 пФ — 2,2 кОм (калькулятор «Подтяжка шины I2C»). "
        "Подтяжка — одна на всю шину, а не на каждое устройство. У модулей с датчиками свои резисторы часто уже стоят.",
        "I2C outputs are open drain: devices only pull the line low and the pull-up resistors make the high level. "
        "4.7 kΩ suits 100 kHz and short wires; for 400 kHz or ~200 pF of bus capacitance use 2.2 kΩ (the I2C pull-up calculator). "
        "One set of pull-ups per bus, not per device. Sensor modules often have their own already.",
        s, [part("U1", "J4", "MCU", FOOTPRINTS["J4"]), part("U2", "J4", "Sensor", FOOTPRINTS["J4"]), part("R1", "R", "4.7k"), part("R2", "R", "4.7k")],
        [("SDA", ["U1.1", "U2.1", "R1.2"]), ("SCL", ["U1.2", "U2.2", "R2.2"]), ("+3V3", ["R1.1", "R2.1", "U1.3", "U2.3"]), ("GND", ["U1.4", "U2.4"])],
        "", ["Rmax = tr / (0,8473·Cb)", "Rmin = (Vdd − 0,4) / 3 мА"], ["i2c", "подтяжка", "pull-up", "шина", "bus"])

# ---- 11. Bridge rectifier with a 7805 ----------------------------------------------------------
s = Sheet(600, 320)
br = s.box(140, 100, 80, 80, "D1", "DB107", [("left", 20, 2, "~"), ("left", 60, 3, "~"), ("right", 20, 1, "+"), ("right", 60, 4, "−")])
tr = s.box(30, 100, 50, 80, "J1", "~12 V", [("right", 20, 1, "1"), ("right", 60, 2, "2")])
s.wire(tr[1], br[2])
s.wire(tr[2], br[3])
s.wire(br[1], (330, 120))
s.dot(270, 120)
s.two("CP", (270, 120), (270, 230), "C1", "2200 µF 25 V")
s.wire(br[4], (250, 160), (250, 250), (270, 250), (270, 230))
s.dot(270, 250)
u = s.box(330, 100, 80, 50, "U1", "L7805", [("left", 20, 1, "IN"), ("right", 20, 3, "OUT"), ("bottom", 40, 2, "GND")])
s.wire(u[2], (370, 250))
s.wire((270, 250), (500, 250))
s.ground(370, 250)
s.dot(370, 250)
s.wire(u[3], (540, 120))
s.dot(460, 120)
s.two("C", (460, 120), (460, 250), "C2", "100 nF")
s.dot(460, 250)
s.supply(540, 100, "+5V")
s.wire((540, 100), (540, 120))
example("bridge_rectifier", "Выпрямитель и стабилизатор 5 В", "Bridge rectifier with a 5 V regulator", "power",
        "Переменное 12 В от трансформатора выпрямляет диодный мост, C1 сглаживает пульсации, 7805 стабилизирует 5 В. "
        "Амплитуда 12·√2 − 2·0,9 ≈ 15 В. Пульсации ΔU ≈ I/(2·f·C) = 0,5/(100·2200 мкФ) ≈ 2,3 В — минимум остаётся выше 7 В, нужных 7805. "
        "Мощность на 7805: (15 − 5)·0,5 = 5 Вт — нужен радиатор. Работа с сетью 230 В опасна: трансформатор только готовый, первичная цепь с предохранителем.",
        "A diode bridge rectifies 12 V AC from a transformer, C1 smooths the ripple, the 7805 regulates 5 V. "
        "The peak is 12·√2 − 2·0.9 ≈ 15 V. Ripple ΔU ≈ I/(2·f·C) = 0.5/(100·2200 µF) ≈ 2.3 V — the minimum stays above the 7 V the 7805 needs. "
        "The 7805 dissipates (15 − 5)·0.5 = 5 W — it needs a heatsink. Mains (230 V) is dangerous: use a ready transformer with a fused primary.",
        s, [part("J1", "J2", "AC_12V"), part("D1", "BRIDGE", "DB107"), part("C1", "CP", "2200u"), part("U1", "7805", "L7805"), part("C2", "C", "100n")],
        [("AC1", ["J1.1", "D1.2"]), ("AC2", ["J1.2", "D1.3"]), ("VRAW", ["D1.1", "C1.1", "U1.1"]), ("GND", ["D1.4", "C1.2", "U1.2", "C2.2"]),
         ("+5V", ["U1.3", "C2.1"])],
        """* Bridge rectifier, 2200 uF, 10 ohm load (no regulator)
V1 a b SIN(0 16.97 50)
D1 a p DBR
D2 b p DBR
D3 n a DBR
D4 n b DBR
C1 p n 2200u
RL p n 30
.model DBR D(IS=1e-9 N=1.9 RS=0.05 BV=1000)
.tran 100u 100m
.end""", ["Uпик = U~·√2 − 2·Uд", "ΔU ≈ I / (2·f·C)"], ["выпрямитель", "мост", "7805", "rectifier", "bridge", "блок питания", "power supply"])


def main():
    out = []
    for e in EXAMPLES:
        (HERE / f"{e['id']}.svg").write_text(e.pop("svg"), encoding="utf-8")
        # Every node must name a pin its symbol has; every net needs two nodes (or a power flag).
        refs = {p["ref"]: p for p in e["parts"]}
        for net in e["nets"]:
            for node in net["nodes"]:
                ref, pin = node.split(".")
                if pin not in refs[ref]["pins"]:
                    raise SystemExit(f"{e['id']}: {node} is not a pin of {refs[ref]['symbol']}")
        out.append(e)
    (HERE / "examples.json").write_text(json.dumps({"schema": 1, "examples": out}, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(len(out), "examples")


if __name__ == "__main__":
    main()
