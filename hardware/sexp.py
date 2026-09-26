"""Minimal S-expression reader/writer for KiCad files."""
import re

_tok = re.compile(r'\s*(?:(\()|(\))|("(?:[^"\\]|\\.)*")|([^\s()"]+))')


class Str(str):
    """Quoted string atom."""


def parse(text):
    stack, cur = [], []
    pos = 0
    while True:
        m = _tok.match(text, pos)
        if not m:
            break
        pos = m.end()
        o, c, s, a = m.groups()
        if o:
            stack.append(cur)
            cur = []
        elif c:
            done = cur
            cur = stack.pop()
            cur.append(done)
        elif s is not None:
            cur.append(Str(s[1:-1].replace('\\n', '\n').replace('\\"', '"').replace('\\\\', '\\')))
        elif a is not None:
            cur.append(a)
    return cur


def dump(x, ind=0):
    if isinstance(x, list):
        if not x:
            return "()"
        if all(not isinstance(i, list) for i in x):
            return "(" + " ".join(dump(i, ind) for i in x) + ")"
        s = "(" + dump(x[0], ind)
        for i in x[1:]:
            s += "\n" + "  " * (ind + 1) + dump(i, ind + 1)
        return s + ")"
    if isinstance(x, Str):
        return '"' + x.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
    if isinstance(x, float):
        return ("%.4f" % x).rstrip("0").rstrip(".")
    return str(x)


def find(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def find1(node, key):
    r = find(node, key)
    return r[0] if r else None
