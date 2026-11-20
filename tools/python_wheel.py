#!/usr/bin/env python3
"""Pack the built ingot module into a wheel, with the standard library only.

    python3 tools/python_wheel.py --module MODULE.so --python PYTHON --version VERSION --out DIR

PYTHON is the interpreter the module was built for; its version gives the wheel's Python and ABI
tags (cp312-cp312). The platform tag comes from it too, except on macOS, where it is the module's
own minimum OS version (otool's LC_BUILD_VERSION minos), which is what the module needs. Prints the
wheel's path.
"""

import argparse
import base64
import hashlib
import pathlib
import re
import subprocess
import sys
import zipfile

SUMMARY = "ingot: block-based compression, folder archives and search, from C++"


def target_tags(python):
    script = (
        "import sys, sysconfig; "
        "print(f'cp{sys.version_info[0]}{sys.version_info[1]}'); "
        "print(sysconfig.get_platform()); "
        "print(f'{sys.version_info[0]}.{sys.version_info[1]}')"
    )
    out = subprocess.run([python, "-I", "-c", script], capture_output=True, text=True, check=True)
    tag, platform, version = out.stdout.split()
    return tag, platform, version


def macos_minimum(module):
    out = subprocess.run(["otool", "-l", str(module)], capture_output=True, text=True, check=False)
    match = re.search(r"LC_BUILD_VERSION.*?minos (\d+)\.(\d+)", out.stdout, re.S)
    return (int(match.group(1)), int(match.group(2))) if match else None


def platform_tag(platform, module):
    if platform.startswith("macosx-"):
        arch = platform.split("-")[-1]
        minimum = macos_minimum(module)
        major, minor = minimum if minimum else map(int, platform.split("-")[1].split("."))
        # macOS 11 and later count only major versions for wheels.
        return f"macosx_{major}_{0 if major >= 11 else minor}_{arch}"
    return re.sub(r"[-.]", "_", platform)


def record_line(name, data):
    digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()
    return f"{name},sha256={digest},{len(data)}"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--module", required=True, type=pathlib.Path)
    parser.add_argument("--python", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()

    tag, platform, python_version = target_tags(args.python)
    platform = platform_tag(platform, args.module)
    name = f"ingot-{args.version}"
    dist_info = f"{name}.dist-info"
    files = {
        args.module.name: args.module.read_bytes(),
        f"{dist_info}/METADATA": (
            "Metadata-Version: 2.1\n"
            "Name: ingot\n"
            f"Version: {args.version}\n"
            f"Summary: {SUMMARY}\n"
            f"Requires-Python: =={python_version}.*\n"
        ).encode(),
        f"{dist_info}/WHEEL": (
            "Wheel-Version: 1.0\n"
            "Generator: ingot tools/python_wheel.py\n"
            "Root-Is-Purelib: false\n"
            f"Tag: {tag}-{tag}-{platform}\n"
        ).encode(),
    }
    record = [record_line(path, data) for path, data in files.items()]
    record.append(f"{dist_info}/RECORD,,")
    files[f"{dist_info}/RECORD"] = ("\n".join(record) + "\n").encode()

    args.out.mkdir(parents=True, exist_ok=True)
    wheel = args.out / f"{name}-{tag}-{tag}-{platform}.whl"
    with zipfile.ZipFile(wheel, "w", zipfile.ZIP_DEFLATED) as archive:
        for path, data in files.items():
            info = zipfile.ZipInfo(path, date_time=(1980, 1, 1, 0, 0, 0))
            info.external_attr = (0o755 if path.endswith((".so", ".pyd")) else 0o644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    print(wheel)
    return 0


if __name__ == "__main__":
    sys.exit(main())
