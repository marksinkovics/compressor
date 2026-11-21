#!/usr/bin/env python3
"""Measure image coders on corpus/pictures: lossless sizes, and lossy sizes at equal quality.

    python3 tools/images.py [--lossless] [--lossy] [--classes NAME...] [--coders NAME...]
                            [--targets N...] [--jobs N] [--corpus PATH] [--app PATH] [--no-save]

Lossless: PNG at maximum effort (ImageMagick), WebP lossless (cwebp -z 9), JPEG XL lossless
(cjxl -d 0), ingot's general levels on the raw pixels (PAM), and `ingot image` once it exists.
Every round trip is checked against the original pixels.

Lossy: JPEG (ImageMagick), WebP (cwebp -m 6), AVIF (ffmpeg with SVT-AV1) and JPEG XL (cjxl) are
each run over a range of quality settings; every result is decoded and scored with ssimulacra2
against the original, and the bits per pixel at each target score are interpolated between the
two settings around it (linear in score against log size). Only opaque images are coded lossy.
Lossy WebP and SVT-AV1 code chroma at half resolution (4:2:0) only, which caps their scores on
photos and screens below 90.

Sizes are bits per pixel (bpp); times are CPU seconds of the child processes, one thread each.
Results go to docs/benchmarks/images/ as JSON named after the date and commit. The corpus comes from
`tools/fetch-corpus.sh pictures`; `--corpus` takes any folder with the same class subfolders. The
exit status is 1 when a lossless round trip changed the pixels.
"""

import argparse
import concurrent.futures
import datetime
import json
import math
import os
import resource
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CORPUS = REPO / "corpus" / "pictures"
HISTORY = REPO / "docs" / "benchmarks" / "images"
CLASSES = ["photo", "screen", "graphic", "alpha"]
TARGETS = [70.0, 80.0, 90.0]

# Quality settings per lossy coder, from smallest to largest output.
SWEEPS = {
    "jpeg": [8, 20, 35, 50, 65, 75, 85, 92, 97, 100],
    "webp": [2, 10, 20, 35, 50, 65, 75, 85, 92, 97, 100],
    "avif": [63, 58, 52, 46, 40, 34, 28, 22, 16, 8, 2],
    "jxl": [25.0, 15.0, 10.0, 7.0, 5.0, 3.5, 2.5, 1.8, 1.2, 0.7, 0.35, 0.15],
}


def cpu():
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def run(command):
    """Runs a command, returning its CPU seconds; raises on failure."""
    start = cpu()
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return cpu() - start


def fill(command, **paths):
    """`command` with each {name} replaced by its path."""
    filled = []
    for part in command:
        for name, value in paths.items():
            part = part.replace("{" + name + "}", str(value))
        filled.append(part)
    return filled


def pixels(path, work, alpha):
    """The image's raw pixels as PAM bytes, for comparing round trips."""
    pam = work / (path.stem + ".cmp.pam")
    channels = "RGBA" if alpha else "RGB"
    run(["magick", str(path), "-depth", "8", "-colorspace", "sRGB",
         "-alpha", "on" if alpha else "off", f"{channels.lower()}:{pam}"])
    data = pam.read_bytes()
    pam.unlink()
    return data


def identify(path):
    out = subprocess.run(["magick", "identify", "-format", "%w %h %[channels]", str(path)],
                         check=True, capture_output=True, text=True).stdout.split()
    return int(out[0]), int(out[1]), out[2].startswith("srgba")


def lossless_coders(app):
    """Name -> (extension, encode command, decode command); {i} and {o} are paths. Decoders write
    an image ImageMagick can read back."""
    coders = {
        "png": (".png",
                ["magick", "{i}", "-strip", "-define", "png:compression-level=9",
                 "-define", "png:compression-filter=5", "-define", "png:compression-strategy=1",
                 "{o}"],
                ["magick", "{i}", "{o}.png"]),
        "webp": (".webp", ["cwebp", "-quiet", "-lossless", "-z", "9", "-mt", "-exact", "{i}",
                           "-o", "{o}"],
                 ["dwebp", "-quiet", "{i}", "-o", "{o}.png"]),
        "jxl": (".jxl", ["cjxl", "--quiet", "-d", "0", "-e", "7", "--num_threads=0", "{i}", "{o}"],
                ["djxl", "--quiet", "--num_threads=0", "{i}", "{o}.png"]),
    }
    if app:
        coders["ingot image"] = (
            ".ingoti",
            [app, "image", "encode", "-T", "1", "-i", "{pam}", "-o", "{o}"],
            [app, "image", "decode", "-T", "1", "-i", "{i}", "-o", "{o}.pam"])
        for level in (6, 9):
            coders[f"ingot -L {level}"] = (
                ".ingot",
                [app, "encode", "-L", str(level), "-T", "1", "-i", "{pam}", "-o", "{o}"],
                [app, "decode", "-T", "1", "-i", "{i}", "-o", "{o}.pam"])
    return coders


def selected(name, coders):
    return not coders or any(name.startswith(prefix) for prefix in coders)


def lossless(path, app, work, coders):
    width, height, alpha = identify(path)
    original = pixels(path, work, alpha)
    pam = work / (path.stem + ".pam")
    run(["magick", str(path), "-depth", "8", "-alpha", "on" if alpha else "off", str(pam)])
    results = {}
    for name, (extension, encode, decode) in lossless_coders(app).items():
        if not selected(name, coders):
            continue
        out = work / (path.stem + "." + name.replace(" ", "_") + extension)
        encode_seconds = run(fill(encode, i=path, o=out, pam=pam))
        decode_seconds = run(fill(decode, i=out, o=out))
        decoded = Path(str(out) + (".pam" if name.startswith("ingot") else ".png"))
        same = pixels(decoded, work, alpha) == original
        results[name] = {
            "bpp": 8 * out.stat().st_size / (width * height),
            "encode_s": encode_seconds,
            "decode_s": decode_seconds,
            "lossless": same,
        }
        out.unlink()
        decoded.unlink()
    pam.unlink()
    return {"width": width, "height": height, "alpha": alpha, "lossless": results}


def lossy_commands(coder, setting, source, out, decoded):
    if coder == "jpeg":
        return (["magick", source, "-strip", "-quality", str(setting), out],
                ["magick", out, decoded])
    if coder == "webp":
        return (["cwebp", "-quiet", "-m", "6", "-q", str(setting), source, "-o", out],
                ["dwebp", "-quiet", out, "-o", decoded])
    if coder == "avif":
        return (["ffmpeg", "-loglevel", "error", "-y", "-i", source, "-c:v", "libsvtav1",
                 "-crf", str(setting), "-preset", "6", "-pix_fmt", "yuv420p",
                 "-svtav1-params", "lp=1", "-frames:v", "1", out],
                ["ffmpeg", "-loglevel", "error", "-y", "-i", out, decoded])
    return (["cjxl", "--quiet", "-d", str(setting), "-e", "7", "--num_threads=0", source, out],
            ["djxl", "--quiet", "--num_threads=0", out, decoded])


def score(original, decoded):
    out = subprocess.run(["ssimulacra2", original, decoded], check=True, capture_output=True,
                         text=True).stdout
    return float(out.split()[-1])


def at_target(points, target):
    """bpp at `target` score from (score, bpp) points, or None outside their range."""
    points = sorted(points)
    for (s0, b0), (s1, b1) in zip(points, points[1:]):
        if s0 <= target <= s1 and s1 > s0:
            t = (target - s0) / (s1 - s0)
            return math.exp(math.log(b0) + t * (math.log(b1) - math.log(b0)))
    return None


def lossy(path, targets, work, coders):
    width, height, alpha = identify(path)
    if alpha:
        return None
    source = work / (path.stem + ".src.png")
    run(["magick", str(path), "-alpha", "off", "-depth", "8", str(source)])
    results = {}
    extensions = {"jpeg": ".jpg", "webp": ".webp", "avif": ".avif", "jxl": ".jxl"}
    for coder, settings in SWEEPS.items():
        if not selected(coder, coders):
            continue
        points = []
        encode_total = decode_total = 0.0
        for setting in settings:
            out = work / f"{path.stem}.{coder}{extensions[coder]}"
            decoded = work / f"{path.stem}.{coder}.dec.png"
            encode, decode = lossy_commands(coder, setting, str(source), str(out), str(decoded))
            encode_total += run(encode)
            decode_total += run(decode)
            points.append((score(str(source), str(decoded)),
                           8 * out.stat().st_size / (width * height)))
            out.unlink()
            decoded.unlink()
        results[coder] = {
            "points": points,
            "at": {str(t): at_target(points, t) for t in targets},
            "encode_s": encode_total / len(settings),
            "decode_s": decode_total / len(settings),
        }
    source.unlink()
    return results


def measure(path, app, do_lossless, do_lossy, targets, coders):
    with tempfile.TemporaryDirectory(prefix="images-") as directory:
        work = Path(directory)
        record = {"class": path.parent.name, "name": path.name}
        if do_lossless:
            record.update(lossless(path, app, work, coders))
        if do_lossy:
            record["lossy"] = lossy(path, targets, work, coders)
        return record


def mean(values):
    """None unless every value is known, so a mean never mixes different images."""
    values = list(values)
    if not values or any(v is None for v in values):
        return None
    return sum(values) / len(values)


def print_summary(records, targets):
    by_class = {}
    for record in records:
        by_class.setdefault(record["class"], []).append(record)
    if any("lossless" in r for r in records):
        names = list(next(r for r in records if "lossless" in r)["lossless"])
        print("Lossless, mean bpp per class (encode / decode CPU seconds per image)")
        print(f"{'class':<9}{'n':>4}" + "".join(f"{n:>22}" for n in names))
        for cls, items in by_class.items():
            cells = []
            for name in names:
                bpp = mean([r["lossless"][name]["bpp"] for r in items])
                enc = mean([r["lossless"][name]["encode_s"] for r in items])
                dec = mean([r["lossless"][name]["decode_s"] for r in items])
                cells.append(f"{bpp:6.3f} ({enc:5.2f}/{dec:5.2f})")
            print(f"{cls:<9}{len(items):>4}" + "".join(f"{c:>22}" for c in cells))
        bad = [(r["name"], n) for r in records for n, v in r.get("lossless", {}).items()
               if not v["lossless"]]
        if bad:
            print("NOT LOSSLESS:", bad)
    lossy_records = [r for r in records if r.get("lossy")]
    if lossy_records:
        coders = list(next(r for r in lossy_records)["lossy"])
        print("\nLossy, mean bpp at each SSIMULACRA2 target ('-': not reached on every image)")
        print(f"{'class':<9}{'target':>7}" + "".join(f"{c:>9}" for c in coders))
        for cls, items in by_class.items():
            items = [r for r in items if r.get("lossy")]
            if not items:
                continue
            for target in targets:
                cells = []
                for coder in coders:
                    value = mean([r["lossy"][coder]["at"][str(target)] for r in items])
                    cells.append(f"{value:9.3f}" if value is not None else f"{'-':>9}")
                print(f"{cls:<9}{target:>7.0f}" + "".join(cells))


def git(*args):
    return subprocess.run(["git", "-C", str(REPO), *args], capture_output=True,
                          text=True).stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--lossless", action="store_true", help="only the lossless part")
    parser.add_argument("--lossy", action="store_true", help="only the lossy part")
    parser.add_argument("--classes", nargs="+", default=CLASSES, choices=CLASSES)
    parser.add_argument("--coders", nargs="+", default=[],
                        help="only coders whose names start with these, e.g. ingot jxl")
    parser.add_argument("--targets", nargs="+", type=float, default=TARGETS)
    parser.add_argument("--corpus", type=Path, default=CORPUS, help="default corpus/pictures")
    parser.add_argument("--jobs", type=int, default=6, help="images measured at once (default 6)")
    parser.add_argument("--app", default=str(REPO / "build" / "release" / "ingot" / "ingot"))
    parser.add_argument("--no-save", action="store_true", help="print only, keep no history file")
    args = parser.parse_args()
    do_lossless = args.lossless or not args.lossy
    do_lossy = args.lossy or not args.lossless

    tools = {"png": [], "jpeg": [], "webp": ["cwebp", "dwebp"], "jxl": ["cjxl", "djxl"],
             "avif": ["ffmpeg"]}
    needed = {"magick"} | ({"ssimulacra2"} if do_lossy else set())
    for name, required in tools.items():
        if selected(name, args.coders):
            needed.update(required)
    missing = sorted(tool for tool in needed if shutil.which(tool) is None)
    if missing:
        sys.exit(f"missing tools: {', '.join(missing)}")
    if not args.corpus.is_dir():
        sys.exit(f"no {args.corpus}: run tools/fetch-corpus.sh pictures")
    app = args.app if Path(args.app).exists() else None
    paths = sorted(p for cls in args.classes for p in (args.corpus / cls).glob("*.png"))

    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
        records = list(pool.map(measure, paths, [app] * len(paths), [do_lossless] * len(paths),
                                [do_lossy] * len(paths), [args.targets] * len(paths),
                                [args.coders] * len(paths)))
    print_summary(records, args.targets)

    if not args.no_save:
        HISTORY.mkdir(parents=True, exist_ok=True)
        stamp = datetime.date.today().isoformat()
        commit = git("rev-parse", "--short", "HEAD") or "unknown"
        path = HISTORY / f"{stamp}-{commit}.json"
        path.write_text(json.dumps({"date": stamp, "commit": commit, "targets": args.targets,
                                    "records": records}, indent=1) + "\n")
        print(f"\nSaved {path.relative_to(REPO)}")
    if any(not v["lossless"] for r in records for v in r.get("lossless", {}).values()):
        sys.exit(1)


if __name__ == "__main__":
    main()
