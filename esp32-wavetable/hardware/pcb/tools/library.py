"""The project's own symbols and footprints for the through-hole main board: the ESP32-S3-DevKitC-1 and the PCM5102A
module (GY-PCM5102), both plugged into sockets. Writes them into lib/tonwerk.kicad_sym and lib/tonwerk.pretty.

The DevKit's pins are Espressif's table for ESP32-S3-DevKitC-1 v1.0/v1.1 (J1 left, J3 right, pin 1 at the antenna
end, seen from the top). Espressif's drawing gives 25.4 mm between the rows; some boards and footprints have 22.86 mm,
so J3 gets a second row of holes 2.54 mm further in, and the socket goes into the row that fits.
"""

import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.abspath(os.path.join(HERE, "..", "lib"))

DEVKIT_J1 = ["3V3", "3V3", "RST", "IO4", "IO5", "IO6", "IO7", "IO15", "IO16", "IO17", "IO18", "IO8", "IO3", "IO46",
             "IO9", "IO10", "IO11", "IO12", "IO13", "IO14", "5V", "G"]
DEVKIT_J3 = ["G", "TX", "RX", "IO1", "IO2", "IO42", "IO41", "IO40", "IO39", "IO38", "IO37", "IO36", "IO35", "IO0",
             "IO45", "IO48", "IO47", "IO21", "IO20", "IO19", "G", "G"]
ROW = 25.4        # J1 to J3, Espressif's drawing
ROW_ALT = 22.86   # the narrower spacing of some boards
PITCH = 2.54

PCM_PINS = ["VIN", "GND", "LCK", "DIN", "BCK", "SCK"]
PCM_BODY = (32.0, 17.0)   # the GY-PCM5102's board as sellers give it; the header runs along the short edge


def font(size=1.27):
    return f"(effects (font (size {size} {size})))"


def pin(kind, x, y, angle, name, number, length=2.54):
    return (f'      (pin {kind} line (at {x:g} {y:g} {angle}) (length {length:g}) (name "{name}" {font()}) '
            f'(number "{number}" {font()}))')


def symbol(name, ref, value, footprint, description, left, right, width):
    """A box with pins on the left and right, numbered down the left and then down the right."""
    rows = max(len(left), len(right))
    top = (rows - 1) * PITCH / 2
    top = round(top / PITCH) * PITCH
    h = top + PITCH
    lines = [f'  (symbol "{name}" (in_bom yes) (on_board yes)',
             f'    (property "Reference" "{ref}" (at 0 {h + 2.54:g} 0) {font()})',
             f'    (property "Value" "{value}" (at 0 {top - PITCH * rows - 2.54:g} 0) {font()})',
             f'    (property "Footprint" "{footprint}" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
             f'    (property "Datasheet" "~" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
             f'    (property "ki_description" "{description}" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
             f'    (symbol "{name}_0_1"',
             f'      (rectangle (start {-width:g} {h:g}) (end {width:g} {top - PITCH * rows:g}) '
             f'(stroke (width 0.254) (type default)) (fill (type background)))',
             '    )',
             f'    (symbol "{name}_1_1"']
    for i, n in enumerate(left):
        lines.append(pin("passive", -width - 2.54, round(top - i * PITCH, 2), 0, n, i + 1))
    for i, n in enumerate(right):
        lines.append(pin("passive", width + 2.54, round(top - i * PITCH, 2), 180, n, len(left) + i + 1))
    lines += ['    )', '  )']
    return "\n".join(lines)


def symbols():
    return [
        symbol("ESP32-S3-DevKitC-1", "U", "ESP32-S3-DevKitC-1-N16R8", "tonwerk:ESP32-S3-DevKitC-1_Socket",
               "Espressif ESP32-S3-DevKitC-1 auf zwei Buchsenleisten 1x22. J1 links, J3 rechts, Pin 1 am Antennenende.",
               DEVKIT_J1, DEVKIT_J3, 7.62),
        symbol("GY-PCM5102", "U", "GY-PCM5102", "tonwerk:GY-PCM5102_Socket",
               "PCM5102A-Modul (lila, GY-PCM5102) auf einer Buchsenleiste 1x6; der Ausgang kommt über Drähte.",
               PCM_PINS, [], 7.62),
    ]


def write_symbols():
    path = os.path.join(LIB, "tonwerk.kicad_sym")
    text = open(path).read()
    # Drop earlier copies of the generated symbols, keep the hand-written ones (WS2812D-F5).
    for name in ("ESP32-S3-DevKitC-1", "GY-PCM5102"):
        text = re.sub(r'\n  \(symbol "%s" .*?\n  \)(?=\n)' % re.escape(name), "", text, flags=re.S)
    body = text.rstrip()
    assert body.endswith(")")
    body = body[:-1].rstrip() + "\n" + "\n".join(symbols()) + "\n)\n"
    open(path, "w").write(body)


def pad(number, x, y, square=False):
    shape = "rect" if square else "oval"
    return (f'  (pad "{number}" thru_hole {shape} (at {x:g} {y:g}) (size 1.7 1.7) (drill 1) '
            f'(layers "*.Cu" "*.Mask"))')


def line(layer, x0, y0, x1, y1, width):
    return (f'  (fp_line (start {x0:g} {y0:g}) (end {x1:g} {y1:g}) (stroke (width {width:g}) (type solid)) '
            f'(layer "{layer}"))')


def rect(layer, x0, y0, x1, y1, width):
    return (f'  (fp_rect (start {x0:g} {y0:g}) (end {x1:g} {y1:g}) (stroke (width {width:g}) (type solid)) '
            f'(fill none) (layer "{layer}"))')


def text(kind, value, x, y, layer, size=1.0, angle=0):
    return (f'  (fp_text {kind} "{value}" (at {x:g} {y:g} {angle:g}) (layer "{layer}") '
            f'(effects (font (size {size:g} {size:g}) (thickness {size * 0.15:.3f}))))')


def footprint(name, descr, items):
    return "\n".join([f'(footprint "{name}" (version 20221018) (generator tonwerk) (layer "F.Cu")',
                      f'  (descr "{descr}")', '  (attr through_hole)'] + items + [")"]) + "\n"


def devkit():
    """Seen from the DevKit's top, which faces away from the board it plugs into: origin at J1 pin 1."""
    n = len(DEVKIT_J1)
    end = (n - 1) * PITCH
    items = [text("reference", "REF**", ROW / 2, -3.5, "F.SilkS", 1.2),
             text("value", "ESP32-S3-DevKitC-1", ROW / 2, end / 2, "F.Fab", 1.2, 90)]
    for i in range(n):
        items.append(pad(i + 1, 0, i * PITCH, i == 0))
    for i in range(n):
        items.append(pad(n + i + 1, ROW, i * PITCH, i == 0))
        items.append(pad(n + i + 1, ROW_ALT, i * PITCH, i == 0))
    # Courtyard only round the rows: the DevKit sits 11 mm up on its sockets, low parts fit under it.
    for x0, x1 in ((-1.5, 1.5), (ROW_ALT - 1.5, ROW + 1.5)):
        items.append(rect("F.CrtYd", x0, -1.5, x1, end + 1.5, 0.05))
    # The board itself, approximately (about 28 x 63 mm, USB at the end of pin 22).
    items.append(rect("F.Fab", -1.27, -5.5, ROW + 1.27, end + 5.0, 0.1))
    for x in (0, ROW, ROW_ALT):
        items.append(rect("F.SilkS", x - 1.27, -1.27, x + 1.27, end + 1.27, 0.12))
    # Labels only beyond the ends of the rows: in between sit other parts.
    items += [text("user", "J1", 0, -2.4, "F.SilkS", 1.0),
              text("user", "J3 25,4", ROW - 1.0, -2.4, "F.SilkS", 1.0),
              text("user", "22,86", ROW_ALT - 0.5, end + 2.4, "F.SilkS", 1.0),
              text("user", "USB", ROW / 2, end + 2.4, "F.SilkS", 1.2)]
    return footprint("ESP32-S3-DevKitC-1_Socket",
                     "ESP32-S3-DevKitC-1 auf zwei Buchsenleisten 1x22, J3 wahlweise 25,4 oder 22,86 mm von J1",
                     items)


def pcm_module():
    """Origin at VIN; the module lies over the board on the -x side (seen from its own side)."""
    n = len(PCM_PINS)
    end = (n - 1) * PITCH
    length, width = PCM_BODY
    y0 = end / 2 - width / 2
    items = [text("reference", "REF**", -length / 2, y0 - 1.5, "F.SilkS", 1.2),
             text("value", "GY-PCM5102", -length / 2, end / 2, "F.Fab", 1.2)]
    for i, name in enumerate(PCM_PINS):
        items.append(pad(i + 1, 0, i * PITCH, i == 0))
        items.append(text("user", name, -3.6, i * PITCH, "F.SilkS", 0.9))
    items.append(rect("F.SilkS", -1.27, -1.27, 1.27, end + 1.27, 0.12))
    items.append(rect("F.CrtYd", -1.5, -1.5, 1.5, end + 1.5, 0.05))
    items.append(rect("F.Fab", -length + 1.5, y0, 1.5, y0 + width, 0.1))
    items.append(text("user", "PCM5102-Modul", -length / 2 - 2, end / 2, "F.SilkS", 1.2))
    return footprint("GY-PCM5102_Socket", "PCM5102A-Modul GY-PCM5102 auf Buchsenleiste 1x6", items)


def write_footprints():
    d = os.path.join(LIB, "tonwerk.pretty")
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "ESP32-S3-DevKitC-1_Socket.kicad_mod"), "w").write(devkit())
    open(os.path.join(d, "GY-PCM5102_Socket.kicad_mod"), "w").write(pcm_module())


if __name__ == "__main__":
    write_symbols()
    write_footprints()
