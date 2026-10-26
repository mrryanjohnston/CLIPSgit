#!/bin/sh
# Builds and tests against every libgit2 release that the makefile pins, one
# after the other. Each release is fetched, built, and installed in its own
# directory in vendor/. A release that is already built there is reused.
#
# Usage:
#   scripts/test-versions.sh
#   LIBGIT2_VERSIONS="1.8.7 1.9.7" scripts/test-versions.sh
set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 1

versions=${LIBGIT2_VERSIONS:-$(make -s print-libgit2-versions)}

failed=
for v in $versions; do
    echo
    echo "=== libgit2 $v ==="
    if ! make --no-print-directory LIBGIT2_VERSION="$v"; then
        failed="$failed $v"
        continue
    fi
    if ! make --no-print-directory LIBGIT2_VERSION="$v" test; then
        failed="$failed $v"
    fi
done

echo
if [ -n "$failed" ]; then
    echo "FAILED:$failed"
    exit 1
fi
echo "PASSED: $versions"
