#!/usr/bin/env python3
# Copyright (c) 2026 Nicola Fiorillo
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""Adds what search engines and link previews need to a page of the site,
just after its <title>: canonical URL, Open Graph and Twitter card tags, and
optionally a new title. The manuals in docs/ stay untouched: publish.sh runs
this on the copies in site/public/.

    seo-head.py FILE URL TITLE DESCRIPTION
"""
import html, re, sys

path, url, title, desc = sys.argv[1:5]
s = open(path, encoding="utf-8").read()
s = re.sub(r'\s*<link rel="canonical"[^>]*>|\s*<meta (property|name)="(og|twitter):[^>]*>', "", s)
s = re.sub(r"<title>.*?</title>", "<title>%s</title>" % html.escape(title), s, count=1, flags=re.S)
e = lambda t: html.escape(t, quote=True)
tags = """
<link rel="canonical" href="{u}">
<meta property="og:type" content="website">
<meta property="og:site_name" content="EFI Partition Manager">
<meta property="og:title" content="{t}">
<meta property="og:description" content="{d}">
<meta property="og:url" content="{u}">
<meta property="og:image" content="https://efipm.nicfio.it/og.jpg">
<meta property="og:image:width" content="1200">
<meta property="og:image:height" content="630">
<meta name="twitter:card" content="summary_large_image">""".format(u=e(url), t=e(title), d=e(desc))
s = s.replace("</title>", "</title>" + tags, 1)
open(path, "w", encoding="utf-8").write(s)
