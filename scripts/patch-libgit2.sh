#!/bin/sh
# Applies the patches in patches/libgit2/ to an unpacked libgit2 source tree.
#
# Each patch fixes one memory leak that valgrind finds when the test suite
# runs. The head of each patch file says which. The patches are written
# against libgit2 1.9.7 and apply to every release that the makefile
# supports. "patch" reports the offset when the lines differ.
#
# The makefile runs this script once, after it unpacks the tarball. A patch
# that is already applied is skipped, so a second run is safe. A patch that
# does not apply stops the build. The source is then not the source that the
# tests ran against.
#
# Usage:
#   scripts/patch-libgit2.sh <libgit2-source-directory>
set -eu

src=${1:?usage: patch-libgit2.sh <libgit2-source-directory>}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

die() { echo "patch-libgit2: $*" >&2; exit 1; }

command -v patch >/dev/null 2>&1 || die "patch is not installed"
[ -f "$src/CMakeLists.txt" ] || die "$src is not a libgit2 source tree"

for p in "$root"/patches/libgit2/*.patch; do
    [ -f "$p" ] || continue
    name=$(basename "$p")
    # --force stops patch from guessing that a patch is reversed. So the
    # reverse dry run succeeds only on a tree that has the patch.
    if (cd "$src" && patch -p1 -R --force --dry-run --silent < "$p") >/dev/null 2>&1; then
        echo "patch-libgit2: $name is already applied"
        continue
    fi
    (cd "$src" && patch -p1 --force --silent < "$p") || die "$name does not apply to $src"
    echo "patch-libgit2: applied $name"
done
