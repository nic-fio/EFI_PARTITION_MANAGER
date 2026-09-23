#!/bin/sh
# Prepare a freshly cloned working copy on a Debian or Ubuntu machine.
#
# A clone carries the sources, the tests, the documentation and the whole
# history, but not two things: the packages the build needs, and the git
# identity of this repository (it lives in .git/config, which is not
# cloned). This script reports both, and with --install installs the
# packages with apt.
#
#     tools/setup-dev.sh            # say what is missing
#     tools/setup-dev.sh --install  # install it (asks for sudo)
set -eu

# package : a command it provides : what it is for
PACKAGES="
build-essential:gcc:compiler and linker
python3:python3:elf2efi.py, the tests and the documentation tool
fdisk:sfdisk:builds the test disk images and is the reference of the tests
parted:parted:second reader of the tables partmgr writes
dosfstools:mkfs.fat:test disks with a file system and no table
qemu-system-x86:qemu-system-x86_64:make qemu-test
ovmf:-:UEFI firmware for QEMU (/usr/share/ovmf/OVMF.fd)
"

NAME=${PARTMGR_GIT_NAME:-nic-fio}
EMAIL=${PARTMGR_GIT_EMAIL:-315794250+nic-fio@users.noreply.github.com}

missing=
echo "Packages:"
while IFS=: read -r pkg cmd why; do
    [ -z "$pkg" ] && continue
    if [ "$cmd" = "-" ]; then
        # no command of its own: ask dpkg
        if dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "ok installed"; then
            ok=yes
        else
            ok=no
        fi
    elif command -v "$cmd" >/dev/null 2>&1 || [ -x "/sbin/$cmd" ] || [ -x "/usr/sbin/$cmd" ]; then
        ok=yes
    else
        ok=no
    fi
    [ "$ok" = no ] && missing="$missing $pkg"
    printf '  [%s] %-18s %s\n' "$([ "$ok" = yes ] && echo ' ok ' || echo MISS)" "$pkg" "$why"
done <<LIST
$PACKAGES
LIST

if [ -n "$missing" ]; then
    if [ "${1:-}" = "--install" ]; then
        # shellcheck disable=SC2086
        sudo apt-get install -y --no-install-recommends $missing
    else
        echo
        echo "  sudo apt-get install -y --no-install-recommends$missing"
    fi
else
    echo "  nothing missing"
fi

echo
echo "Git identity of this working copy:"
if git rev-parse --git-dir >/dev/null 2>&1; then
    if [ -z "$(git config --local --get user.email || true)" ]; then
        git config --local user.name "$NAME"
        git config --local user.email "$EMAIL"
        echo "  set to $NAME <$EMAIL>"
        echo "  (it keeps the personal address out of the public history;"
        echo "   PARTMGR_GIT_NAME and PARTMGR_GIT_EMAIL override it)"
    else
        echo "  already set: $(git config --local --get user.name) <$(git config --local --get user.email)>"
    fi
else
    echo "  not a git working copy, skipped"
fi

cat <<'EOF2'

Then check that everything really works:

    make            # build/partmgr.efi
    make test       # the partition table tests, the documentation check
    make qemu-test  # partmgr.efi inside QEMU/OVMF
EOF2
