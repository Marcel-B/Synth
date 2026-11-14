# Pictures for step 4 of BAUANLEITUNG (website/modul/bauanleitung.html): the three wires from the DAC module to J13,
# seen from the soldering side like the other plans, and the four solder bridges on the back of the GY-PCM5102.
# A side view of J13 used to stand here; it did not say from which side it looked, so L and R were soldered the wrong
# way round on a module (2026-11-14). Everything is now drawn from above the assembled side, as the board lies.
#
#   python3 plan-j13.py <out dir>     writes haupt-j13.png, dac-loetbruecken.png and their -en copies
import math
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
PCB = HERE.parent / "pcb/tonwerk-haupt-tht/tonwerk-haupt-tht.kicad_pcb"
FONTS = Path("/usr/share/fonts/truetype/dejavu")

RED, BLACK, BLUE = (214, 39, 40), (40, 40, 40), (31, 100, 200)
GREY, LIGHT, PURPLE = (150, 150, 150), (215, 215, 215), (140, 80, 190)
TIN = (175, 175, 180)

TEXT = {
    "de": {
        "title": "J13 von der Bestückungsseite",
        "module": "DAC-Modul,\nliegt hier\ndrüber",
        "jack": "Klinke",
        "ground": "Masse",
        "square": "eckiges Pad = L",
        "close": "Vergrößert",
        "notes": [
            "So liegt die Platine, wenn die Bauteile",
            "oben sind und J1 rechts unten (wie in",
            "den anderen Plänen).",
            "",
            "L kommt an das eckige Pad (nach unten,",
            "zu R11 hin), Masse in die Mitte, R oben.",
            "",
            "Am Modul: Pads L, G, R neben der Klinke",
            "(oder die Lötfahnen der Klinke).",
            "",
            "Probe: Steckt der rote Draht am eckigen",
            "Pad, ist es richtig. Vertauschte L und R",
            "erkennt man daran, dass der Filter an",
            "OSC wirkt statt an VOICE.",
        ],
        "middle": "Mitte",
        "bridges": "Lötbrücken auf der Rückseite des GY-PCM5102",
        "bridge_note": [
            "Jede Brücke hat drei Pads. Ein Zinnklecks verbindet das mittlere mit H oder L.",
            "Welche Seite H und welche L ist, steht als Buchstabe neben den Pads des Moduls;",
            "das zählt, nicht die Lage in dieser Zeichnung.",
            "",
            "Prüfen: Durchgang vom mittleren Pad zu GND des Moduls. Bei 1, 2 und 4 piept es,",
            "bei 3 nicht (dort liegen 3,3 V an, sobald das Modul Strom hat).",
        ],
        "rows": [("1", "FLT", "Filter", "L"), ("2", "DEMP", "De-Emphasis aus", "L"),
                 ("3", "XSMT", "Ton an (nicht stumm)", "H"), ("4", "FMT", "Format I2S", "L")],
    },
    "en": {
        "title": "J13 from the assembled side",
        "module": "DAC module\nsits above\nhere",
        "jack": "Jack",
        "ground": "Ground",
        "square": "square pad = L",
        "close": "Enlarged",
        "notes": [
            "The board as it lies with the parts on",
            "top and J1 at the bottom right (as in",
            "the other plans).",
            "",
            "L goes to the square pad (downwards,",
            "towards R11), ground in the middle, R on top.",
            "",
            "On the module: pads L, G, R beside the jack",
            "(or the jack's solder tags).",
            "",
            "Check: the red wire sits on the square pad.",
            "With L and R swapped, the filter acts on",
            "OSC instead of VOICE.",
        ],
        "middle": "middle",
        "bridges": "Solder jumpers on the back of the GY-PCM5102",
        "bridge_note": [
            "Each jumper has three pads. A blob of solder joins the middle one to H or L.",
            "Which side is H and which L is printed beside the module's pads;",
            "that counts, not where they lie in this drawing.",
            "",
            "Check: continuity from the middle pad to the module's GND. 1, 2 and 4 beep,",
            "3 does not (it carries 3.3 V once the module has power).",
        ],
        "rows": [("1", "FLT", "Filter", "L"), ("2", "DEMP", "De-emphasis off", "L"),
                 ("3", "XSMT", "Sound on (not muted)", "H"), ("4", "FMT", "Format I2S", "L")],
    },
}


def font(size, bold=True):
    return ImageFont.truetype(str(FONTS / ("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf")), size)


def footprints():
    s = PCB.read_text()
    result = {}
    for p in re.split(r"\n  \(footprint ", s)[1:]:
        ref = re.search(r'(?:reference "|property "Reference" ")([^"]+)"', p).group(1)
        x, y, a = re.search(r"\(at ([\d.\-]+) ([\d.\-]+)(?: ([\d.\-]+))?\)", p).groups()
        x, y, a = float(x), float(y), math.radians(float(a or 0))
        pads = {}
        for num, shape, lx, ly in re.findall(r'\(pad "(\w*)" thru_hole (\w+) \(at ([\d.\-]+) ([\d.\-]+)', p):
            lx, ly = float(lx), float(ly)
            pads[num] = (x + lx * math.cos(a) + ly * math.sin(a), y - lx * math.sin(a) + ly * math.cos(a), shape)
        result[ref] = pads
    return result


def board_view(t, fps):
    """The DAC corner of the main board, in the plans' photo orientation, and J13 enlarged beside it."""
    W, H = 2000, 1000
    im = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(im)
    # The region around the DAC module, as in hauptplatine-messpunkte (x and y of the board swapped: seen from the
    # assembled side with J1 at the bottom right).
    S, X0, Y0, M = 23.0, 112.0, 130.0, 60.0

    def P(b):
        return (M + (b[1] - X0) * S, 90 + (b[0] - Y0) * S)

    d.text((M, 20), t["title"], fill="black", font=font(34))
    left = (0, 70, 1060, H)
    for ref, pads in fps.items():
        pts = [P(q) for q in pads.values()]
        if not pts or ref == "J13":
            continue
        if not all(left[0] < x < left[2] - 20 and left[1] < y < left[3] - 20 for x, y in pts):
            continue
        if ref[0] in "UJQ":
            x0 = min(q[0] for q in pts) - 14; x1 = max(q[0] for q in pts) + 14
            y0 = min(q[1] for q in pts) - 14; y1 = max(q[1] for q in pts) + 14
            d.rectangle([x0, y0, x1, y1], outline=(175, 175, 175), width=2)
            if ref != "U5":
                tw = d.textlength(ref, font=font(20))
                d.text(((x0 + x1) / 2 - tw / 2, (y0 + y1) / 2 - 12), ref, fill=GREY, font=font(20))
        elif len(pts) == 2:
            d.line(pts, fill=LIGHT, width=8)
            mx, my = (pts[0][0] + pts[1][0]) / 2, (pts[0][1] + pts[1][1]) / 2
            d.text((mx + 6, my + 6), ref, fill=GREY, font=font(17))
        for x, y in pts:
            d.ellipse([x - 4, y - 4, x + 4, y + 4], outline=(190, 190, 190), width=2)

    # The module above its socket, with its jack where the GY-PCM5102 has it.
    u5 = [P(q) for q in fps["U5"].values()]
    mx0 = min(q[0] for q in u5) - 30; mx1 = max(q[0] for q in u5) + 30
    my0 = min(q[1] for q in u5) - 30
    jack = (mx0 + 60, my0 + 420, mx0 + 230, my0 + 500)
    d.rectangle([mx0, my0, mx1, jack[3] + 30], outline=PURPLE, width=3)
    d.rounded_rectangle(jack, radius=10, outline=PURPLE, width=3)
    d.text((jack[0] + 18, jack[1] + 28), t["jack"], fill=PURPLE, font=font(24))
    d.multiline_text((mx0 + 150, my0 + 130), t["module"], fill=PURPLE, font=font(24), spacing=6)

    # J13 and the wires, L at the square pad; the wires leave the jack in the order they arrive, so none cross.
    pads = fps["J13"]
    colours = {"1": RED, "2": BLACK, "3": BLUE}
    names = {"1": "L", "2": t["ground"], "3": "R"}
    order = sorted(("1", "2", "3"), key=lambda n: P(pads[n])[1])
    for i, num in enumerate(order):
        x, y = P(pads[num])
        source = (jack[0], jack[1] + 25 + 25 * i)
        d.line([source, (x + 70, y), (x, y)], fill=colours[num], width=6, joint="curve")
        r = 13
        if num == "1":
            d.rectangle([x - r, y - r, x + r, y + r], outline=colours[num], width=5)
        else:
            d.ellipse([x - r, y - r, x + r, y + r], outline=colours[num], width=5)
        tw = d.textlength(names[num], font=font(30))
        d.text((x - 30 - tw, y - 18), names[num], fill=colours[num], font=font(30))
    x, y = P(pads[order[0]])
    d.text((x - 30, y - 75), "J13", fill="black", font=font(32))

    # The enlarged J13 on the right, same orientation.
    d.line([(1080, 70), (1080, H - 20)], fill="black", width=3)
    cx, top = 1250, 120
    d.text((1120, 80), t["close"], fill=GREY, font=font(24))
    for i, num in enumerate(order):
        y = top + 60 + i * 120
        c = colours[num]
        d.line([(cx - 160, y), (cx, y)], fill=c, width=10)
        r = 34
        if num == "1":
            d.rectangle([cx - r, y - r, cx + r, y + r], outline=c, width=8, fill=TIN)
        else:
            d.ellipse([cx - r, y - r, cx + r, y + r], outline=c, width=8, fill=TIN)
        d.text((cx + 60, y - 24), names[num], fill=c, font=font(44))
    d.text((cx - 100, top + 60 + 2 * 120 + 50), t["square"], fill=RED, font=font(30))
    d.multiline_text((1120, top + 420), "\n".join(t["notes"]), fill="black", font=font(25, False), spacing=8)
    return im


def bridges_view(t):
    W, H = 1100, 900
    im = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(im)
    d.text((40, 30), t["bridges"], fill="black", font=font(34))
    pad, gap, x0, y0 = 70, 30, 120, 140
    for i, (num, name, what, side) in enumerate(t["rows"]):
        y = y0 + i * 130
        d.text((40, y + 14), num, fill="black", font=font(40))
        xs = [x0 + k * (pad + gap) for k in range(3)]
        # The middle pad joined to H (left) or L (right) by a blob.
        a, b = (xs[0], xs[1]) if side == "H" else (xs[1], xs[2])
        d.rounded_rectangle([a, y, b + pad, y + pad], radius=26, fill=TIN)
        for k, x in enumerate(xs):
            d.rectangle([x + 8, y + 8, x + pad - 8, y + pad - 8], outline=(200, 140, 40), width=4)
        d.text((xs[0] + 22, y + pad + 4), "H", fill=GREY, font=font(24))
        d.text((xs[2] + 26, y + pad + 4), "L", fill=GREY, font=font(24))
        mid = t["middle"]
        d.text((xs[1] + pad / 2 - d.textlength(mid, font=font(18)) / 2, y + pad + 8), mid, fill=GREY, font=font(18))
        d.text((x0 + 3 * (pad + gap) + 40, y + 6), f"{name}: {what}", fill="black", font=font(30))
        c = BLUE if side == "H" else RED
        d.text((x0 + 3 * (pad + gap) + 40, y + 44), f"→ {side}", fill=c, font=font(28))
    d.multiline_text((40, y0 + 4 * 130 + 20), "\n".join(t["bridge_note"]), fill="black", font=font(24, False),
                     spacing=8)
    return im


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    fps = footprints()
    for lang, suffix in (("de", ""), ("en", "-en")):
        board_view(TEXT[lang], fps).save(out / f"haupt-j13{suffix}.png", optimize=True)
        bridges_view(TEXT[lang]).save(out / f"dac-loetbruecken{suffix}.png", optimize=True)


if __name__ == "__main__":
    main()
