#!/bin/bash
# Builds the Windows release zip: upstream's 0.2.0 package (its launcher and the launcher's DLLs, unchanged) with this
# fork's game exe, documents and an empty Game folder. No game files go in.
#
#   packaging/windows/make-zip.sh <upstream testdrive3-enhanced-0.2.0-win64.zip> <testdrive3-enhanced.exe> <out dir>
#
# The upstream zip: gh release download v0.2.0 -R kylofon/testdrive3-enhanced
# (sha256 a497ad29aa86167078bf6a39571fae9568a1bf29184c51c6a440519742f6ea99).
set -euo pipefail

UP=$1 EXE=$2 OUT=$3
VERSION=${VERSION:-0.2.0-og10}
SRC=$(cd "$(dirname "$0")/../.." && pwd)
NAME=TestDrive3-Super-Enhanced-$VERSION-win64
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "a497ad29aa86167078bf6a39571fae9568a1bf29184c51c6a440519742f6ea99  $UP" | sha256sum -c --quiet

# Upstream's zip names its entries with backslashes; read it with Python, which takes them as written.
python -I - "$UP" "$WORK/up" <<'EOF'
import sys, zipfile, os
z = zipfile.ZipFile(sys.argv[1])
for i in z.infolist():
    name = i.filename.replace("\\", "/")
    if name.endswith("/"):
        continue
    to = os.path.join(sys.argv[2], name)
    os.makedirs(os.path.dirname(to), exist_ok=True)
    with open(to, "wb") as f:
        f.write(z.read(i))
EOF
D=$WORK/$NAME
mkdir -p "$D/Game"
# Upstream's launcher and DLLs; its game exe and documents are replaced by the fork's.
for f in "$WORK"/up/*/*; do
    case ${f##*/} in
        testdrive3-enhanced.exe|README.md|CHANGELOG.md|"HOW TO RUN.txt") ;;
        *) cp "$f" "$D/" ;;
    esac
done
cp "$EXE" "$D/testdrive3-enhanced.exe"
for f in README.md CHANGELOG.md LICENSE CHASE.md FOOT.md WEAPON.md REVERSE.md FOES.md CIVS.md POLICE.md TOWNS.md \
         ANIMALS.md; do
    cp "$SRC/$f" "$D/"
done
cp "$SRC/packaging/windows/HOW TO RUN.txt" "$D/"
cp "$SRC/packaging/windows/PUT YOUR GAME FILES HERE.txt" "$D/Game/"

# Nothing of the original game may be in the package.
if find "$D" -iname 'tdiii.exe' -o -iname 'td3.exe' -o -iname 'playdisk.dat' -o -iname 'scene*.dat' | grep -q .; then
    echo "game files found in the package" >&2
    exit 1
fi

mkdir -p "$OUT"
rm -f "$OUT/$NAME.zip"
# Forward slashes in the entry names, so the zip unpacks the same everywhere.
python -I - "$WORK" "$NAME" "$OUT/$NAME.zip" <<'EOF'
import sys, zipfile, os
root, name, out = sys.argv[1:]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for d, dirs, files in os.walk(os.path.join(root, name)):
        dirs.sort()
        rel = os.path.relpath(d, root).replace(os.sep, "/")
        z.write(d, rel + "/")
        for f in sorted(files):
            z.write(os.path.join(d, f), rel + "/" + f)
EOF
sha256sum "$OUT/$NAME.zip"
unzip -l "$OUT/$NAME.zip" | tail -n 3
