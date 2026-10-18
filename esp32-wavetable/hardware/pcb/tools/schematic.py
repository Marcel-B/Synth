"""Writes a KiCad 7 schematic for a board from parts.py: the library's symbols, a net label on every connected pin,
a no-connect mark on every free one. Connections by label rather than wires keep the generated sheet readable and
its netlist exactly the one in parts.py (build.py checks that through kicad-cli)."""

import uuid

from sexpr import Sym, dump, find, first, parse

KICAD_SYMBOLS = "/usr/share/kicad/symbols"
POWER_NETS = {"GND": "power:GND", "+5V": "power:+5V", "+3V3": "power:+3V3", "+12V": "power:+12V",
              "-12V": "power:-12V"}


def uid():
    return str(uuid.uuid4())


class Library:
    """Symbols from KiCad's libraries (and the project's own), flattened the way a schematic stores them."""

    def __init__(self, local_libs):
        self.files = {}
        self.local = local_libs

    def _lib(self, name):
        if name not in self.files:
            path = self.local.get(name, f"{KICAD_SYMBOLS}/{name}.kicad_sym")
            self.files[name] = {s[1]: s for s in find(parse(open(path).read())[0], "symbol")}
        return self.files[name]

    def symbol(self, lib_id):
        lib, name = lib_id.split(":")
        symbols = self._lib(lib)
        sym = symbols[name]
        parent = first(sym, "extends")
        if parent is None:
            flat = [x for x in sym]
        else:
            # A derived symbol: the parent's drawing and pins under the child's name and properties.
            base = symbols[parent[1]]
            props = {p[1]: p for p in find(sym, "property")}
            flat = [Sym("symbol"), name]
            for x in base[2:]:
                if isinstance(x, list) and x[0] == "property":
                    flat.append(props.pop(x[1], x))
                elif isinstance(x, list) and x[0] == "symbol":
                    flat.append([x[0], x[1].replace(parent[1], name, 1)] + x[2:])
                else:
                    flat.append(x)
            flat += list(props.values())
        flat = [flat[0], lib_id] + flat[2:]
        return flat

    @staticmethod
    def pins(sym):
        """(unit, number, name, x, y, angle, hidden) of every pin of body style 1 (unit 0 = common to all)."""
        out = []
        for sub in find(sym, "symbol"):
            unit, style = (int(v) for v in sub[1].rsplit("_", 2)[-2:])
            if style not in (0, 1):
                continue
            for pin in find(sub, "pin"):
                at = first(pin, "at")
                hidden = any(x == "hide" for x in pin)
                out.append((unit, first(pin, "number")[1], first(pin, "name")[1], float(at[1]), float(at[2]),
                            float(at[3]) if len(at) > 3 else 0.0, hidden))
        return out

    @staticmethod
    def units(sym):
        units = {int(s[1].rsplit("_", 2)[-2]) for s in find(sym, "symbol")}
        units.discard(0)
        return sorted(units) or [1]


def resolve_pins(part, pins):
    """part.pins keyed by number or name -> {pin number: net}."""
    by_number = {}
    for key, net in part.pins.items():
        hits = [p for p in pins if p[1] == key] or [p for p in pins if p[2] == key]
        if not hits:
            raise KeyError(f"{part.ref}: no pin {key}")
        for p in hits:
            by_number[p[1]] = net
    return by_number


def bbox(pins):
    xs = [p[3] for p in pins] or [0]
    ys = [p[4] for p in pins] or [0]
    return min(xs) - 5, max(xs) + 5, min(ys) - 5, max(ys) + 5


def grid(v, step=2.54):
    return round(v / step) * step


def write(path, project, parts, title, local_libs):
    lib = Library(local_libs)
    root = uid()
    used = {}
    instances, labels, no_connects, flags = [], [], [], []

    # Lay the units out in rows on an A1 sheet, in the order of parts.py, so related parts sit together.
    x0, y0, x, y, row_height, width = 25.4, 30.48, 25.4, 30.48, 0.0, 800.0
    power_symbols = {}

    def place_power(net, px, py):
        lib_id = POWER_NETS[net]
        used.setdefault(lib_id, lib.symbol(lib_id))
        n = power_symbols.setdefault(net, 0) + 1
        power_symbols[net] = n
        ref = "#PWR%03d" % (len(instances) + 1)
        angle = 180 if net in ("GND", "-12V") and False else 0
        instances.append([Sym("symbol"), [Sym("lib_id"), lib_id], [Sym("at"), px, py, angle], [Sym("unit"), 1],
                          [Sym("in_bom"), Sym("no")], [Sym("on_board"), Sym("yes")], [Sym("dnp"), Sym("no")],
                          [Sym("uuid"), uid()],
                          [Sym("property"), "Reference", ref, [Sym("at"), px, py + 5, 0],
                           [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]], Sym("hide")]],
                          [Sym("property"), "Value", net, [Sym("at"), px, py - 3.5, 0],
                           [Sym("effects"), [Sym("font"), [Sym("size"), 1.0, 1.0]]]],
                          [Sym("pin"), "1", [Sym("uuid"), uid()]],
                          [Sym("instances"), [Sym("project"), project,
                                              [Sym("path"), "/" + root, [Sym("reference"), ref], [Sym("unit"), 1]]]]])

    symbol_uuids = {}
    for part in parts:
        sym = used.setdefault(part.symbol, lib.symbol(part.symbol))
        all_pins = Library.pins(sym)
        nets = resolve_pins(part, all_pins)
        for unit in Library.units(sym):
            pins = [p for p in all_pins if p[0] in (0, unit)]
            if not pins:
                continue
            x_min, x_max, y_min, y_max = bbox(pins)
            w, h = x_max - x_min + 22, y_max - y_min + 10
            if x + w > x0 + width:
                x, y = x0, y + row_height + 7.62
                row_height = 0.0
            sx, sy = grid(x - x_min + 11), grid(y + y_max + 3)
            u = uid()
            symbol_uuids.setdefault(part.ref, u)
            props = [("Reference", part.ref, False), ("Value", part.value, False),
                     ("Footprint", part.footprint, True), ("Datasheet", "~", True)]
            if part.lcsc:
                props.append(("LCSC", part.lcsc, True))
            if part.note:
                props.append(("Hinweis", part.note, True))
            node = [Sym("symbol"), [Sym("lib_id"), part.symbol], [Sym("at"), sx, sy, 0], [Sym("unit"), unit],
                    [Sym("in_bom"), Sym("yes")], [Sym("on_board"), Sym("yes")], [Sym("dnp"), Sym("no")],
                    [Sym("uuid"), u]]
            for i, (name, value, hide) in enumerate(props):
                effects = [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]], [Sym("justify"), Sym("left")]]
                if hide:
                    effects.append(Sym("hide"))
                node.append([Sym("property"), name, value, [Sym("at"), sx + x_max + 1, sy - y_max + 1 + 1.8 * i, 0],
                             effects])
            for p in pins:
                node.append([Sym("pin"), p[1], [Sym("uuid"), uid()]])
            node.append([Sym("instances"), [Sym("project"), project, [Sym("path"), "/" + root,
                                                                        [Sym("reference"), part.ref],
                                                                        [Sym("unit"), unit]]]])
            instances.append(node)
            for (_, number, _name, px, py, angle, hidden) in pins:
                ex, ey = sx + px, sy - py
                net = nets.get(number)
                if net is None:
                    if not hidden:
                        no_connects.append([Sym("no_connect"), [Sym("at"), ex, ey], [Sym("uuid"), uid()]])
                    continue
                if net in POWER_NETS:
                    place_power(net, ex, ey)
                    continue
                # The label points away from the symbol's body.
                la = (angle + 180) % 360
                justify = Sym("left") if la in (0, 90) else Sym("right")
                labels.append([Sym("label"), net, [Sym("at"), ex, ey, la % 180], [Sym("fields_autoplaced")],
                               [Sym("effects"), [Sym("font"), [Sym("size"), 1.0, 1.0]],
                                [Sym("justify"), justify, Sym("bottom")]], [Sym("uuid"), uid()]])
            x += w
            row_height = max(row_height, h)

    # PWR_FLAGs: the nets the connectors or the Eurorack header feed, so the ERC knows they are driven.
    flag_nets = sorted({n for p in parts for n in p.pins.values() if n in POWER_NETS})
    used.setdefault("power:PWR_FLAG", lib.symbol("power:PWR_FLAG"))
    fx, fy = x0, y + row_height + 25
    for i, net in enumerate(flag_nets):
        px = fx + 20 * i
        instances.append([Sym("symbol"), [Sym("lib_id"), "power:PWR_FLAG"], [Sym("at"), px, fy, 0],
                          [Sym("unit"), 1], [Sym("in_bom"), Sym("no")], [Sym("on_board"), Sym("yes")],
                          [Sym("dnp"), Sym("no")], [Sym("uuid"), uid()],
                          [Sym("property"), "Reference", "#FLG%02d" % (i + 1), [Sym("at"), px, fy - 5, 0],
                           [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]], Sym("hide")]],
                          [Sym("property"), "Value", "PWR_FLAG", [Sym("at"), px, fy - 4, 0],
                           [Sym("effects"), [Sym("font"), [Sym("size"), 1.0, 1.0]]]],
                          [Sym("pin"), "1", [Sym("uuid"), uid()]],
                          [Sym("instances"), [Sym("project"), project, [Sym("path"), "/" + root,
                                                                          [Sym("reference"), "#FLG%02d" % (i + 1)],
                                                                          [Sym("unit"), 1]]]]])
        place_power(net, px, fy)

    doc = [Sym("kicad_sch"), [Sym("version"), Sym("20230121")], [Sym("generator"), Sym("eeschema")],
           [Sym("uuid"), root], [Sym("paper"), "A1"],
           [Sym("title_block"), [Sym("title"), title], [Sym("date"), "2026-10-10"], [Sym("rev"), "0.1"],
            [Sym("company"), "Tonwerk"],
            [Sym("comment"), 1, "Erzeugt von hardware/pcb/tools/build.py aus parts.py. Verbindungen über Netznamen."]],
           [Sym("lib_symbols")] + list(used.values())]
    doc += instances + labels + no_connects + flags
    doc.append([Sym("sheet_instances"), [Sym("path"), "/", [Sym("page"), "1"]]])
    with open(path, "w") as f:
        f.write(dump(doc) + "\n")
    return symbol_uuids
