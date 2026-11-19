#!/bin/bash
# Download the benchmark corpora into corpus/ (git-ignored).
#
#   tools/fetch-corpus.sh            Canterbury and Silesia
#   tools/fetch-corpus.sh <name>...  only the named ones (canterbury, silesia)
#
# A corpus whose directory already exists is skipped; delete the directory to fetch it again.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CORPUS_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)/corpus"

CANTERBURY_URL=https://corpus.canterbury.ac.nz/resources/cantrbry.tar.gz
CANTERBURY_SHA256=f140e8a5b73d3f53198555a63bfb827889394a42f20825df33c810c3d5e3f8fb
SILESIA_URL=https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip
SILESIA_SHA256=0626e25f45c0ffb5dc801f13b7c82a3b75743ba07e3a71835a41e3d9f63c77af

fail() {
    printf 'ERROR: %s\n' "$1" >&2
    exit 1
}

sha256() {
    if command -v sha256sum > /dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

# fetch <name> <url> <sha256>
fetch() {
    local name="$1" url="$2" expected="$3"
    local target="$CORPUS_DIR/$name"
    if [[ -d "$target" ]]; then
        printf '%s: already in %s\n' "$name" "$target"
        return
    fi

    local work
    work="$(mktemp -d "$CORPUS_DIR/.fetch-$name.XXXXXX")"
    # shellcheck disable=SC2064 # expand $work now, while it is set
    trap "rm -rf '$work'" RETURN EXIT

    local archive="$work/${url##*/}"
    printf '%s: downloading %s\n' "$name" "$url"
    curl -fsSL --retry 3 -o "$archive" "$url"

    local actual
    actual="$(sha256 "$archive")"
    [[ "$actual" == "$expected" ]] || fail "$name: SHA-256 mismatch (expected $expected, got $actual)"

    mkdir "$work/files"
    case "$archive" in
        *.tar.gz) tar -xzf "$archive" -C "$work/files" ;;
        *.zip) unzip -q "$archive" -d "$work/files" ;;
        *) fail "$name: unknown archive type: $archive" ;;
    esac
    # Canterbury's files are read-only in the archive.
    chmod -R u+rw "$work/files"
    mv "$work/files" "$target"
    printf '%s: %s files in %s\n' "$name" "$(find "$target" -type f | wc -l | tr -d ' ')" "$target"
}

main() {
    local names=("$@")
    [[ ${#names[@]} -gt 0 ]] || names=(canterbury silesia)
    mkdir -p "$CORPUS_DIR"
    for name in "${names[@]}"; do
        case "$name" in
            canterbury) fetch canterbury "$CANTERBURY_URL" "$CANTERBURY_SHA256" ;;
            silesia) fetch silesia "$SILESIA_URL" "$SILESIA_SHA256" ;;
            *) fail "unknown corpus: $name (known: canterbury, silesia)" ;;
        esac
    done
}

main "$@"
