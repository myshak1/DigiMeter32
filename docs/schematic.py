"""DigiMeter32 by SP2KMO — full schematic diagram (schemdraw)."""
import sys
import schemdraw
import schemdraw.elements as elm

OUT = sys.argv[1] if len(sys.argv) > 1 else "schematic"
FMT = sys.argv[2] if len(sys.argv) > 2 else "svg"
schemdraw.use("svg" if FMT == "svg" else "matplotlib")

FS = 1.7          # skala czcionek (czytelnosc na A3)
BLUE = "#1f4e9c"
from schemdraw.segments import Segment, SegmentCircle, SegmentText


class Buzzer(elm.Element2Term):
    """Buzzer: kolko z oznaczeniem BZ i znakiem +."""
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        r = 0.5
        self.segments.append(Segment([(0, 0), (0, 0)]))
        self.segments.append(SegmentCircle((r, 0), r))
        self.segments.append(SegmentText((r, 0), "BZ", fontsize=FS*10, rotation=0, rotation_mode="default"))
        self.segments.append(SegmentText((2 * r + 0.15, 0.35), "+", fontsize=FS*11))
        self.anchors["center"] = (r, 0)


elm.Buzzer = Buzzer
GREY = "#555555"
RED = "#b3261e"


def tag(d, at, name, direction="right", width=None):
    """Net label (flag) attached at point `at`."""
    w = width or (0.18 * FS * len(name) + 0.45)
    t = elm.Tag(width=w, height=0.625 * FS).at(at).label(name, fontsize=FS*10, color=BLUE)
    if direction == "left":
        t = t.left()
    elif direction == "up":
        t = t.up()
    elif direction == "down":
        t = t.down()
    else:
        t = t.right()
    return d.add(t)


def gnd(d, at):
    return d.add(elm.Ground().at(at))


def vcc(d, at, name="+3V3"):
    return d.add(elm.Vdd().at(at).label(name, fontsize=FS*10, color=BLUE))


def note(d, xy, text, size=10, color=GREY, halign="left"):
    d.add(elm.Label().at(xy).label(text, fontsize=FS*size, color=color, halign=halign, valign="top"))


def channel(d, x0, y0, ch, pins):
    """One buffer channel. ch="1" (FWD) or "2" (REF)."""
    name, wire, gpio, ain, pin_p, pin_n, pin_o, unit = pins
    d.add(elm.Label().at((x0 - 0.2, y0 + 2.6)).label(
        f"{name} channel", fontsize=FS*13, halign="left", color="black"))
    inp = d.add(elm.Dot(open=True).at((x0, y0)).label(
        f"{wire}\n(detector output)", loc="left", fontsize=FS*10, halign="right"))
    d.add(elm.Line().right(1.0))
    p = d.add(elm.Dot())
    d.add(elm.Capacitor().down(2.0).at(p.center).label(f"C{ch}0\n100p", loc="bottom", fontsize=FS*10))
    gnd(d, d.here)
    d.add(elm.Resistor().right(3.0).at(p.center).label(f"R{ch}1  1 MΩ", fontsize=FS*10))
    a = d.add(elm.Dot())
    d.add(elm.Resistor().down(2.0).at(a.center).label(f"R{ch}2\n318 kΩ", loc="bottom", fontsize=FS*10))
    gnd(d, d.here)
    d.add(elm.Line().right(2.4).at(a.center))
    a2 = d.add(elm.Dot())
    d.add(elm.Capacitor().down(2.0).at(a2.center).label(f"C{ch}1\n1n", loc="bottom", fontsize=FS*10))
    gnd(d, d.here)
    d.add(elm.Resistor().right(2.6).at(a2.center).label(f"R{ch}3  10 kΩ", fontsize=FS*10))
    op = d.add(elm.Opamp(leads=True, sign=True).flip().anchor("in2"))
    d.add(elm.Label().at(op.in2).label(pin_p, fontsize=FS*9, color=RED, halign="right", valign="bottom"))
    d.add(elm.Label().at(op.in1).label(pin_n, fontsize=FS*9, color=RED, halign="right", valign="top"))
    d.add(elm.Label().at(op.out).label(pin_o, fontsize=FS*9, color=RED, halign="left", valign="bottom"))
    d.add(elm.Label().at((op.center[0] - 0.4, op.center[1] + 1.55)).label(unit, fontsize=FS*10))
    # sprzezenie zwrotne: wtornik
    o = d.add(elm.Dot().at(op.out))
    d.add(elm.Line().down(1.6).at(o.center))
    d.add(elm.Line().tox(op.in1[0] - 0.35))
    d.add(elm.Line().toy(op.in1[1]))
    d.add(elm.Line().tox(op.in1[0]))
    # wyjscie -> filtr RC -> ADS1115
    d.add(elm.Line().right(0.9).at(o.center))
    d.add(elm.Resistor().right(2.6).label(f"R{ch}4  470 Ω", fontsize=FS*10))
    e2 = d.add(elm.Dot())
    d.add(elm.Capacitor().down(1.6).at(e2.center).label(f"C{ch}2\n10n", loc="bottom", fontsize=FS*10))
    gnd(d, d.here)
    d.add(elm.Line().right(1.0).at(e2.center))
    tag(d, d.here, f"{name}_ADS → {ain}")
    return inp



def frame(d, x0, y0, x1, y1, title, color="#2e6b3a"):
    """Ramka strefy montazu (linia przerywana) z tytulem."""
    for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
        d.add(elm.Line().at(a).to(b).color(color).linestyle("--").linewidth(1.6))
    d.add(elm.Label().at((x0 + 0.4, y0 - 0.3)).label(title, fontsize=FS*15, color=color, halign="left", valign="top"))


def pins(side, names, n):
    """names od gory; slot n = najwyzej"""
    out = []
    for i, nm in enumerate(names):
        if nm is None:
            continue
        name, pin, anc = (nm + (None, None))[:3] if isinstance(nm, tuple) else (nm, None, None)
        out.append(elm.IcPin(name=name, pin=pin or "", side=side, slot=f"{n - i}/{n}", anchorname=anc,
                              lblsize=FS * 10, pinlblsize=FS * 7))
    return out


CABLE = ["+13.8V", "+3V3", "SDA", "SCL", "GND"]


def connector(d, x, y_top, name, numbers_left):
    """Zlacze 5-stykowe: kolka z numerami, ramka. Zwraca liste punktow styku."""
    pts = []
    for i in range(len(CABLE)):
        y = y_top - 1.5 * i
        d.add(elm.Dot(open=True).at((x, y)))
        d.add(elm.Label().at((x + (-0.25 if numbers_left else 0.25), y + 0.05)).label(
            str(i + 1), fontsize=FS*9, color=RED, halign="right" if numbers_left else "left", valign="bottom"))
        pts.append((x, y))
    yb = y_top - 1.5 * (len(CABLE) - 1)
    bx0, bx1 = x - 0.7, x + 0.7
    for a, b in (((bx0, y_top + 0.8), (bx1, y_top + 0.8)), ((bx1, y_top + 0.8), (bx1, yb - 0.8)),
                 ((bx1, yb - 0.8), (bx0, yb - 0.8)), ((bx0, yb - 0.8), (bx0, y_top + 0.8))):
        d.add(elm.Line().at(a).to(b).color(GREY).linestyle(":"))
    d.add(elm.Label().at((x, y_top + 1.0)).label(name, fontsize=FS*11, halign="center", valign="bottom"))
    return pts


with schemdraw.Drawing(show=False) as d:
    d.config(unit=2.5, fontsize=FS*11, lw=1.3)
    # margines z lewej (etykiety wyrownane do prawej wychodza poza obrys)
    d.add(elm.Line().at((-6.0, 0)).to((-5.95, 0)).color("white"))

    # ------------------------------------------------------------ tytul
    d.add(elm.Label().at((-4.2, 7.0)).label(
        "DigiMeter32 by SP2KMO — schematic diagram", fontsize=FS*20, halign="left"))
    d.add(elm.Label().at((-4.2, 6.0)).label(
        "ESP32-WROOM-32D (DevKit) · LCD 4\" SPI (ILI9488/ILI9486/ST7796S) · MCP6002 · ADS1115 · "
        "all parts through-hole (THT), ready-made modules", fontsize=FS*11, halign="left", color=GREY))

    YT, YB = 4.2, -45.0
    XB1 = 97.0
    frame(d, -4.2, YT, 40.0, YB, "A. INSIDE THE METER CASE — next to the coupler")
    frame(d, 43.0, YT, XB1, YB, "B. AT THE DISPLAY — on the back of the LCD panel", color="#1f4e9c")

    # ============================================================ STREFA A
    # ------------------------------------------------------------ BUFOR
    d.add(elm.Label().at((0, 1.2)).label(
        "1. Input buffer (U1 = MCP6002, DIP-8)", fontsize=FS*14, halign="left"))
    channel(d, 4.5, -4.0, "1",
            ("FWD", "red wire", None, "A0", "3", "2", "1", "U1A"))
    channel(d, 4.5, -15.5, "2",
            ("REF", "white wire", None, "A1", "5", "6", "7", "U1B"))

    ux, uy = 30.5, -3.0
    d.add(elm.Label().at((ux - 0.5, uy + 2.2)).label("U1 supply", fontsize=FS*12, halign="left"))
    vcc(d, (ux, uy + 1.0))
    d.add(elm.Line().down(1.0).at((ux, uy + 1.0)))
    d.add(elm.Dot())
    d.add(elm.Line().right(1.8))
    d.add(elm.Label().at(d.here).label(" pin 8 (VDD)", fontsize=FS*10, halign="left", color=RED))
    d.add(elm.Capacitor().down(2.0).at((ux, uy)).label("C5  100n\n(right at U1)", loc="bottom", fontsize=FS*10))
    d.add(elm.Dot())
    d.add(elm.Line().right(1.8))
    d.add(elm.Label().at(d.here).label(" pin 4 (VSS)", fontsize=FS*10, halign="left", color=RED))
    gnd(d, (ux, uy - 2.0))

    note(d, (28.6, -8.5),
         "Divider R1/R2 = 1 MΩ / 318 kΩ (÷4.14);\n"
         "R2 = 300 kΩ + 18 kΩ in series, R12 = R22.\n"
         "Chosen from measurement (IC-7000, 40 m):\n"
         "   100 W → detector approx. 8.9 V\n"
         "            → approx. 2.15 V at ADS1115\n"
         "   full range approx. 13 V (approx. 200 W)\n\n"
         "Use the meter with FUNCTION = SWR —\n"
         "the needle then does not load the detector\n"
         "and the reading ignores RANGE/POWER/CAL.\n\n"
         "Solder C10/C20 right at the coupler.\n\n"
         "The detector must give a POSITIVE voltage\n"
         "to ground — check with a multimeter.\n\n"
         "Inputs protected by R1 (1 MΩ) and the\n"
         "diodes inside MCP6002 — extra Schottky\n"
         "diodes intentionally omitted.", size=10)

    # ------------------------------------------------------------ ADS1115
    AX = 6.5
    d.add(elm.Label().at((0, -22.0)).label("2. ADS1115 (module)", fontsize=FS*14, halign="left"))
    ads = d.add(elm.Ic(
        size=(4.0, 7.5),
        pins=pins("left", ["A0", "A1", "A2", "A3"], 6) + pins("right", ["VDD", "GND", "SCL", "SDA", "ADDR", "ALRT"], 6),
        leadlen=0.9).at((AX, -24.5)).anchor("A0").label("U3  ADS1115", loc="bottom", fontsize=FS*11))
    tag(d, ads.A0, "FWD_ADS", "left")
    tag(d, ads.A1, "REF_ADS", "left")
    d.add(elm.Line().left(0.8).at(ads.A2))
    d.add(elm.Dot())
    p2 = d.here
    d.add(elm.Line().left(0.8).at(ads.A3))
    d.add(elm.Dot())
    d.add(elm.Line().up().toy(p2[1]))
    d.add(elm.Line().down(0.6).at((p2[0], ads.A3[1])))
    gnd(d, d.here)
    d.add(elm.Label().at((p2[0] - 0.3, ads.A3[1] - 0.2)).label("unused", fontsize=FS*9, color=GREY, halign="right", valign="top"))
    tag(d, ads.VDD, "+3V3")
    d.add(elm.Line().right(0.6).at(ads.GND))
    gnd(d, d.here)
    tag(d, ads.SCL, "SCL")
    tag(d, ads.SDA, "SDA")
    d.add(elm.Line().right(0.6).at(ads.ADDR))
    gnd(d, d.here)
    d.add(elm.Label().at((ads.ADDR[0] + 1.0, ads.ADDR[1] + 0.1)).label("address 0x48", fontsize=FS*9, color=GREY, halign="left", valign="bottom"))
    d.add(elm.Label().at((ads.ALRT[0] + 0.15, ads.ALRT[1])).label("not connected", fontsize=FS*9, color=GREY, halign="left"))
    note(d, (18.0, -23.8),
         "The module has 10 kΩ pull-ups\n"
         "on SDA/SCL — do not add more.\n"
         "Supply 3.3 V (not 5 V!) — then\n"
         "inputs A0–A3 match the MCP6002\n"
         "output (both on the same +3V3 rail,\n"
         "brought by wire from the ESP).", size=10)

    # ------------------------------------------------------------ ZLACZE X1
    X1x, X2x, XY = 37.6, 45.4, -35.0
    x1 = connector(d, X1x, XY, "X1", numbers_left=False)
    x2 = connector(d, X2x, XY, "X2", numbers_left=True)
    for (a, b) in zip(x1, x2):
        d.add(elm.Line().at((a[0] + 0.15, a[1])).to((b[0] - 0.15, b[1])))
    # strona miernika
    d.add(elm.Line().at(x1[0]).left(5.0))
    d.add(elm.Dot(open=True).label("+13.8 V from the meter\n(after the POWER switch)", loc="left", fontsize=FS*10, halign="right"))
    for i in (1, 2, 3):
        d.add(elm.Line().at(x1[i]).left(0.6))
        tag(d, d.here, CABLE[i], "left")
    d.add(elm.Line().at(x1[4]).left(1.4))
    g1 = d.here
    gnd(d, g1)
    d.add(elm.Label().at((g1[0] - 0.4, g1[1] - 0.1)).label(
        "meter ground\n(black wire, coupler housing)", fontsize=FS*10, halign="right", valign="top"))
    note(d, (13.0, -39.0),
         "Cable X1–X2: 5 wires (twisted or shielded),\n"
         "up to approx. 50 cm. Shield grounded at the\n"
         "meter side only. Analog FWD/REF signals\n"
         "never leave the meter case.", size=10)

    # ============================================================ STREFA B
    # ------------------------------------------------------------ ZASILANIE
    OX = 46.0
    d.add(elm.Label().at((OX, 1.2)).label("3. Power supply", fontsize=FS*14, halign="left"))
    d.add(elm.Line().at((OX + 3.6, -1.6)).right(0.01))
    tag(d, (OX + 3.6, -1.6), "+13.8V", "left")
    d.add(elm.Diode().at((OX + 3.6, -1.6)).right(2.5).label("D1  1N5819", fontsize=FS*10))
    d.add(elm.Line().right(0.8))
    reg = d.add(elm.VoltageRegulator().right().anchor("in").label("U2\nR-78E5.0-0.5", fontsize=FS*10, loc="top"))
    d.add(elm.Label().at(reg["in"]).label("1", fontsize=FS*9, color=RED, halign="right", valign="bottom"))
    d.add(elm.Label().at(reg.out).label("3", fontsize=FS*9, color=RED, halign="left", valign="bottom"))
    d.add(elm.Line().down(0.6).at(reg.gnd))
    d.add(elm.Label().at(reg.gnd).label(" 2", fontsize=FS*9, color=RED, halign="left", valign="top"))
    gnd(d, d.here)
    d.add(elm.Line().right(0.8).at(reg.out))
    d.add(elm.Diode().right(2.5).label("D2  1N5819", fontsize=FS*10))
    tag(d, d.here, "+5V → VIN")
    note(d, (OX + 21.0, 1.0),
         "• U2 — ready-made converter (1 = +Vin, 2 = GND, 3 = +Vout,\n"
         "   left to right facing the marking); no capacitors needed.\n"
         "• D1 — reverse polarity protection.\n"
         "• D2 — USB 5 V does not feed back into the converter;\n"
         "   when working over USB, best set POWER = OFF.\n"
         "• +3V3 for the LCD and (via X2) for MCP6002 and ADS1115\n"
         "   — from the 3V3 pin of the ESP32 board.\n"
         "• Part B ground meets the meter only through connector\n"
         "   pin 5 — do not tie it to the case separately.", size=10)

    # zlacze X2 - strona wyswietlacza
    for i in (0, 1, 2, 3):
        d.add(elm.Line().at(x2[i]).right(0.6))
        tag(d, d.here, CABLE[i], "right")
    d.add(elm.Line().at(x2[4]).right(1.0))
    gnd(d, d.here)

    # ------------------------------------------------------------ ESP32
    AX2 = 56.5
    d.add(elm.Label().at((AX2 - 4.0, -7.6)).label("4. ESP32-WROOM-32D (DevKit board)", fontsize=FS*14, halign="left"))
    esp = d.add(elm.Ic(
        size=(6.0, 13.0),
        pins=pins("left", [("3V3", "3V3", "V33"), ("5V", "VIN", "V5"), ("GND", "GND", "GNDL"),
                           None, None, ("GPIO21", "D21", "PSDA"),
                           ("GPIO22", "D22", "PSCL"), ("GPIO26", "D26", "PALM"), ("GPIO27", "D27", "PBTN")], 9)
        + pins("right", [("GPIO18", "D18", "SCK"), ("GPIO23", "D23", "MOSI"), ("GPIO5", "D5", "CS"),
                         ("GPIO33", "D33", "DC"), ("GPIO4", "D4", "RST"), ("GPIO25", "D25", "LR"),
                         ("GPIO32", "D32", "LG"), ("GPIO13", "D13", "LB"), ("GND", "GND", "GNDR")], 9),
        leadlen=1.4).at((AX2, -9.5)).anchor("V33").label("U4", loc="bottom", fontsize=FS*11))
    tag(d, esp.V33, "+3V3", "left")
    tag(d, esp.V5, "+5V", "left")
    d.add(elm.Line().left(0.6).at(esp.GNDL))
    gnd(d, d.here)
    tag(d, esp.PSDA, "SDA", "left")
    tag(d, esp.PSCL, "SCL", "left")
    tag(d, esp.PALM, "ALARM", "left")
    tag(d, esp.PBTN, "BUTTON", "left")
    d.add(elm.Line().right(0.6).at(esp.GNDR))
    gnd(d, d.here)
    tag(d, esp.LR, "RGB_R", "right")
    tag(d, esp.LG, "RGB_G", "right")
    tag(d, esp.LB, "RGB_B", "right")

    # ------------------------------------------------------------ LCD
    LX = esp.SCK[0] + 6.0
    d.add(elm.Label().at((LX - 0.5, -7.6)).label("5. LCD 4\" 480×320 SPI", fontsize=FS*14, halign="left"))
    lcd = d.add(elm.Ic(
        size=(6.0, 13.0),
        pins=pins("left", [("SCK", None, "L_SCK"), ("SDI (MOSI)", None, "L_MOSI"), ("CS", None, "L_CS"),
                           ("DC / RS", None, "L_DC"), ("RESET", None, "L_RST"), ("LED", None, "L_LED"),
                           ("VCC", None, "L_VCC"), ("GND", None, "L_GND")], 9)
        + pins("right", [("SDO (MISO)", None, "L_MISO"), None, ("T_CLK", None, "T1"), ("T_CS", None, "T2"),
                         ("T_DIN", None, "T3"), ("T_DO", None, "T4"), ("T_IRQ", None, "T5")], 9),
        leadlen=0.8).at((LX, esp.SCK[1])).anchor("L_SCK").label("DS1", loc="bottom", fontsize=FS*11))
    for a, b in (("SCK", "L_SCK"), ("MOSI", "L_MOSI"), ("CS", "L_CS"), ("DC", "L_DC"), ("RST", "L_RST")):
        d.add(elm.Line().at(esp[a]).to(lcd[b]))
    tag(d, lcd.L_LED, "+3V3", "left")
    tag(d, lcd.L_VCC, "+3V3", "left")
    d.add(elm.Line().left(0.6).at(lcd.L_GND))
    gnd(d, d.here)
    d.add(elm.Label().at((lcd.L_MISO[0] + 0.15, lcd.L_MISO[1])).label("n.c.", fontsize=FS*9, color=GREY, halign="left"))
    d.add(elm.Label().at((lcd.T1[0] + 0.15, lcd.T1[1] + 0.3)).label(
        "touch\nn.c.", fontsize=FS*9, color=GREY, halign="left", valign="bottom"))
    note(d, (LX - 1.0, -25.2),
         "Pin order differs between modules —\n"
         "wire by the NAMES printed on the board.\n"
         "VCC and LED at 3.3 V work with any module.\n"
         "LED tied high = backlight always on.", size=10)

    # ------------------------------------------------------------ ALARM
    ZX, ZY = 55.5, -41.5
    d.add(elm.Label().at((50.5, -30.6)).label("6. SWR alarm (optional)", fontsize=FS*14, halign="left"))
    q2 = d.add(elm.BjtNpn(circle=True).right().at((ZX, ZY)).label("Q2\nBC337", loc="right", fontsize=FS*10))
    k = q2.collector[0]
    d.add(elm.Line().at(q2.collector).toy(ZY + 1.6))
    kk = d.add(elm.Dot())
    top_y = ZY + 6.0
    d.add(elm.Line().at(kk.center).tox(k - 3.0))
    d.add(elm.LED().up(2.2).reverse().label("LED1", loc="bottom", fontsize=FS*10))
    d.add(elm.Resistor().up().toy(top_y).label("R43\n330 Ω", loc="bottom", fontsize=FS*10))
    d.add(elm.Line().tox(k + 4.6))
    d.add(elm.Buzzer().at(kk.center).up().toy(top_y).label("BZ1\n5 V active\nbuzzer", loc="bottom", fontsize=FS*10))
    d.add(elm.Dot().at((k, top_y)))
    vcc(d, (k, top_y), "+5V")
    d.add(elm.Line().at(kk.center).tox(k + 4.6))
    d.add(elm.Diode().up().toy(top_y).label("D3\n1N4148", loc="bottom", fontsize=FS*10))
    d.add(elm.Line().at(q2.emitter).down(0.5))
    gnd(d, d.here)
    d.add(elm.Line().at(q2.base).left(0.4))
    g = d.add(elm.Dot())
    d.add(elm.Resistor().left(3.2).at(g.center).label("R41  1 kΩ", fontsize=FS*10))
    gg = d.add(elm.Dot())
    tag(d, gg.center, "ALARM", "left")
    d.add(elm.Resistor().down(2.0).at(gg.center).label("R42\n10 kΩ", loc="bottom", fontsize=FS*10))
    gnd(d, d.here)

    # ------------------------------------------------------------ DIODA RGB
    RX0, RY0 = 69.0, -34.0
    d.add(elm.Label().at((RX0 - 4.5, -30.6)).label("7. RGB status LED", fontsize=FS*14, halign="left"))
    xc = RX0 + 6.4                                   # wspolna katoda
    rows = (("RGB_R", "R51  150 Ω", "R (red)"),
            ("RGB_G", "R52  47 Ω", "G (green)"),
            ("RGB_B", "R53  47 Ω", "B (blue)"))
    ys = []
    for i, (net, rlab, llab) in enumerate(rows):
        y = RY0 - 1.9 * i
        ys.append(y)
        tag(d, (RX0, y), net, "left")
        d.add(elm.Resistor().at((RX0, y)).right(2.6).label(rlab, fontsize=FS*9))
        d.add(elm.LED().right(1.8))
        d.add(elm.Line().tox(xc))
        d.add(elm.Dot())
    d.add(elm.Line().at((xc, ys[0])).toy(ys[-1] - 1.0))
    gnd(d, d.here)
    bx0, bx1 = RX0 + 2.75, RX0 + 5.25
    for a_, b_ in (((bx0, ys[0] + 0.9), (bx1, ys[0] + 0.9)), ((bx1, ys[0] + 0.9), (bx1, ys[-1] - 0.6)),
                   ((bx1, ys[-1] - 0.6), (bx0, ys[-1] - 0.6)), ((bx0, ys[-1] - 0.6), (bx0, ys[0] + 0.9))):
        d.add(elm.Line().at(a_).to(b_).color(GREY).linestyle(":"))
    d.add(elm.Label().at((bx0, ys[0] + 1.0)).label("LED2 (RGB, common cathode)", fontsize=FS*9, halign="left", valign="bottom"))
    note(d, (RX0 - 4.5, ys[-1] - 2.6),
         "LED legs (from left): 1 R, 2 K (common), 3 G, 4 B.\n"
         "Green = standby, red = transmitting,\n"
         "blue = ADS1115 missing.", size=10)

    # ------------------------------------------------------------ PRZYCISK
    PX, PY = 86.5, -34.0
    d.add(elm.Label().at((PX - 3.5, -30.6)).label("8. Button", fontsize=FS*14, halign="left"))
    tag(d, (PX, PY), "BUTTON", "left")
    d.add(elm.Line().right(0.8).at((PX, PY)))
    d.add(elm.Button().down(2.4).label("S1", loc="right", fontsize=FS*10))
    gnd(d, d.here)
    note(d, (PX - 3.5, PY - 4.6),
         "Momentary (NO), on D27.\n"
         "Short: AVG/PEP,\n"
         "long (>0.8 s): profile.", size=10)

    # ------------------------------------------------------------ STOPKA
    note(d, (-4.2, -46.0),
         "Pins as in the DigiMeter32 firmware (menuconfig, target esp32):\n"
         "LCD SCK=D18, MOSI=D23, CS=D5, DC=D33, RST=D4 (VSPI), backlight always on;\n"
         "I²C to ADS1115 SDA=D21, SCL=D22; button D27; alarm D26;\n"
         "RGB LED R=D25, G=D32, B=D13.\n"
         "Measurement via ADS1115 only — if it does not respond, the screen shows an ADS1115 error.\n"
         "Nets with the same name (flags) are connected. THT parts, ready-made modules.",
         size=10)

    # ------------------------------------------------------------ TABELKA
    def cell(x0, y0, x1, y1, label, value, vsize=11, bold=False):
        for a_, b_ in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
            d.add(elm.Line().at(a_).to(b_).linewidth(1.2))
        if label:
            d.add(elm.Label().at((x0 + 0.2, y0 - 0.12)).label(label, fontsize=FS*7.5, color=GREY, halign="left", valign="top"))
        d.add(elm.Label().at((x0 + 0.25, (y0 + y1) / 2 - (0.22 if label else 0))).label(
            value, fontsize=FS*vsize, halign="left", valign="center", color="black"))

    TX0, TX1 = 59.2, XB1 + 1.2           # do prawej krawedzi ramki arkusza
    ty = -46.0
    cell(TX0, ty, TX1, ty - 2.8, "Title", "DigiMeter32 by SP2KMO — schematic diagram", vsize=15)
    ty -= 2.8
    cell(TX0, ty, TX1, ty - 2.0, "Project",
         "Digital power and SWR readout for an analog reflectometer (ESP32 + ADS1115 + LCD 4\")", vsize=10)
    ty -= 2.0
    cell(TX0, ty, TX0 + 23.0, ty - 2.0, "Drawn by", "Kris SP2KMO", vsize=12)
    cell(TX0 + 23.0, ty, TX1, ty - 2.0, "Date", "2026-11-04", vsize=12)
    ty -= 2.0
    w = (TX1 - TX0) / 3
    cell(TX0, ty, TX0 + w, ty - 2.0, "Revision", "1.1", vsize=12)
    cell(TX0 + w, ty, TX0 + 2 * w, ty - 2.0, "Sheet", "1 / 1", vsize=12)
    cell(TX0 + 2 * w, ty, TX1, ty - 2.0, "Format", "A3", vsize=12)
    ty -= 2.0
    cell(TX0, ty, TX0 + 19.0, ty - 2.0, "File", "docs/schematic.pdf  (source: schematic.py)", vsize=9)
    cell(TX0 + 19.0, ty, TX1, ty - 2.0, "Firmware", "DigiMeter32, ESP-IDF 5.5, LVGL 9", vsize=9)
    T_BOTTOM = ty - 2.0

    # tabela zmian (obok tabelki, po lewej)
    HX0, HX1 = 34.0, 57.7
    hy = -46.0
    cols = (HX0, HX0 + 2.6, HX0 + 8.6, HX1 - 5.0, HX1)
    heads = ("Rev.", "Date", "Description", "Author")
    for i in range(4):
        cell(cols[i], hy, cols[i + 1], hy - 1.4, None, heads[i], vsize=8)
    rows = (("1.0", "2026-10-31", "First release", "SP2KMO"),
            ("1.1", "2026-11-04", "Named DigiMeter32; R12/R22 318 kΩ; English", "SP2KMO"))
    for k, row in enumerate(rows):
        y0 = hy - 1.4 - 1.6 * k
        for i in range(4):
            cell(cols[i], y0, cols[i + 1], y0 - 1.6, None, row[i], vsize=9 if i != 2 else 8)
    d.add(elm.Label().at((HX0, hy + 0.15)).label("Revision history", fontsize=FS*9, color=GREY, halign="left", valign="bottom"))

    # ramka arkusza
    OX0, OY0, OX1, OY1 = -6.2, 8.6, TX1, T_BOTTOM     # tabelka w prawym dolnym rogu ramki
    for a_, b_ in (((OX0, OY0), (OX1, OY0)), ((OX1, OY0), (OX1, OY1)), ((OX1, OY1), (OX0, OY1)), ((OX0, OY1), (OX0, OY0))):
        d.add(elm.Line().at(a_).to(b_).linewidth(2.0))
    # tabelka dosunieta do prawego dolnego rogu ramki
    for a_, b_ in (((TX1, T_BOTTOM), (OX1, T_BOTTOM)),):
        pass

    d.save(f"{OUT}.{FMT}")

# białe tło (przezroczyste wygląda na GitHubie w trybie ciemnym jak czarne)
if FMT == "png":
    from PIL import Image
    im = Image.open(f"{OUT}.png").convert("RGBA")
    bg = Image.new("RGBA", im.size, (255, 255, 255, 255))
    Image.alpha_composite(bg, im).convert("RGB").save(f"{OUT}.png", optimize=True)
elif FMT == "svg":
    import re
    with open(f"{OUT}.svg", encoding="utf-8") as f:
        svg = f.read()
    vb = re.search(r'viewBox="([-\d.]+) ([-\d.]+) ([-\d.]+) ([-\d.]+)"', svg)
    x, y, w, h = vb.groups()
    rect = f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="white"/>'
    svg = svg.replace(vb.group(0) + ">", vb.group(0) + ">" + rect, 1)
    with open(f"{OUT}.svg", "w", encoding="utf-8") as f:
        f.write(svg)
print("saved", OUT, FMT)
