#!/bin/sh
# Downloads one libgit2 source release and checks it against the digest that
# the makefile pins for it.
#
# libgit2 publishes its releases as tags on GitHub. GitHub generates the
# tarball for a tag on request. It does not keep a signed file for the tag. So
# the digests in the makefile were calculated from the tarballs that this
# binding was built and tested against. They pin those bytes. A mismatch means
# that the file behind the URL is not those bytes. That is a reason to stop,
# whether the cause is a bad download or a regenerated archive.
#
# The script moves the file into place only after the check. So the next make
# cannot use an interrupted or damaged download as a good one.
#
# Usage:
#   scripts/fetch-libgit2.sh <url> <sha256> <destination-file>
set -eu

url=${1:?usage: fetch-libgit2.sh <url> <sha256> <dest-file>}
want=${2:?usage: fetch-libgit2.sh <url> <sha256> <dest-file>}
dest=${3:?usage: fetch-libgit2.sh <url> <sha256> <dest-file>}

die() { echo "fetch-libgit2: $*" >&2; exit 1; }

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    elif command -v openssl >/dev/null 2>&1; then
        openssl dgst -sha256 -r "$1" | cut -d' ' -f1
    else
        die "no SHA-256 tool found: need sha256sum, shasum or openssl"
    fi
}

fetch() {
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$2" "$1"
    else
        die "neither curl nor wget is installed"
    fi
}

mkdir -p "$(dirname "$dest")"
tmp="$dest.incoming.$$"
trap 'rm -f "$tmp"' EXIT INT TERM

echo "fetch-libgit2: downloading $url"
fetch "$url" "$tmp" || die "download failed: $url"

got=$(sha256 "$tmp")
if [ "$got" != "$want" ]; then
    die "SHA-256 mismatch for $url
   expected $want
   actual   $got
   Nothing has been unpacked. Either the download was corrupted -- try
   again -- or the archive behind that URL is no longer the one this
   version was pinned to, which is worth understanding before building."
fi
echo "fetch-libgit2: SHA-256 $got verified"

mv "$tmp" "$dest"
echo "fetch-libgit2: $dest"
