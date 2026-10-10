#!/bin/sh
# Copyright (c) 2026 Nicola Fiorillo
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Pack the whole project into one file: a git bundle with every branch, every
# tag and the complete history. It restores without a network and without
# GitHub:
#
#     tools/backup.sh ~/partmgr-backup       # writes partmgr-YYYY-MM-DD.bundle
#     git clone partmgr-2026-09-30.bundle EFI_PARTITION_MANAGER
#     cd EFI_PARTITION_MANAGER && tools/setup-dev.sh --install && make test
#
# Keep a copy off this machine: a bundle on the same disk protects against a
# wrong rm, not against a dead disk.
set -eu

DEST=${1:-.}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$DEST/partmgr-$(date +%F).bundle"

[ -d "$DEST" ] || mkdir -p "$DEST"
git -C "$ROOT" bundle create "$OUT" --all
git -C "$ROOT" bundle verify "$OUT" >/dev/null

if [ -n "$(git -C "$ROOT" status --porcelain)" ]; then
    echo "warning: uncommitted changes are NOT in the bundle:"
    git -C "$ROOT" status --short
fi
if [ -n "$(git -C "$ROOT" log --oneline '@{upstream}..HEAD' 2>/dev/null)" ]; then
    echo "note: the bundle has commits that are not on the remote yet."
fi

printf '%s: %s, %s commits, tags: %s\n' "$OUT" "$(du -h "$OUT" | cut -f1)" \
    "$(git -C "$ROOT" rev-list --count HEAD)" \
    "$(git -C "$ROOT" tag | tr '\n' ' ')"
