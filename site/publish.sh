#!/usr/bin/env bash
# publish.sh - builds the site of EFI Partition Manager in site/public/ and
# publishes it to https://efipm.nicfio.it (the owner's VPS, user "progetti",
# /srv/www/efipm.nicfio.it).
#
#   site/publish.sh            build and publish
#   site/publish.sh --build    build only (look at site/public/ first)
#
# The site: the landing page (site/landing/index.html), the two manuals and
# their pictures from docs/, the licence as a page, and the downloads of the
# current release (download/: partmgr.efi, SHA256SUMS, the licence). The
# downloads come from the GitHub release while it is reachable (cached in
# build/), or from build/ with PARTMGR_FROM_BUILD=1.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/site/public
HOST=progetti@57.131.27.241
DEST=/srv/www/efipm.nicfio.it
VERSION=$(sed -n 's/^#define PARTMGR_VERSION "\(.*\)"/\1/p' "$ROOT/src/partmgr/partmgr.h")
URL=https://efipm.nicfio.it

rm -rf "$OUT"
mkdir -p "$OUT/download" "$OUT/assets"

cp "$ROOT/site/landing/index.html" "$OUT/index.html"
cp "$ROOT/docs/User Manual.html" "$ROOT/docs/Technical Manual.html" "$OUT/"
for f in $(grep -oh 'src="assets/[^"]*"' "$ROOT"/docs/*Manual.html | sort -u | sed 's/src="assets\/\(.*\)"/\1/'); do
    cp "$ROOT/docs/assets/$f" "$OUT/assets/"
done
python3 "$ROOT/site/licence-page.py" "$ROOT/LICENSE" > "$OUT/licence.html"

SEO=$ROOT/site/seo-head.py
python3 "$SEO" "$OUT/User Manual.html" "$URL/User%20Manual.html" \
    "EFI Partition Manager User Manual — partition disks from UEFI firmware" \
    "How to use EFI Partition Manager, a graphical GPT and MBR partition manager that runs in UEFI firmware: every screen and key, backups, wipe, damaged tables."
python3 "$SEO" "$OUT/Technical Manual.html" "$URL/Technical%20Manual.html" \
    "EFI Partition Manager Technical Manual — how a UEFI partition manager works" \
    "How EFI Partition Manager reads, changes and writes GPT and MBR tables in UEFI firmware, its backup format, and how it is tested."
python3 "$SEO" "$OUT/licence.html" "$URL/licence.html" \
    "EFI Partition Manager Freeware Licence" \
    "EFI Partition Manager is freeware: free to use, also at work, and to give away unchanged; not to sell, modify or include in commercial products."

# downloads of the current release (published with the release's own licence)
DL=$OUT/download
if [ "${PARTMGR_FROM_BUILD:-}" = 1 ]; then
    cp "$ROOT/build/partmgr.efi" "$DL/"
    sed 's/$/\r/' "$ROOT/LICENSE" > "$DL/LICENSE.txt"
    sed 's/$/\r/' "$ROOT/NOTICE.md" > "$DL/NOTICE.txt"
else
    CACHE=$ROOT/build/release-v$VERSION
    if [ ! -f "$CACHE/partmgr.efi" ]; then
        mkdir -p "$CACHE"
        gh release download "v$VERSION" --repo nic-fio/EFI_PARTITION_MANAGER --dir "$CACHE" --clobber
    fi
    (cd "$CACHE" && sha256sum -c SHA256SUMS)
    cp "$CACHE/partmgr.efi" "$DL/"
    sed 's/$/\r/' "$CACHE/LICENSE" > "$DL/LICENSE.txt"
    sed 's/$/\r/' "$CACHE/NOTICE.md" > "$DL/NOTICE.txt"
fi
(cd "$DL" && sha256sum partmgr.efi > SHA256SUMS)

cp "$ROOT/site/og.jpg" "$OUT/og.jpg"
cp "$ROOT"/site/google*.html "$OUT/" 2>/dev/null || true
cat > "$OUT/robots.txt" <<EOR
User-agent: *
Allow: /
Sitemap: $URL/sitemap.xml
EOR
TODAY=$(date +%F)
{
    echo '<?xml version="1.0" encoding="UTF-8"?>'
    echo '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">'
    for p in "" "User%20Manual.html" "Technical%20Manual.html" "licence.html"; do
        echo "  <url><loc>$URL/$p</loc><lastmod>$TODAY</lastmod></url>"
    done
    echo '</urlset>'
} > "$OUT/sitemap.xml"

echo "built $OUT (version $VERSION):"
(cd "$OUT" && find . -type f -printf '  %p  %s bytes\n' | sort)

[ "${1:-}" = --build ] && exit 0

# the server ends up identical to site/public (no rsync on either side)
tar -C "$OUT" -cf - --mode=u=rwX,go=rX . | ssh "$HOST" "find '$DEST' -mindepth 1 -delete && tar -C '$DEST' -xf - --no-same-owner"
echo "published to $URL"
