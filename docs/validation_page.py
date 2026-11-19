import argparse
import html
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MARKDOWN = os.path.join(HERE, "VALIDATION.md")
SLUG_MAX = 48
PREFIX_MATCH = 30
FIRST_SECTION = '  <section class="card soft">'
DETAILS = '  <details class="card">'
STEP_RE = re.compile(r'  <section class="step"><div class="step-head"><span class="num">([^<]+)</span><h3>(.*?)</h3>'
                     r'<label class="done"><input type="checkbox" data-check="([^"]+)">Done</label></div>\n(.*?)\n  </section>\n', re.S)
SECTION_RE = re.compile(r"^### (\S+)\. (.*)$")
ITEM_RE = re.compile(r"^- \[ \] (.*)$")
SUB_RE = re.compile(r"^  - (.*)$")
ONLY_VERSION_RE = re.compile(r"^(.*?) \((0\.\d+\.\d+)\)$")
ANY_VERSION_RE = re.compile(r"0\.\d+\.\d+")
EYEBROW_RE = re.compile(r"(<div class=\"eyebrow\">ShotPuck rev 0B · firmware )[^ ]+( · on-device checklist</div>)")
FOOTER_RE = re.compile(r"<footer class=\"wrap\">Generated from docs/VALIDATION.md \([^)]*\) · [^<]*</footer>")


def bold(text):
    parts = html.escape(text, quote=True).split("**")
    return "".join(("<strong>" + p + "</strong>") if i % 2 else p for i, p in enumerate(parts))


def inline(text):
    out = []
    for i, part in enumerate(text.split("`")):
        out.append('<code class="f">' + html.escape(part, quote=True) + "</code>" if i % 2 else bold(part))
    return "".join(out)


def parse_markdown(md):
    sections, current, item = [], None, None
    in_checklist = False
    for line in md.splitlines():
        if line.startswith("## On-device checklist"):
            in_checklist = True
            continue
        m = SECTION_RE.match(line) if in_checklist else None
        if m:
            current = {"num": m.group(1), "title": m.group(2), "items": []}
            sections.append(current)
            continue
        m = ITEM_RE.match(line) if current is not None else None
        if m:
            item = {"text": m.group(1), "subs": []}
            current["items"].append(item)
            continue
        m = SUB_RE.match(line) if item is not None else None
        if m:
            item["subs"].append(m.group(1))
    return sections


def split_item(text):
    m = re.match(r"^\*\*(.+?)\*\*(.*)$", text)
    if not m:
        return None, text
    title, rest = m.group(1), m.group(2)
    title = title[:-1] if title.endswith(":") else title
    rest = (rest[1:] if rest.startswith(":") else rest).strip()
    return title, rest[:1].upper() + rest[1:]


def body_html(item):
    title, rest = split_item(item["text"])
    body = "    <p>" + inline(rest if title else item["text"]) + "</p>"
    if item["subs"]:
        body += "\n    <ul>" + "".join("<li>" + inline(s) + "</li>" for s in item["subs"]) + "</ul>"
    return body


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")[:SLUG_MAX]


def version_key(version):
    return tuple(int(x) for x in version.split("."))


def title_html(title):
    m = ONLY_VERSION_RE.match(title)
    if m:
        return inline(m.group(1)) + ' <span class="chip">' + m.group(2) + "</span>", slug(m.group(1))
    versions = ANY_VERSION_RE.findall(title)
    chip = ' <span class="chip">' + max(versions, key=version_key) + "</span>" if versions else ""
    return inline(title) + chip, slug(title)


def plain(body):
    return html.unescape(re.sub(r"<[^>]+>", "", body)).strip()


def old_steps(page):
    return [{"h3": m.group(2), "id": m.group(3), "body": m.group(4)} for m in STEP_RE.finditer(page)]


def step_head(section, item, old):
    title, _ = split_item(item["text"])
    if title:
        h3, sl = title_html(title)
        return h3, ("v" + section["num"] + "-" + sl).rstrip("-")
    body = body_html(item)
    match = next((st for st in old if st["body"] == body), None)
    match = match or next((st for st in old if plain(st["body"])[:PREFIX_MATCH] == plain(body)[:PREFIX_MATCH]), None)
    if match:
        return match["h3"], match["id"]
    short = item["text"].split(":")[0][:SLUG_MAX]
    return inline(short), ("v" + section["num"] + "-" + slug(short)).rstrip("-")


def table_rows(md):
    rows = []
    for line in md.split("## On-device checklist")[0].splitlines():
        if line.startswith("| ") and not line.startswith("| What |"):
            cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
            rows.append("<tr><td>" + inline(cells[0]) + "</td><td>" + inline(cells[-1]) + "</td></tr>")
    return "".join(rows)


def checklist_html(md, old):
    out = []
    for s in parse_markdown(md):
        out.append(FIRST_SECTION + "<h2>" + inline(s["num"] + ". " + s["title"]) + "</h2></section>\n")
        for k, item in enumerate(s["items"], 1):
            h3, sid = step_head(s, item, old)
            out.append('  <section class="step"><div class="step-head"><span class="num">' + s["num"] + "." + str(k) +
                       "</span><h3>" + h3 + '</h3><label class="done"><input type="checkbox" data-check="' + sid +
                       '">Done</label></div>\n' + body_html(item) + "\n  </section>\n")
    return "".join(out)


def build(page, args):
    md = open(args.validation, encoding="utf-8").read()
    head = EYEBROW_RE.sub(lambda m: m.group(1) + args.version + m.group(2), page[:page.index(FIRST_SECTION)])
    tail = page[page.index(DETAILS):]
    tail = re.sub(r"<tbody>.*?</tbody>", lambda m: "<tbody>" + table_rows(md) + "</tbody>", tail, count=1, flags=re.S)
    footer = '<footer class="wrap">Generated from docs/VALIDATION.md (' + args.source + ") · " + args.date + "</footer>"
    tail = FOOTER_RE.sub(lambda m: footer, tail)
    return head + checklist_html(md, old_steps(page)) + tail


def main():
    ap = argparse.ArgumentParser(description="Regenerate the NAS validation page from docs/VALIDATION.md")
    ap.add_argument("--page", required=True, help="the current validation.html (its step ids and titles are kept)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--version", required=True, help="firmware version for the page header, e.g. 0.3.13")
    ap.add_argument("--source", required=True, help="footer source, e.g. shot-tracker c545c8b")
    ap.add_argument("--date", required=True)
    ap.add_argument("--validation", default=DEFAULT_MARKDOWN)
    args = ap.parse_args()
    page = open(args.page, encoding="utf-8").read()
    open(args.out, "w", encoding="utf-8", newline="\n").write(build(page, args))


if __name__ == "__main__":
    main()
