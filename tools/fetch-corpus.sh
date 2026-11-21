#!/bin/bash
# Download the benchmark corpora into corpus/ (git-ignored).
#
#   tools/fetch-corpus.sh            Canterbury and Silesia
#   tools/fetch-corpus.sh <name>...  only the named ones (canterbury, silesia, images, pictures)
#
# images (macOS only) isn't downloaded: sips converts some of the system's desktop pictures and an
# app icon into uncompressed BMP files, so the bytes depend on the macOS version.
#
# pictures (macOS, ImageMagick) is the image track's PNG corpus in four classes: photo/ (the 24 Kodak
# images, downloaded, and a large desktop picture), screen/ and graphic/ (drawn by ImageMagick, the
# screens from this repository's text at a fixed commit) and alpha/ (images with transparency).
#
# A corpus whose directory already exists is skipped; delete the directory to fetch it again.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CORPUS_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)/corpus"

CANTERBURY_URL=https://corpus.canterbury.ac.nz/resources/cantrbry.tar.gz
CANTERBURY_SHA256=f140e8a5b73d3f53198555a63bfb827889394a42f20825df33c810c3d5e3f8fb
SILESIA_URL=https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip
SILESIA_SHA256=0626e25f45c0ffb5dc801f13b7c82a3b75743ba07e3a71835a41e3d9f63c77af
KODAK_URL=https://r0k.us/graphics/kodak/kodak
# Of kodim01.png to kodim24.png concatenated.
KODAK_SHA256=eddba72a8e8880f5a3a78b2742867a5896ac01af822d38460ae3df8c36419072
# The commit whose text the screen pictures show.
PICTURES_TEXT_COMMIT=0d29f52

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

# Uncompressed images for the 2-D transforms: a photo, a gradient, an abstract picture (24-bit, odd
# widths so rows are padded) and an icon with alpha.
make_images() {
    local target="$CORPUS_DIR/images"
    if [[ -d "$target" ]]; then
        printf 'images: already in %s\n' "$target"
        return
    fi
    command -v sips > /dev/null 2>&1 || fail "images: needs macOS's sips"
    local pictures="/System/Library/Desktop Pictures"
    local work
    work="$(mktemp -d "$CORPUS_DIR/.fetch-images.XXXXXX")"
    # shellcheck disable=SC2064 # expand $work now, while it is set
    trap "rm -rf '$work'" RETURN EXIT
    sips -Z 1999 -s format bmp "$pictures/Sonoma.heic" --out "$work/photo.bmp" > /dev/null
    sips -Z 1501 -s format bmp "$pictures/Radial Sky Blue.heic" --out "$work/gradient.bmp" > /dev/null
    sips -Z 1777 -s format bmp "$pictures/iMac Blue.heic" --out "$work/abstract.bmp" > /dev/null
    sips -Z 1024 -s format tiff "/Applications/Safari.app/Contents/Resources/AppIcon.icns" \
        --out "$work/icon.tiff" > /dev/null
    mv "$work" "$target"
    trap - RETURN EXIT
    printf 'images: %s files in %s\n' "$(find "$target" -type f | wc -l | tr -d ' ')" "$target"
}

# Draws the screen-like and graphic pictures into $1 (screen/, graphic/) with ImageMagick.
draw_pictures() {
    local work="$1" repo
    repo="$(cd -- "$SCRIPT_DIR/.." && pwd)"
    local code="$work/code.txt" doc="$work/doc.txt"
    git -C "$repo" show "$PICTURES_TEXT_COMMIT:compressor/src/compressor/chunk/chunk.cpp" | head -60 > "$code"
    git -C "$repo" show "$PICTURES_TEXT_COMMIT:docs/fskit.md" | fold -s -w 110 | head -48 > "$doc"

    # An editor: a file tree, line numbers and code in a dark theme.
    seq 1 60 > "$work/numbers.txt"
    magick -size 1920x1080 xc:'#1e1f22' \
        -fill '#2b2d30' -draw 'rectangle 0,0 279,1079' -fill '#3c3f41' -draw 'rectangle 0,1050 1919,1079' \
        -font /System/Library/Fonts/Menlo.ttc -pointsize 14 -fill '#a9b7c6' \
        -annotate +20+30 $'compressor\n  include\n  src\n    chunk\n      chunk.cpp\n    data\n    engine\n    longrange\n  test\ndocs\ntools' \
        -fill '#606366' -annotate +290+30 "@$work/numbers.txt" \
        -fill '#bcbec4' -annotate +340+30 "@$code" \
        -fill '#dfe1e5' -annotate +20+1070 'chunk.cpp  UTF-8  LF  C++  Ln 42, Col 17' \
        "$work/screen/code.png"

    # A document page: a header bar, a navigation column, body text.
    magick -size 1600x1200 xc:white \
        -fill '#24292f' -draw 'rectangle 0,0 1599,59' -fill '#f6f8fa' -draw 'rectangle 0,60 299,1199' \
        -font Helvetica -pointsize 18 -fill white -annotate +24+38 'Compressor / docs / fskit.md' \
        -pointsize 15 -fill '#0969da' \
        -annotate +24+100 $'Overview\nWhat was checked\nWhat an implementation needs\nOpen questions\nRoadmap\nFile format\nBenchmarks' \
        -fill '#1f2328' -pointsize 15 -annotate +330+100 "@$doc" \
        "$work/screen/document.png"

    # An application window: toolbar buttons, a table with striped rows, a side panel.
    local args=(-size 1440x900 'xc:#e8ebef'
        -fill '#d1d5db' -draw 'rectangle 0,0 1439,47'
        -fill '#ff5f57' -draw 'circle 24,24 30,24' -fill '#febc2e' -draw 'circle 46,24 52,24'
        -fill '#28c840' -draw 'circle 68,24 74,24'
        -fill '#0a84ff' -draw 'roundrectangle 1200,64 1300,96 8,8'
        -fill '#ffffff' -draw 'roundrectangle 1310,64 1420,96 8,8'
        -fill '#f8f9fb' -draw 'rectangle 0,48 199,899'
        -font Helvetica -pointsize 14 -fill '#24292f'
        -annotate +20+90 $'Archives\nRecent\nShared\nTrash'
        -fill white -annotate +1222+86 'Encode' -fill '#24292f' -annotate +1330+86 'Decode')
    local row top stripe
    for row in $(seq 0 19); do
        top=$((120 + row * 32))
        stripe='#ffffff'
        [[ $((row % 2)) -eq 1 ]] && stripe='#f2f5f9'
        args+=(-fill "$stripe" -draw "rectangle 220,$top 1180,$((top + 31))"
            -fill '#24292f' -annotate "+236+$((top + 21))" "file_$((row * 7 % 23)).ingot"
            -annotate "+620+$((top + 21))" "$(((row * 7919) % 99991)) bytes"
            -annotate "+900+$((top + 21))" "2026-11-$((row % 28 + 1))")
    done
    magick "${args[@]}" "$work/screen/interface.png"

    # A bar chart with flat colours and labels.
    args=(-size 1200x800 xc:white -stroke none
        -fill '#333333' -draw 'rectangle 118,80 120,700' -draw 'rectangle 118,700 1150,702'
        -font Helvetica -pointsize 18 -annotate +400+50 'Compressed size by level' -pointsize 14)
    local i height colour
    for i in $(seq 0 9); do
        height=$(((i * 37 + 11) % 97 * 6 + 40))
        colour="$(printf '#%02x%02x%02x' $((40 + i * 20)) $((90 + i * 12)) $((200 - i * 15)))"
        args+=(-fill "$colour" -draw "rectangle $((140 + i * 100)),$((700 - height)) $((210 + i * 100)),700"
            -fill '#333333' -annotate "+$((150 + i * 100))+730" "L$((i + 1))")
    done
    magick "${args[@]}" "$work/graphic/chart.png"

    # Flat shapes over a gradient band.
    magick -size 1000x1000 xc:'#fdfdfd' \
        \( -size 1000x200 gradient:'#ff7e5f-#feb47b' \) -geometry +0+400 -composite \
        -fill '#2a9d8f' -draw 'circle 250,250 250,100' -fill '#e9c46a' -draw 'polygon 600,100 900,100 750,380' \
        -fill '#264653' -draw 'roundrectangle 120,680 480,900 30,30' \
        -fill '#e76f51' -draw 'ellipse 740,780 180,110 0,360' \
        "$work/graphic/shapes.png"
}

# The image track's PNG corpus (see the top of this file).
make_pictures() {
    local target="$CORPUS_DIR/pictures"
    if [[ -d "$target" ]]; then
        printf 'pictures: already in %s\n' "$target"
        return
    fi
    command -v sips > /dev/null 2>&1 || fail "pictures: needs macOS's sips"
    command -v magick > /dev/null 2>&1 || fail "pictures: needs ImageMagick's magick"
    local desktop="/System/Library/Desktop Pictures"
    local work
    work="$(mktemp -d "$CORPUS_DIR/.fetch-pictures.XXXXXX")"
    # shellcheck disable=SC2064 # expand $work now, while it is set
    trap "rm -rf '$work'" RETURN EXIT
    mkdir "$work/photo" "$work/screen" "$work/graphic" "$work/alpha"

    printf 'pictures: downloading the Kodak images from %s\n' "$KODAK_URL"
    local i
    for i in $(seq -w 1 24); do
        curl -fsSL --retry 3 -o "$work/photo/kodim$i.png" "$KODAK_URL/kodim$i.png"
    done
    local actual
    actual="$(cat "$work"/photo/kodim*.png | shasum -a 256 | cut -d' ' -f1)"
    [[ "$actual" == "$KODAK_SHA256" ]] || fail "pictures: Kodak SHA-256 mismatch (expected $KODAK_SHA256, got $actual)"

    # The one large desktop picture that ships as an image (a rendered landscape).
    sips -Z 2048 -s format png "$desktop/Sonoma.heic" --out "$work/photo/sonoma.png" > /dev/null
    sips -Z 1501 -s format png "$desktop/Radial Sky Blue.heic" --out "$work/graphic/gradient.png" > /dev/null
    sips -Z 1024 -s format png "/Applications/Safari.app/Contents/Resources/AppIcon.icns" \
        --out "$work/alpha/icon.png" > /dev/null
    magick -size 900x300 xc:none -font Helvetica-Bold -pointsize 96 \
        -fill '#00000080' -annotate +34+194 'ingot' -fill '#1f6feb' -annotate +24+184 'ingot' \
        -channel A -blur 0x1 +channel "$work/alpha/label.png"
    draw_pictures "$work"
    rm -f "$work"/*.txt
    # 8 bits per channel, no metadata, alpha only in alpha/.
    local file
    for file in "$work"/photo/*.png "$work"/screen/*.png "$work"/graphic/*.png; do
        magick "$file" -strip -alpha off -depth 8 "PNG24:$file"
    done
    for file in "$work"/alpha/*.png; do
        magick "$file" -strip -depth 8 "PNG32:$file"
    done

    mv "$work" "$target"
    trap - RETURN EXIT
    printf 'pictures: %s files in %s\n' "$(find "$target" -type f | wc -l | tr -d ' ')" "$target"
}

main() {
    local names=("$@")
    [[ ${#names[@]} -gt 0 ]] || names=(canterbury silesia)
    mkdir -p "$CORPUS_DIR"
    for name in "${names[@]}"; do
        case "$name" in
            canterbury) fetch canterbury "$CANTERBURY_URL" "$CANTERBURY_SHA256" ;;
            silesia) fetch silesia "$SILESIA_URL" "$SILESIA_SHA256" ;;
            images) make_images ;;
            pictures) make_pictures ;;
            *) fail "unknown corpus: $name (known: canterbury, silesia, images, pictures)" ;;
        esac
    done
}

main "$@"
