"""Manufacturing files for JLCPCB: Gerbers and drills as one zip per board, BOM and CPL of the SMD parts (assembly on
the side they sit on), a list of the parts to solder by hand, PNG previews. A board without SMD parts (the through-hole
variant) gets no BOM and CPL, only a list of its parts by value to solder them from."""

import csv
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(__file__))
import parts as P
from render import render

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, ".."))
LAYERS = "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"


def cli(*args):
    subprocess.run(["kicad-cli", *args], check=True, capture_output=True)


def is_smd(part):
    return any(s in part.footprint for s in ("_SMD:", "Package_SO:", "Package_TO_SOT_SMD:", "RF_Module:",
                                             "Connector_USB:"))


def export(name):
    d = os.path.join(OUT, name)
    pcb = os.path.join(d, name + ".kicad_pcb")
    fab = os.path.join(d, "fertigung")
    shutil.rmtree(fab, ignore_errors=True)
    os.makedirs(fab)

    with tempfile.TemporaryDirectory() as tmp:
        g = os.path.join(tmp, "gerber")
        os.makedirs(g)
        cli("pcb", "export", "gerbers", "--layers", LAYERS, "--subtract-soldermask", "-o", g + "/", pcb)
        cli("pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm", "--excellon-separate-th",
            "--generate-map", "--map-format", "gerberx2", "-o", g + "/", pcb)
        shutil.make_archive(os.path.join(fab, name + "-gerber"), "zip", g)

        pos = os.path.join(tmp, "pos.csv")
        cli("pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both", "-o", pos, pcb)
        positions = {row["Ref"]: row for row in csv.DictReader(open(pos))}

    parts = P.BOARDS[name]()
    smd = [p for p in parts if is_smd(p) and not p.hand]
    hand = [p for p in parts if not is_smd(p) or p.hand]
    if not smd:
        solder_list(name, fab, parts)
        render(pcb, "F.Cu,B.Cu,Edge.Cuts,F.SilkS", os.path.join(d, name + "-vorn.png"))
        render(pcb, "B.Cu,Edge.Cuts,B.SilkS,B.Fab", os.path.join(d, name + "-hinten.png"), mirror=True)
        # Assembly drawings: only print and outlines, readable at a glance while soldering.
        render(pcb, "Edge.Cuts,F.SilkS,F.Fab", os.path.join(d, name + "-bestueckung-vorn.png"))
        render(pcb, "Edge.Cuts,B.SilkS,B.Fab", os.path.join(d, name + "-bestueckung-hinten.png"), mirror=True)
        print(name, len(parts), "parts, all through-hole")
        return

    # BOM: one line per value and footprint, as JLCPCB's assembly upload wants it.
    lines = {}
    for p in smd:
        key = (p.value, p.footprint.split(":")[1], p.lcsc)
        lines.setdefault(key, []).append(p.ref)
    with open(os.path.join(fab, name + "-bom-jlcpcb.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (value, footprint, lcsc), refs in sorted(lines.items(), key=lambda kv: kv[1][0]):
            w.writerow([value, ",".join(refs), footprint, lcsc])

    # JLCPCB's 3D models do not all share KiCad's zero orientation; corrections seen in their placement preview.
    turn = {"U4": 180.0,   # ESP32-S3-WROOM-1: antenna pointed away from the board edge (2026-10-11)
            "J2": 180.0}   # USB-C: contacts at the board edge, opening inward (2026-10-11)
    # JLCPCB's WROOM sits on the centre of its pads, not of the module: 3.77 mm toward the bottom pad row (pads from
    # -5.71 to 13.25 mm), which matches what their 3D preview showed; mm toward the board's bottom edge.
    # The USB-C model sat about 1 mm out over the board edge (Marcel, from their 2D view).
    shift = {"U4": 3.77, "J2": -1.0}
    # SOIC, TSSOP and SOT-23-5 models lie across their pads (2026-10-11); a quarter turn puts pin 1 on pad 1.
    for p in parts:
        if ("Package_SO:" in p.footprint or "SOT-23-5" in p.footprint) and p.side == "B":
            turn[p.ref] = 90.0
        # SMA diodes: the model's cathode band sat on the anode pad (2026-10-11).
        if "D_SMA" in p.footprint and p.side == "B":
            turn[p.ref] = 180.0
    with open(os.path.join(fab, name + "-cpl-jlcpcb.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for p in smd:
            row = positions[p.ref]
            dy = shift.get(p.ref, 0.0)
            w.writerow([p.ref, row["PosX"] + "mm", "%.4fmm" % (float(row["PosY"]) - dy),
                        "Top" if row["Side"] == "top" else "Bottom",
                        "%.1f" % ((float(row["Rot"]) + turn.get(p.ref, 0.0)) % 360)])

    with open(os.path.join(fab, name + "-handbestueckung.csv"), "w", newline="") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["Bauteil", "Wert", "Gehäuse", "Seite", "Hinweis"])
        for p in hand:
            w.writerow([p.ref, p.value, p.footprint.split(":")[1], "vorn" if p.side == "F" else "hinten", p.note])

    render(pcb, "F.Cu,B.Cu,Edge.Cuts,F.SilkS", os.path.join(d, name + "-vorn.png"))
    render(pcb, "B.Cu,Edge.Cuts,B.SilkS,B.Fab", os.path.join(d, name + "-hinten.png"), mirror=True)
    print(name, len(smd), "SMD parts,", len(hand), "by hand")


def solder_list(name, fab, parts):
    """One line per value and package, in the order to solder: low parts first, sockets and tall parts last."""
    def kind(p):
        fp = p.footprint.split(":")[1]
        order = [("R_Axial", 0, "Widerstand"), ("D_DO-41", 1, "Diode"), ("C_Disc", 2, "Keramikkondensator"),
                 ("TO-92", 3, ""), ("DIP-", 4, "IC-Fassung + IC"), ("PinHeader_1x03", 5, "Lötpunkte"),
                 ("CP_Radial", 6, "Elko"), ("PinSocket", 7, "Buchsenleiste"), ("PinHeader", 7, "Stiftleiste"),
                 ("tonwerk", 7, "Modul auf Buchsenleiste"), ("R-78E", 8, "Schaltregler"),
                 ("IDC", 9, "Wannenstecker"), ("Potentiometer", 10, "Poti"), ("Jack_3.5mm", 10, "Klinkenbuchse"),
                 ("SW_PUSH", 10, "Taster"), ("LED_D5.0mm", 10, "RGB-LED")]
        for key, rank, label in order:
            if key in fp or key in p.footprint:
                return rank, label
        return 11, ""

    lines = {}
    for p in parts:
        rank, label = kind(p)
        lines.setdefault((rank, label, p.value, p.footprint.split(":")[1]), []).append(p)

    def ref_key(ref):
        return (ref.rstrip("0123456789"), int(ref[len(ref.rstrip("0123456789")):] or 0))

    with open(os.path.join(fab, name + "-bestueckliste.csv"), "w", newline="") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["Anzahl", "Bauteil", "Wert", "Bauteile", "Gehäuse", "Seite", "Hinweis"])
        for (rank, label, value, footprint), ps in sorted(lines.items(), key=lambda kv: (kv[0][0], kv[0][2])):
            refs = sorted((p.ref for p in ps), key=ref_key)
            # Notes that differ from part to part name their part.
            distinct = {p.note for p in ps if p.note}
            if len(ps) > 1 and len(distinct) > 0 and (len(distinct) > 1 or any(not p.note for p in ps)):
                notes = ["%s: %s" % (p.ref, p.note) for p in sorted(ps, key=lambda p: ref_key(p.ref)) if p.note]
            else:
                notes = sorted(distinct)
            w.writerow([len(ps), label, value, " ".join(refs), footprint,
                        "vorn" if ps[0].side == "F" else "hinten", "; ".join(notes)])


if __name__ == "__main__":
    for n in sys.argv[1:] or list(P.BOARDS):
        export(n)
