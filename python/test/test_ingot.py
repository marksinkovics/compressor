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
    assert 0 < len(dictionary) <= 2048 + 163  # the content, and a header with its codes
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


def test_streaming_matches_one_shot():
    data = (b"streamed in pieces of any size. " * 70000)[: 3 * 1024 * 1024 + 12345]
    for chunk in (1, 1000, 1 << 20, len(data)):
        compressor = ingot.Compressor(level=2)
        pieces = [compressor.compress(data[i : i + chunk]) for i in range(0, len(data), chunk)] if chunk > 1 else None
        if pieces is None:  # byte by byte is slow in Python; a few bytes then the rest
            pieces = [compressor.compress(data[i : i + 1]) for i in range(100)]
            pieces.append(compressor.compress(data[100:]))
        packed = b"".join(pieces) + compressor.flush()
        assert packed == ingot.compress(data, level=2), chunk
        decompressor = ingot.Decompressor()
        out = [decompressor.decompress(packed[:5])]
        assert out[0] == b""  # not even the frame header yet
        for i in range(5, len(packed), 7777):
            out.append(decompressor.decompress(packed[i : i + 7777]))
        assert decompressor.eof
        out.append(decompressor.flush())
        assert b"".join(out) == data, chunk
    # Bytes after the end, a damaged stream, a short one, and flush twice.
    decompressor = ingot.Decompressor()
    assert decompressor.decompress(ingot.compress(b"abc") + b"tail") == b"abc"
    assert decompressor.eof and decompressor.unused_data == b"tail"
    for broken in (b"\x89ING\x02\x14\x00\x00\x09", ingot.compress(b"x" * 5000)[:-3]):
        try:
            d = ingot.Decompressor()
            d.decompress(broken)
            d.flush()
        except ingot.Error:
            continue
        raise AssertionError("no ingot.Error")
    compressor = ingot.Compressor()
    compressor.flush()
    try:
        compressor.flush()
    except ingot.Error:
        pass
    else:
        raise AssertionError("flush twice")


def test_streaming_with_a_dictionary_and_single_block_frames():
    dictionary = ingot.train_dictionary([record(i) for i in range(300)], size=2048)
    packed = ingot.compress(record(5), dictionary=dictionary)  # a single-block frame
    decompressor = ingot.Decompressor(dictionary=dictionary)
    assert decompressor.decompress(packed) == b""
    assert decompressor.flush() == record(5)
    compressor = ingot.Compressor(dictionary=dictionary)
    streamed = compressor.compress(record(6)) + compressor.flush()
    assert ingot.decompress(streamed, dictionary=dictionary) == record(6)


def test_archives():
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "tree" / "docs").mkdir(parents=True)
        (root / "tree" / "docs" / "a.txt").write_bytes(b"first file\n" * 500)
        (root / "tree" / "b.bin").write_bytes(bytes(range(256)) * 40)
        os.symlink("docs/a.txt", root / "tree" / "link")
        ingot.compress_file(root / "tree", root / "tree.ingot")
        entries = {e["path"]: e for e in ingot.list(root / "tree.ingot")}
        assert entries["docs/a.txt"]["type"] == "file"
        assert entries["docs/a.txt"]["size"] == 11 * 500
        assert entries["docs"]["type"] == "directory"
        assert entries["link"]["target"] == "docs/a.txt"
        assert ingot.read(root / "tree.ingot", "b.bin") == bytes(range(256)) * 40
        ingot.extract(root / "tree.ingot", "docs", root / "docs-copy")
        assert (root / "docs-copy" / "a.txt").read_bytes() == b"first file\n" * 500
        for call in (
            lambda: ingot.read(root / "tree.ingot", "docs"),
            lambda: ingot.read(root / "tree.ingot", "missing"),
            lambda: ingot.list(root / "tree" / "b.bin"),
        ):
            try:
                call()
            except ingot.Error:
                continue
            raise AssertionError("no ingot.Error")


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
