#!/usr/bin/env python3
# Copyright (c) 2026 Nicola Fiorillo
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Builds licence.html from LICENSE.md: a short summary in plain language, the
required notice, the full text of the PolyForm Noncommercial License 1.0.0 with
a link to its official source, and the earlier licences of the project (their
texts are published by publish.sh in licences/).
Usage: licence-page.py LICENSE.md > licence.html
Markdown understood: headings, paragraphs, numbered and bulleted lists,
**bold**, `code`, [links](url) and <https://autolinks>."""
import html, re, sys

# --- what changes from one project to the other
NAME = "EFI Partition Manager"
SITE = "https://efipm.nicfio.it"
MAIL = "efipm@nicfio.it"
REPO = "https://github.com/nic-fio/EFI_PARTITION_MANAGER"
CURRENT = "0.2.1"
# (versions, licence, file published in licences/)
EARLIER = [
    ("0.1.0 to 0.2.1", "Apache License 2.0 with the Commons Clause", "LICENSE-0.2.1-and-earlier.txt"),
]
OFFICIAL = "https://polyformproject.org/licenses/noncommercial/1.0.0"
NOTICE = "Required Notice: Copyright (c) 2026 Nicola Fiorillo (https://nicfio.it)"


def slug(t):
    return re.sub(r"[^a-z0-9]+", "-", t.lower()).strip("-")


def inline(t):
    t = html.escape(t, quote=False)
    t = re.sub(r"&lt;(https?://[^&\s]+)&gt;", r'<a href="\1">\1</a>', t)
    t = re.sub(r"\[(.+?)\]\((.+?)\)", r'<a href="\2">\1</a>', t)
    t = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", t)
    t = re.sub(r"`(.+?)`", r"<code>\1</code>", t)
    t = re.sub(r"(?<![\w:/])(" + re.escape(MAIL) + r")", r'<a href="mailto:\1">\1</a>', t)
    return t


def body(md, shift=0):
    out, para, lst = [], [], None

    def flush():
        nonlocal para, lst
        if para:
            out.append("<p>%s</p>" % inline(" ".join(para)))
            para = []
        if lst:
            tag, items = lst
            out.append("<%s>%s</%s>" % (tag, "".join("<li>%s</li>" % inline(i) for i in items), tag))
            lst = None

    for line in md.splitlines():
        s = line.strip()
        m = re.match(r"(#+)\s+(.*)", s)
        if s.startswith(">"):
            flush()
            out.append("<blockquote>%s</blockquote>" % inline(s.lstrip("> ")))
            continue
        if m:
            flush()
            n = min(len(m.group(1)) + shift, 6)
            out.append('<h%d id="%s">%s</h%d>' % (n, slug(m.group(2)), inline(m.group(2)), n))
        elif re.match(r"(\d+\.|-)\s+", s):
            if para:
                flush()
            tag = "ol" if s[0].isdigit() else "ul"
            if not lst or lst[0] != tag:
                flush()
                lst = (tag, [])
            lst[1].append(re.sub(r"^(\d+\.|-)\s+", "", s))
        elif s and lst and line.startswith("  "):
            lst[1][-1] += " " + s
        elif s:
            if lst:
                flush()
            para.append(s)
        else:
            flush()
    flush()
    return "\n".join(out)


def licence_page(md):
    e = html.escape
    earlier = "".join('<li>%s: %s — <a href="licences/%s">%s</a></li>' % (e(v), e(l), e(f), e(f))
                      for v, l, f in EARLIER)
    return """<h1>Licence</h1>
<section class="summary">
<p><strong>%(name)s is free for any noncommercial use</strong>: personal use, study, hobby projects, and use by
schools, charities and public bodies. For <strong>commercial use</strong>, including use at work inside a company,
write to <a href="mailto:%(mail)s">%(mail)s</a>.</p>
<p>The source code is on GitHub: <a href="%(repo)s">%(repo)s</a>.</p>
</section>
<p class="notice">%(notice)s</p>
<p>The licence of %(name)s is the <strong>PolyForm Noncommercial License 1.0.0</strong>, reproduced in full below,
unchanged. Official source: <a href="%(official)s">%(official)s</a>.</p>
<div class="text">
%(text)s
</div>
<h2 id="earlier-licences">Earlier licences</h2>
<p>The PolyForm Noncommercial License covers the current version, %(current)s, too. Whoever has a copy of
version %(current)s or of an earlier version may use it, at their choice, under the licence it was released with
or under the PolyForm Noncommercial License 1.0.0.</p>
<ul>%(earlier)s</ul>""" % dict(name=e(NAME), mail=MAIL, repo=REPO, notice=e(NOTICE), official=OFFICIAL,
                               text=body(md, shift=1), current=e(CURRENT), earlier=earlier)


def page(title, description, path, content):
    return """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%s</title>
<meta name="description" content="%s">
<link rel="canonical" href="%s/%s">
<style>
body{margin:0;background:#f4f6fa;color:#111827;font:16px/1.65 Cantarell,"Segoe UI",Roboto,Arial,sans-serif}
main{max-width:760px;margin:0 auto;padding:48px 24px 64px;background:#fff;box-shadow:0 1px 30px rgba(15,23,42,.08)}
nav{max-width:760px;margin:0 auto;padding:18px 24px;font-size:15px}
a{color:#0050c0;overflow-wrap:anywhere}
h1{font-size:32px;line-height:1.15;margin:0 0 18px;color:#0a2a6b}
h2{font-size:22px;margin:34px 0 10px;color:#0a2a6b}
h3{font-size:18px;margin:26px 0 8px;color:#0a2a6b}
ol,ul{padding-left:1.4em}li{margin:6px 0}
code{font-family:"DejaVu Sans Mono",Consolas,monospace;font-size:.88em;background:#eef2f7;padding:.08em .35em;border-radius:4px}
.summary{background:#eef4ff;border-left:4px solid #0050c0;border-radius:6px;padding:4px 18px;margin:0 0 22px}
blockquote{margin:10px 0;padding:6px 14px;border-left:3px solid #c9d3e0;color:#374151}
.notice{font-family:"DejaVu Sans Mono",Consolas,monospace;font-size:14px;background:#f6f7f9;padding:10px 14px;border-radius:6px}
.text{border-top:1px solid #e3e8ef;margin-top:22px}
@media (max-width:640px){main{padding:32px 16px 48px}nav{padding:14px 16px}h1{font-size:26px}}
@media print{body{background:#fff}main{box-shadow:none}nav{display:none}}
</style>
</head>
<body>
<nav><a href="./">&larr; %s</a></nav>
<main>
%s
</main>
</body>
</html>""" % (html.escape(title), html.escape(description), SITE, path, html.escape(NAME), content)


md = open(sys.argv[1]).read()
print(page("%s licence: PolyForm Noncommercial 1.0.0" % NAME,
           "The licence of %s: the PolyForm Noncommercial License 1.0.0, free for any noncommercial use." % NAME,
           "licence.html", licence_page(md)))
