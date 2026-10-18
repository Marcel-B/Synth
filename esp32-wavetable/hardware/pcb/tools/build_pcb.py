"""Builds both boards from parts.py: footprints placed, nets assigned, routed by Freerouting, GND poured, DRC report.

Run build_sch.py first (it writes the schematics whose symbol uuids the footprints link to).
"""

import json
import os
import subprocess
import sys

os.environ.setdefault("KICAD7_FOOTPRINT_DIR", "/usr/share/kicad/footprints")
os.environ.setdefault("KICAD7_SYMBOL_DIR", "/usr/share/kicad/symbols")
import pcbnew  # noqa: E402


def add(board, item):
    item.thisown = 0   # the board owns it now; without this Python frees it a second time
    board.Add(item)
    return item

sys.path.insert(0, os.path.dirname(__file__))
import parts as P
import schematic
from sexpr import find, first, parse

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, ".."))
FOOTPRINTS = "/usr/share/kicad/footprints"
LOCAL_FOOTPRINTS = {"tonwerk": os.path.join(OUT, "lib", "tonwerk.pretty")}
FREEROUTING = os.environ.get("FREEROUTING", "/home/user/tools/freerouting.jar")
LOCAL = {"tonwerk": os.path.join(OUT, "lib", "tonwerk.kicad_sym")}

OFFSET = (100.0, 50.0)                 # board coordinates = panel coordinates + OFFSET
OUTLINE = (2.4, 11.0, 68.4, 118.5)     # panel coordinates; 107.5 mm high, clear of the rails
HOLES = [(24.2, 63.0), (46.6, 63.0)]   # M3 spacers between the boards, free on both

POWER = ["+12V", "-12V", "+12V_IN", "-12V_IN", "+5V", "+5V_REG", "VBUS", "+3V3", "+3V3A"]

# Where the auto-placed groups of the main board go (panel coordinates, back side), tried in this order.
REGIONS = {
    # The module sits on the back, so its pins 1-14 (3V3, IO4-8, IO15-20, USB) face the right, toward J4, and the
    # bottom row (IO9-14, I2S) faces down: the DAC goes under the module, the output stage next to J4's outputs.
    "potcaps": [(3.0, 12.0, 22.6, 52.0)],
    "esp": [(48.6, 12.0, 67.8, 30.0), (48.6, 12.0, 67.8, 62.0)],
    "dac": [(26.5, 39.0, 44.5, 58.0), (3.0, 52.0, 45.0, 66.0)],
    "out": [(48.6, 30.0, 67.8, 52.0), (48.6, 12.0, 67.8, 62.0)],
    "gate": [(48.6, 52.0, 67.8, 62.0), (3.0, 52.0, 67.8, 86.0)],
    "cv": [(3.0, 56.0, 45.0, 86.0), (3.0, 52.0, 67.8, 86.0)],
    "adc": [(27.0, 62.0, 67.8, 86.0), (3.0, 52.0, 67.8, 86.0)],
    "power": [(3.0, 87.0, 54.0, 118.0)],
    "ref": [(27.0, 62.0, 67.8, 86.0), (3.0, 87.0, 54.0, 118.0)],
}

# The through-hole main board. The DevKit (bottom left) and the DAC module (right of it) sit 11 mm up on their
# sockets: under them go only low parts (resistors, ceramics, DIPs, TO-92, diodes); electrolytics, the converter and
# the Eurorack header stay outside. The top third is free on the back.
_LEFT, _MID, _RIGHT = (3.0, 13.6, 22.6, 58.0), (25.8, 13.6, 45.0, 58.0), (48.2, 13.6, 68.0, 58.0)
_UNDER_DEVKIT = (3.0, 58.5, 31.6, 118.2)
_UNDER_DAC = (32.4, 85.6, 68.0, 104.2)
_POWER_TOP, _POWER_BOTTOM = (32.4, 58.5, 68.0, 85.2), (32.4, 104.4, 68.0, 118.2)
REGIONS_THT = {
    "potcaps": [_UNDER_DEVKIT],
    "gate": [_UNDER_DEVKIT, _LEFT, _POWER_TOP],
    "out": [_UNDER_DAC, _POWER_TOP, _MID],
    "cv": [_RIGHT, _MID, _LEFT, _UNDER_DEVKIT, _POWER_TOP, _UNDER_DAC],
    "adc": [_MID, _RIGHT, _LEFT, _UNDER_DEVKIT],
    "ref": [_LEFT, _MID, _UNDER_DEVKIT],
    "power": [_POWER_TOP, _POWER_BOTTOM, _LEFT],
    "powerlow": [_POWER_BOTTOM, _UNDER_DAC, _POWER_TOP, _UNDER_DEVKIT],
}
# The passives of the small groups first, so the CV stages' many resistors take what is left.
ORDER_THT = ["adc", "ref", "gate", "out", "potcaps", "power", "powerlow", "cv"]
# Footprints whose courtyard spans a module on sockets: only their pins and sockets block the board.
SOCKETED = ("ESP32-S3-DevKitC-1_Socket", "GY-PCM5102_Socket")

# The point of a footprint that sits at Part.at, in the footprint's own coordinates (None: courtyard centre).
ANCHORS = {
    "Potentiometer_Alpha_RD901F-40-00D_Single_Vertical": (7.5, 2.5),    # the shaft
    "Jack_3.5mm_QingPu_WQP-PJ398SM_Vertical_CircularHoles": (0.0, 6.48),  # the thread
    "LED_D5.0mm-4_RGB": (1.905, 0.0),
    "SW_PUSH_6mm_H13mm": (3.25, 2.25),
    "PinHeader_1x14_P2.54mm_Vertical": (0.0, 0.0),                     # pin 1
    "PinSocket_1x14_P2.54mm_Vertical": (0.0, 0.0),
    "ESP32-S3-WROOM-1": (0.0, 0.0),                                    # the module's centre
    "ESP32-S3-DevKitC-1_Socket": (0.0, 0.0),                           # J1 pin 1
    "GY-PCM5102_Socket": (0.0, 0.0),                                   # VIN
}
# Pots turn with their pins down: pin 1 left, pin 3 (VREF) right, clockwise raises the voltage.
ROTATION = {"Potentiometer_Alpha_RD901F-40-00D_Single_Vertical": 90.0}


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(x + OFFSET[0]), mm(y + OFFSET[1]))


def courtyard(fp):
    fp.BuildCourtyardCaches()
    poly = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd)
    box = fp.GetBoundingBox(False, False) if poly.OutlineCount() == 0 else poly.BBox()
    return (pcbnew.ToMM(box.GetLeft()) - OFFSET[0], pcbnew.ToMM(box.GetTop()) - OFFSET[1],
            pcbnew.ToMM(box.GetRight()) - OFFSET[0], pcbnew.ToMM(box.GetBottom()) - OFFSET[1])


def load(fpid):
    lib, name = fpid.split(":")
    fp = pcbnew.FootprintLoad(LOCAL_FOOTPRINTS.get(lib, f"{FOOTPRINTS}/{lib}.pretty"), name)
    if fp is None:
        raise RuntimeError("footprint not found: " + fpid)
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp, name


def trim_module(fp):
    """The WROOM footprint keeps 24 mm either side of the antenna free, for a module at the board's edge with WiFi in
    use. The firmware never turns the radio on, so only the antenna itself stays free of copper; courtyard to match."""
    for zone in fp.Zones():
        if zone.GetIsRuleArea():
            o = zone.Outline()
            o.RemoveAllContours()
            o.NewOutline()
            for x, y in ((-9.05, -12.8), (9.05, -12.8), (9.05, -6.75), (-9.05, -6.75)):
                o.Append(mm(x), mm(y))
    # The ground pad's vias come with 0.2 mm drills; JLCPCB's standard process starts at 0.3 mm.
    for pad in fp.Pads():
        if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and pad.GetDrillSize().x < mm(0.3):
            pad.SetDrillSize(pcbnew.VECTOR2I(mm(0.3), mm(0.3)))
            pad.SetSize(pcbnew.VECTOR2I(mm(0.6), mm(0.6)))
    for item in list(fp.GraphicalItems()):
        if item.GetLayer() == pcbnew.F_CrtYd:
            fp.Remove(item)
    rect = pcbnew.FP_SHAPE(fp)
    rect.SetShape(pcbnew.SHAPE_T_RECT)
    rect.SetStart0(pcbnew.VECTOR2I(mm(-9.3), mm(-13.05)))
    rect.SetEnd0(pcbnew.VECTOR2I(mm(9.3), mm(13.6)))
    rect.SetLayer(pcbnew.F_CrtYd)
    rect.SetWidth(mm(0.05))
    rect.SetDrawCoord()
    rect.thisown = 0
    fp.Add(rect)


def place(fp, name, part, x, y):
    """Turns and flips the footprint at the origin, then moves it so its anchor lands on (x, y)."""
    fp.SetPosition(pt(0.0, 0.0))
    fp.SetOrientationDegrees(part.rot or ROTATION.get(name, 0.0))
    if part.side == "B" and not fp.IsFlipped():
        fp.Flip(fp.GetPosition(), True)
    anchor = ANCHORS.get(name)
    if anchor is None:
        x0, y0, x1, y1 = courtyard(fp)
        ax, ay = (x0 + x1) / 2, (y0 + y1) / 2
    else:
        # Rotate (and mirror) the anchor the way KiCad turned the footprint: positive angles counter-clockwise on
        # screen, with y pointing down.
        import math
        a = math.radians(fp.GetOrientationDegrees())
        lx, ly = anchor
        if part.side == "B":
            lx = -lx
        ax = lx * math.cos(a) + ly * math.sin(a)
        ay = -lx * math.sin(a) + ly * math.cos(a)
    fp.SetPosition(pt(x - ax, y - ay))


def overlaps(a, b, gap):
    return not (a[2] + gap <= b[0] or b[2] + gap <= a[0] or a[3] + gap <= b[1] or b[3] + gap <= a[1])


GAP = 1.0   # mm between courtyards of auto-placed parts; main() tries a few until Freerouting connects everything


def socket_boxes(fp):
    """One box per pin, as wide as the socket's body (2.54 mm)."""
    out = []
    for pad in fp.Pads():
        x, y = pcbnew.ToMM(pad.GetPosition().x) - OFFSET[0], pcbnew.ToMM(pad.GetPosition().y) - OFFSET[1]
        out.append((x - 1.27, y - 1.27, x + 1.27, y + 1.27))
    return out


def first_fit(fp, name, part, regions, taken):
    """(region index, y, x) of the first free spot, row by row, or None; courtyards keep GAP apart."""
    place(fp, name, part, 0.0, 0.0)
    x0, y0, x1, y1 = courtyard(fp)
    w, h = x1 - x0, y1 - y0
    for i, (rx0, ry0, rx1, ry1) in enumerate(regions):
        y = ry0
        while y + h <= ry1 + 1e-6:
            x = rx0
            while x + w <= rx1 + 1e-6:
                box = (x, y, x + w, y + h)
                if not any(overlaps(box, t, GAP) for t in taken):
                    return i, y, x, w, h
                x += 0.5
            y += 0.5
    return None


def pack(fp, name, part, regions, taken):
    """First free spot inside one of the regions; on the through-hole board two-pin parts may also turn by 90
    degrees, whichever finds the earlier spot."""
    turns = [part.rot]
    if part.footprint.startswith(("Resistor_THT", "Capacitor_THT", "Diode_THT")):
        turns.append((part.rot + 90.0) % 180.0)
    best = None
    for rot in turns:
        part.rot = rot
        fit = first_fit(fp, name, part, regions, taken)
        if fit and (best is None or fit[:3] < best[1][:3]):
            best = (rot, fit)
    if best is None:
        raise RuntimeError(f"no room for {part.ref} in group {part.group}")
    part.rot, (_, y, x, w, h) = best
    place(fp, name, part, x + w / 2, y + h / 2)
    return courtyard(fp)


def tidy_silk(fp):
    """Reference in the middle of the part, small: on the crowded through-hole board the library's spot beside the
    part lands on the neighbour's."""
    ref = fp.Reference()
    box = fp.GetBoundingBox(False, False)
    ref.SetPosition(box.Centre())
    ref.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
    ref.SetTextThickness(mm(0.12))
    ref.SetTextAngleDegrees(fp.GetOrientationDegrees() % 180)


def symbol_uuids(sch_path):
    doc = parse(open(sch_path).read())[0]
    out = {}
    for sym in find(doc, "symbol"):
        ref = [p[2] for p in find(sym, "property") if p[1] == "Reference"]
        if ref and not ref[0].startswith("#"):
            out.setdefault(ref[0], first(sym, "uuid")[1])
    return out


def stripe_position(header, name):
    """Where "-12V" goes: beyond the pin 1 end of the header on the SMD board; on the through-hole board, where that
    end meets the board's edge, above the shroud at the pins 1/2 end (the red stripe's)."""
    pin1 = header.FindPadByNumber("1").GetPosition()
    if name.endswith("-tht"):
        pin2 = header.FindPadByNumber("2").GetPosition()
        dx, dy, away = pin2.x - pin1.x, pin2.y - pin1.y, mm(6.6)
    else:
        pin9 = header.FindPadByNumber("9").GetPosition()
        dx, dy, away = pin1.x - pin9.x, pin1.y - pin9.y, mm(7.2)
    length = (dx * dx + dy * dy) ** 0.5
    return pcbnew.VECTOR2I(int(pin1.x + dx / length * away), int(pin1.y + dy / length * away))


def build(name, parts):
    d = os.path.join(OUT, name)
    path = os.path.join(d, name + ".kicad_pcb")
    for f in os.listdir(d):
        if f.endswith((".kicad_pcb", ".kicad_prl", ".dsn", ".ses")) or f == "fp-info-cache":
            os.remove(os.path.join(d, f))
    board = pcbnew.NewBoard(path)
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_MinClearance = mm(0.2)
    ds.m_TrackMinWidth = mm(0.2)
    ds.m_ViasMinSize = mm(0.6)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(0.5)   # for the router; the DRC holds it to JLCPCB's 0.3 mm
    ds.m_HoleToHoleMin = mm(0.5)

    lib = schematic.Library(LOCAL)
    uuids = symbol_uuids(os.path.join(d, name + ".kicad_sch"))
    nets = {}

    def net(n):
        if n not in nets:
            nets[n] = pcbnew.NETINFO_ITEM(board, n)
            nets[n] = add(board, nets[n])
        return nets[n]

    # Outline.
    x0, y0, x1, y1 = OUTLINE
    for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(pt(*a))
        seg.SetEnd(pt(*b))
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetWidth(mm(0.1))
        add(board, seg)

    taken_f, taken_b = [], []   # courtyards per side; THT parts block both
    for i, (hx, hy) in enumerate(HOLES):
        fp, fname = load("MountingHole:MountingHole_3.2mm_M3")
        fp.SetReference("H%d" % (i + 1))
        fp.SetValue("M3")
        fp.SetPosition(pt(hx, hy))
        add(board, fp)
        box = (hx - 3.5, hy - 3.5, hx + 3.5, hy + 3.5)
        taken_f.append(box)
        taken_b.append(box)

    footprints = {}
    unplaced = []
    for part in parts:
        fp, fname = load(part.footprint)
        if "WROOM" in fname:
            trim_module(fp)
        if fname == "LED_D5.0mm-4_RGB":
            # The library's round pads at 1.27 mm leave an annular ring of 0.085 mm, under JLCPCB's 0.13 mm:
            # narrow ovals with a 0.7 mm drill for the LED's 0.45 mm leads.
            for pad in fp.Pads():
                pad.SetShape(pcbnew.PAD_SHAPE_RECT if pad.GetNumber() == "1" else pcbnew.PAD_SHAPE_OVAL)
                pad.SetSize(pcbnew.VECTOR2I(mm(1.05), mm(1.9)))
                pad.SetDrillSize(pcbnew.VECTOR2I(mm(0.7), mm(0.7)))
        fp.SetReference(part.ref)
        fp.SetValue(part.value)
        if part.ref in uuids:
            fp.SetPath(pcbnew.KIID_PATH("/" + uuids[part.ref]))
        if part.lcsc:
            fp.SetProperty("LCSC", part.lcsc)
        add(board, fp)
        footprints[part.ref] = (fp, fname, part)
        # Nets by pin number, the way the schematic resolved them.
        pins = schematic.Library.pins(lib.symbol(part.symbol))
        by_number = schematic.resolve_pins(part, pins)
        for pad in fp.Pads():
            n = by_number.get(pad.GetNumber())
            if n is None and pad.GetNumber() == "" and "WROOM" in fname:
                n = "GND"   # the thermal vias of the module's ground pad
            if n is not None:
                pad.SetNet(net(n))
        if part.at is not None:
            place(fp, fname, part, *part.at)
            boxes = socket_boxes(fp) if fname in SOCKETED else [courtyard(fp)]
            tht = any(p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH for p in fp.Pads())
            if part.side == "F" or tht:
                taken_f.extend(boxes)
            if part.side == "B" or tht:
                taken_b.extend(boxes)
        else:
            unplaced.append(part.ref)

    # Chips first, so they take the top of their region and their passives gather round.
    regions = REGIONS_THT if name.endswith("-tht") else REGIONS
    if name == "tonwerk-haupt-tht":
        taken_b.append((14.0, 11.0, 57.0, 13.4))   # the board's name
    if name.endswith("-tht"):
        unplaced.sort(key=lambda r: (not r.startswith(("U", "Q", "SW")), ORDER_THT.index(footprints[r][2].group)))
    else:
        unplaced.sort(key=lambda r: not r.startswith(("U", "Q", "SW")))
    for ref in unplaced:
        fp, fname, part = footprints[ref]
        box = pack(fp, fname, part, regions[part.group], taken_b)
        taken_b.append(box)
        if name.endswith("-tht"):
            tidy_silk(fp)

    # Board text on the back silk, readable from behind.
    label = pcbnew.PCB_TEXT(board)
    label.SetText({"tonwerk-bedien": "Tonwerk Wavetable · Bedienplatine v0.1",
                   "tonwerk-haupt": "Tonwerk Wavetable · Hauptplatine v0.1",
                   "tonwerk-bedien-tht": "Tonwerk Wavetable · Bedienplatine bedrahtet v0.1",
                   "tonwerk-haupt-tht": "Tonwerk Wavetable · Hauptplatine bedrahtet v0.1"}[name])
    label.SetLayer(pcbnew.B_SilkS)
    label.SetMirrored(True)
    label.SetTextSize(pcbnew.VECTOR2I(mm(1.2), mm(1.2)))
    label.SetTextThickness(mm(0.18))
    label.SetPosition({"tonwerk-haupt": pt(47.0, 117.2), "tonwerk-haupt-tht": pt(35.4, 12.2)}.get(name, pt(35.4, 85.3)))
    add(board, label)
    if name.startswith("tonwerk-haupt"):
        stripe = pcbnew.PCB_TEXT(board)
        stripe.SetText("-12V")
        stripe.SetLayer(pcbnew.B_SilkS)
        stripe.SetMirrored(True)
        stripe.SetTextSize(pcbnew.VECTOR2I(mm(1.2), mm(1.2)))
        stripe.SetTextThickness(mm(0.18))
        stripe.SetPosition(stripe_position(footprints["J1"][0], name))
        add(board, stripe)

    pcbnew.SaveBoard(path, board)
    if any(p.footprint.startswith("tonwerk:") for p in parts):
        with open(os.path.join(d, "fp-lib-table"), "w") as f:
            f.write('(fp_lib_table\n  (lib (name "tonwerk")(type "KiCad")(uri "${KIPRJMOD}/../lib/tonwerk.pretty")'
                    '(options "")(descr "Tonwerk: DevKit und PCM5102-Modul auf Buchsenleisten"))\n)\n')
    return path


SEVERITIES = {
    # A pad that the router reached by a track and the pour by one spoke is connected twice; KiCad calls the missing
    # second spoke an error.
    "starved_thermal": "warning",
    "lib_footprint_mismatch": "ignore",   # the LED's pads are changed on purpose
    "lib_footprint_issues": "ignore",     # lib/tonwerk.pretty is only in the board's own fp-lib-table
}


def set_netclasses(name, clearance=0.2):
    pro = os.path.join(OUT, name, name + ".kicad_pro")
    data = json.load(open(pro))
    ns = data["net_settings"]
    default = next(c for c in ns["classes"] if c["name"] == "Default")
    default.update(track_width=0.25, clearance=clearance, via_diameter=0.6, via_drill=0.3)
    power = dict(default, name="Power", track_width=0.5)
    ns["classes"] = [default, power]
    ns["netclass_patterns"] = [{"netclass": "Power", "pattern": n} for n in POWER]
    data.setdefault("board", {}).setdefault("design_settings", {}).setdefault("rule_severities", {}).update(SEVERITIES)
    json.dump(data, open(pro, "w"), indent=2)


def import_session(board, ses):
    """Freerouting's tracks and vias into the board. pcbnew's own importer only works inside the editor."""
    doc = parse(open(ses).read())[0]
    routes = first(doc, "routes")
    res = first(routes, "resolution")
    scale = {"um": 1e-3, "mm": 1.0, "mil": 0.0254}[str(res[1])] / float(res[2])
    layers = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu}

    def p(x, y):
        return pcbnew.VECTOR2I(mm(float(x) * scale), mm(-float(y) * scale))

    count = 0
    for n in find(first(routes, "network_out"), "net"):
        net = board.FindNet(str(n[1]))
        for wire in find(n, "wire"):
            path = first(wire, "path")
            layer, width, coords = layers[str(path[1])], float(path[2]) * scale, path[3:]
            pts = [p(coords[i], coords[i + 1]) for i in range(0, len(coords) - 1, 2)]
            for a, b in zip(pts, pts[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(a)
                t.SetEnd(b)
                t.SetWidth(mm(width))
                t.SetLayer(layer)
                t.SetNet(net)
                add(board, t)
                count += 1
        for v in find(n, "via"):
            size, drill = (float(s) for s in str(v[1]).split("_")[1].split(":"))
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(p(v[2], v[3]))
            via.SetWidth(mm(size / 1000))
            via.SetDrill(mm(drill / 1000))
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            via.SetNet(net)
            add(board, via)
    if count == 0:
        raise RuntimeError("the session has no tracks")


def route(name, passes=100):
    d = os.path.join(OUT, name)
    path = os.path.join(d, name + ".kicad_pcb")
    board = pcbnew.LoadBoard(path)
    dsn, ses = os.path.join(d, name + ".dsn"), os.path.join(d, name + ".ses")
    # Freerouting keeps only its track clearance from the outline; hand it an outline 0.35 mm smaller, so the copper
    # keeps JLCPCB's 0.3 mm from the real edge.
    edges = [s for s in board.GetDrawings() if s.GetLayer() == pcbnew.Edge_Cuts]
    saved = [(pcbnew.VECTOR2I(s.GetStart().x, s.GetStart().y), pcbnew.VECTOR2I(s.GetEnd().x, s.GetEnd().y))
             for s in edges]   # copies: GetStart() hands out the shape's own point
    cx, cy = mm((OUTLINE[0] + OUTLINE[2]) / 2 + OFFSET[0]), mm((OUTLINE[1] + OUTLINE[3]) / 2 + OFFSET[1])

    def inset(p):
        return pcbnew.VECTOR2I(p.x - mm(0.35) if p.x > cx else p.x + mm(0.35),
                               p.y - mm(0.35) if p.y > cy else p.y + mm(0.35))

    for s in edges:
        s.SetStart(inset(s.GetStart()))
        s.SetEnd(inset(s.GetEnd()))
    if not pcbnew.ExportSpecctraDSN(board, dsn):
        raise RuntimeError("DSN export failed")
    for s, (a, b) in zip(edges, saved):
        s.SetStart(a)
        s.SetEnd(b)
    # Freerouting 1.9 needs a display even in batch mode; -mt 0 skips its optimizer, which ran for over half an hour
    # on the main board after routing it in one minute.
    r = subprocess.run(["xvfb-run", "-a", "java", "-jar", FREEROUTING, "-de", dsn, "-do", ses,
                        "-mp", str(passes), "-dct", "0", "-mt", "0"], capture_output=True, text=True, timeout=1500)
    if not os.path.exists(ses):
        print(r.stdout[-3000:], r.stderr[-3000:])
        raise RuntimeError("Freerouting wrote no session")
    import_session(board, ses)
    os.remove(dsn)
    os.remove(ses)
    # Freerouting routed with a little more than the 0.2 mm the DRC holds it to, as it rounds to its grid. (Reloading
    # the project file would not do: pcbnew keeps the project it loaded first.)
    ds = board.GetDesignSettings()
    ns = ds.m_NetSettings
    ns.m_DefaultNetClass.SetClearance(mm(0.2))
    for key in list(ns.m_NetClasses.keys()):
        ns.m_NetClasses[key].SetClearance(mm(0.2))
    ds.m_CopperEdgeClearance = mm(0.3)

    # Ground on both layers over the whole board; the routed GND tracks stay as the backbone.
    gnd = board.FindNet("GND")
    x0, y0, x1, y1 = OUTLINE
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        zone = pcbnew.ZONE(board)
        zone.SetLayer(layer)
        zone.SetNet(gnd)
        outline = zone.Outline()
        outline.NewOutline()
        for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            outline.Append(mm(x + OFFSET[0]), mm(y + OFFSET[1]))
        zone.SetLocalClearance(mm(0.3))
        zone.SetMinThickness(mm(0.25))
        zone.SetThermalReliefGap(mm(0.4))
        zone.SetThermalReliefSpokeWidth(mm(0.4))
        zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        zone.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        add(board, zone)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(path, board)

    return drc(board, name)


def drc(board, name):
    """KiCad's DRC as a text report, from a fresh process that reads the project file as saved. Saving the board
    writes the project with pcbnew's defaults for the severities, so they go in again first."""
    path = board.GetFileName()
    pro = os.path.splitext(path)[0] + ".kicad_pro"
    data = json.load(open(pro))
    data.setdefault("board", {}).setdefault("design_settings", {}).setdefault("rule_severities", {}).update(SEVERITIES)
    json.dump(data, open(pro, "w"), indent=2)
    report = os.path.join(OUT, name, name + "-drc.txt")
    # A fresh process reads the project file's severities.
    subprocess.run([sys.executable, "-c", "import pcbnew, sys; b = pcbnew.LoadBoard(sys.argv[1]); "
                    "pcbnew.WriteDRCReport(b, sys.argv[2], pcbnew.EDA_UNITS_MILLIMETRES, True)", path, report],
                   check=True, capture_output=True)
    return report


def unconnected(report):
    for line in open(report):
        if line.startswith("** Found") and "unconnected" in line:
            return int(line.split()[2])
    return -1


def main():
    global GAP
    names = sys.argv[1:] or list(P.BOARDS)
    for name in names:
        # Freerouting leaves a net or two open on some placements; a slightly different spacing usually settles it.
        # The through-hole main board is full: only spacings up to about 1 mm leave room for every part.
        gaps = (0.9, 1.0, 0.95, 0.85, 1.05, 0.8, 0.75) if name.endswith("-tht") else \
            (0.9, 1.15, 1.0, 1.3, 1.1, 1.2, 1.4, 0.8)
        for GAP in gaps:
            try:
                build(name, P.BOARDS[name]())
            except RuntimeError as e:
                print(f"{name}: spacing {GAP} mm, {e}")
                continue
            set_netclasses(name, clearance=0.22)
            report = route(name)
            left = unconnected(report)
            print(f"{name}: spacing {GAP} mm, {left} unconnected")
            if left == 0:
                break
        print(open(report).read()[-1500:])


if __name__ == "__main__":
    main()
