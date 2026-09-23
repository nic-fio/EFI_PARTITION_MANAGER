#!/usr/bin/env python3
"""Keeps the technical manual in step with the sources: the line counts of its
source file map and of its area table are counted from the files themselves,
so that they cannot drift away from the tree.

  tools/gen-docs.py           rewrite the counts of docs/partmgr-developer-manual.html
  tools/gen-docs.py --check   fail if the manual is out of date, if a source file
                              has no row in the source map, or if the map lists a
                              file that does not exist (run by 'make test')
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MANUAL = ROOT / "docs" / "partmgr-developer-manual.html"


def lines_of(path):
    with open(path, "rb") as f:
        return sum(1 for _ in f)


def source_files():
    """The files the source map lists: headers, C sources, tools and tests."""
    files = set((ROOT / "include").glob("*.h"))
    files |= {p for p in (ROOT / "src").rglob("*") if p.suffix in (".c", ".h")}
    files |= {p for p in (ROOT / "tools").iterdir() if p.is_file()}
    files |= {p for p in (ROOT / "tests").rglob("*") if p.is_file() and "__pycache__" not in p.parts}
    return {str(p.relative_to(ROOT)) for p in files}


def dir_lines(names):
    """Lines of the files directly in the directories of an area row."""
    return sum(lines_of(p) for n in names for p in sorted((ROOT / n).iterdir()) if p.is_file())


# A row of the source map: a file in <code> followed by its count; a row of the
# area table: a second cell naming directories, then the count.
FILE_ROW = re.compile(r"(<tr><td><code>)([^<]+)(</code></td><td>)(\d+)(</td>)")
AREA_ROW = re.compile(r"(<tr><td>[^<]*</td><td>)((?:<code>[^<]+</code>(?:, )?)+)(</td><td>)([\d,]+)(</td>)")


def count_lines(text, problems):
    listed = []

    def file_row(m):
        path = m.group(2)
        listed.append(path)
        if not (ROOT / path).is_file():
            problems.append("%s: the source map lists '%s', which is not a file" % (MANUAL.name, path))
            return m.group(0)
        return m.group(1) + path + m.group(3) + str(lines_of(ROOT / path)) + m.group(5)

    def area_row(m):
        names = re.findall(r"<code>([^<]+)</code>", m.group(2))
        if not all((ROOT / n).is_dir() for n in names):
            return m.group(0)
        return m.group(1) + m.group(2) + m.group(3) + format(dir_lines(names), ",") + m.group(5)

    new = AREA_ROW.sub(area_row, FILE_ROW.sub(file_row, text))
    for f in sorted(source_files() - set(listed)):
        problems.append("%s: '%s' has no row in the source map" % (MANUAL.name, f))
    for f in sorted(set(listed)):
        if listed.count(f) > 1:
            problems.append("%s: the source map lists '%s' twice" % (MANUAL.name, f))
    return new


def main():
    problems = []
    text = MANUAL.read_text()
    new = count_lines(text, problems)
    if "--check" in sys.argv:
        if text != new:
            problems.append("%s is out of date: run 'make docs'" % MANUAL.relative_to(ROOT))
        if problems:
            print("\n".join(problems), file=sys.stderr)
            return 1
        print("docs: %d source files counted" % len(source_files()))
        return 0
    if text != new:
        MANUAL.write_text(new)
        print("updated %s" % MANUAL.relative_to(ROOT))
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
