"""Writes both schematics and checks their netlists (kicad-cli) against parts.py."""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import parts as P
import schematic
from sexpr import find, first, parse

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, ".."))
LOCAL = {"tonwerk": os.path.join(OUT, "lib", "tonwerk.kicad_sym")}
TITLES = {"tonwerk-bedien": "Tonwerk Wavetable – Bedienplatine", "tonwerk-haupt": "Tonwerk Wavetable – Hauptplatine",
          "tonwerk-bedien-tht": "Tonwerk Wavetable – Bedienplatine (bedrahtet)",
          "tonwerk-haupt-tht": "Tonwerk Wavetable – Hauptplatine (bedrahtet)"}


def expected(parts):
    lib = schematic.Library(LOCAL)
    nets = {}
    for p in parts:
        pins = schematic.Library.pins(lib.symbol(p.symbol))
        for number, net in schematic.resolve_pins(p, pins).items():
            nets.setdefault(net, set()).add((p.ref, number))
    return nets


def from_kicad(netlist_path):
    doc = parse(open(netlist_path).read())[0]
    nets = {}
    for net in find(first(doc, "nets"), "net"):
        name = first(net, "name")[1].lstrip("/")
        nodes = {(first(n, "ref")[1], first(n, "pin")[1]) for n in find(net, "node")}
        nodes = {n for n in nodes if not n[0].startswith("#")}
        # Pins marked no-connect come out as single-pin nets named "unconnected-(...)".
        if nodes and not name.startswith("unconnected-"):
            nets[name] = nodes
    return nets


def main():
    uuids = {}
    ok = True
    # A new schematic gets new symbol uuids: rebuild only the boards named (all without arguments), and their boards.
    for name in sys.argv[1:] or list(P.BOARDS):
        make = P.BOARDS[name]
        d = os.path.join(OUT, name)
        os.makedirs(d, exist_ok=True)
        parts = make()
        sch = os.path.join(d, name + ".kicad_sch")
        uuids[name] = schematic.write(sch, name, parts, TITLES[name], LOCAL)
        net_file = os.path.join(d, name + ".net")
        subprocess.run(["kicad-cli", "sch", "export", "netlist", "-o", net_file, sch], check=True,
                       capture_output=True)
        want = {k: v for k, v in expected(parts).items()}
        got = from_kicad(net_file)
        # Compare as sets of pin groups: KiCad names unlabelled nets itself.
        want_groups = {frozenset(v) for v in want.values() if len(v) > 0}
        got_groups = {frozenset(v) for v in got.values()}
        missing = want_groups - got_groups
        extra = got_groups - want_groups
        print(f"{name}: {len(parts)} parts, {len(want)} nets expected, {len(got)} in KiCad's netlist")
        for g in list(missing)[:10]:
            print("  missing", sorted(g))
        for g in list(extra)[:10]:
            print("  extra  ", sorted(g))
        ok &= not missing and not extra
        os.remove(net_file)
    print("netlists match parts.py" if ok else "NETLIST MISMATCH")
    return uuids, ok


if __name__ == "__main__":
    sys.exit(0 if main()[1] else 1)
