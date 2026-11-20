"""Tests for the ingot Python module, run by ctest (Python.Module) without pytest.

    python3 -I python/test/test_ingot.py build/python/python/package
"""

import os
import pathlib
import sys
import tempfile

sys.path.insert(0, sys.argv[1] if len(sys.argv) > 1 else ".")
import ingot  # noqa: E402


def record(i):
    return (
        '{"name": "port%d", "version": "1.%d", "description": "A library.", '
        '"dependencies": [{"name": "vcpkg-cmake", "host": true}]}\n' % (i, i % 7)
    ).encode()


def test_round_trip_every_level():
    data = b"ingot compresses bytes. " * 2000 + bytes(range(256)) * 20
    for level in range(1, 10):
        packed = ingot.compress(data, level=level, threads=2)
        assert packed[:4] == b"\x89ING", level
        # Level 1 is Huffman only; from 2 on, LZ77 finds the repeats.
        assert len(packed) < (len(data) * 7 // 10 if level == 1 else len(data) // 4), level
        assert ingot.decompress(packed) == data, level
    assert ingot.decompress(ingot.compress(b"")) == b""


def test_errors_raise_ingot_error():
    for call in (
        lambda: ingot.decompress(b"not an ingot file"),
        lambda: ingot.decompress(ingot.compress(b"abc" * 100)[:-3]),
        lambda: ingot.compress(b"x", level=0),
        lambda: ingot.decompress(b"x", dictionary=b""),
    ):
        try:
            call()
        except ingot.Error:
            continue
        raise AssertionError("no ingot.Error")


def test_dictionary_shrinks_small_inputs():
    dictionary = ingot.train_dictionary([record(i) for i in range(300)], size=2048)
    assert 0 < len(dictionary) <= 2048
    sample = record(1000)
    plain = ingot.compress(sample)
    primed = ingot.compress(sample, dictionary=dictionary)
    assert len(primed) * 2 < len(plain)
    assert ingot.decompress(primed, dictionary=dictionary) == sample
    try:
        ingot.decompress(primed)
    except ingot.Error as error:
        assert "dictionary" in str(error)
    else:
        raise AssertionError("decoded without the dictionary")


def test_files_and_folders():
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "folder" / "sub").mkdir(parents=True)
        (root / "folder" / "a.txt").write_bytes(b"alpha\n" * 1000)
        (root / "folder" / "sub" / "b.bin").write_bytes(bytes(range(256)) * 10)
        ingot.compress_file(root / "folder" / "a.txt", root / "a.ingot", level=9)
        ingot.decompress_file(str(root / "a.ingot"), str(root / "a.out"))
        assert (root / "a.out").read_bytes() == b"alpha\n" * 1000
        ingot.compress_file(root / "folder", root / "folder.ingot", threads=1)
        ingot.decompress_file(root / "folder.ingot", root / "copy")
        assert (root / "copy" / "sub" / "b.bin").read_bytes() == bytes(range(256)) * 10
        try:
            ingot.decompress_file(root / "missing.ingot", root / "x")
        except ingot.Error:
            pass
        else:
            raise AssertionError("no ingot.Error for a missing file")


def main():
    assert ingot.__version__
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_")]
    for test in tests:
        test()
        print("ok", test.__name__)
    print("%d tests passed (Python %s)" % (len(tests), sys.version.split()[0]))
    return 0


if __name__ == "__main__":
    os.chdir(tempfile.gettempdir())
    sys.exit(main())
