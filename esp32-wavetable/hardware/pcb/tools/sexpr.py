"""A small reader and writer for KiCad's s-expressions."""

import re

_TOKEN = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))')


class Sym(str):
    """An unquoted atom, so writing keeps quoted strings quoted and atoms bare."""


def parse(text):
    stack, current = [], []
    pos = 0
    while True:
        m = _TOKEN.match(text, pos)
        if not m or m.end() == pos:
            break
        pos = m.end()
        if m.group(1):
            stack.append(current)
            current = []
        elif m.group(2):
            done = current
            current = stack.pop()
            current.append(done)
        elif m.group(3) is not None:
            current.append(m.group(3).replace('\\"', '"').replace("\\\\", "\\"))
        else:
            current.append(Sym(m.group(4)))
    return current


def dump(node, indent=0):
    if isinstance(node, list):
        if not node:
            return "()"
        simple = all(not isinstance(x, list) for x in node)
        if simple:
            return "(" + " ".join(dump(x) for x in node) + ")"
        out = "(" + dump(node[0])
        for x in node[1:]:
            if isinstance(x, list):
                out += "\n" + "  " * (indent + 1) + dump(x, indent + 1)
            else:
                out += " " + dump(x)
        return out + ")"
    if isinstance(node, Sym):
        return str(node)
    if isinstance(node, bool):
        return "yes" if node else "no"
    if isinstance(node, (int, float)):
        return ("%.4f" % node).rstrip("0").rstrip(".") if isinstance(node, float) else str(node)
    return '"' + str(node).replace("\\", "\\\\").replace('"', '\\"') + '"'


def find(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == key]


def first(node, key):
    found = find(node, key)
    return found[0] if found else None
