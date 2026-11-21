# ingot

A file compressor and archiver written in C++23, started as a Huffman coder and grown into a
block-based container with several coders, folder archives and search inside archives.

- **Coders:** Huffman, LZ77 + Huffman (Deflate), BWT + rANS, LZMA-style LZR and a context-mixing
  coder, chosen per 1 MiB block; transforms for numeric data, images and machine code.
- **Levels:** `-L 1` (Huffman only, fastest) to `-L 9` (context mixing, about 2% below `xz -9` on
  the Silesia corpus); the default is 6.
- **Folders:** a solid archive with an index, so `list`, `extract`, `cat` and `grep` decode only
  the blocks they need; identical files are stored once.
- **Other formats:** writes `.gz` and `.zip` (Zip64) with its own Deflate, reads `.gz`.
- **Threads:** blocks are coded in parallel (`-T`, all cores by default).

The format is specified in [docs/file_format.md](docs/file_format.md) and measured in
[docs/benchmarks.md](docs/benchmarks.md).

## Usage

```bash
ingot encode -i notes.txt -o notes.txt.ingot          # a file
ingot encode -L 9 -i photos/ -o photos.ingot          # a folder, best compression
ingot decode -i photos.ingot -o photos-copy           # extracts into a new folder
ingot list -l photos.ingot
ingot extract photos.ingot 2024/beach.bmp -o beach.bmp
ingot grep -n "TODO" sources.ingot                   # fixed-string search, like grep -n
ingot cat --range 1000000:1000100 big.log.ingot       # bytes 1,000,000-1,000,099, one block decoded
ingot encode --repair 5 -i data.tar -o data.tar.ingot  # also data.tar.ingot.repair (Reed-Solomon)
ingot encode --long -i backups.tar -o backups.tar.ingot  # copy repeats up to 256 MiB back (--long=BITS)
ingot image encode -i photo.ppm -o photo.ingoti         # lossless images: PGM, PPM or PAM in
ingot image decode -i photo.ingoti -o photo.ppm
ingot image encode --quality 80 -i photo.ppm -o photo.ingotl  # lossy (an experiment)
ingot repair data.tar.ingot                           # undo damage to it, using the .repair file
ingot encode -f gzip -i notes.txt -o notes.txt.gz     # or -f zip for a folder
```

Without `-i` or `-o`, or with `-`, it reads standard input and writes standard output:

```bash
tar c some_dir | ingot encode > some_dir.tar.ingot
ingot decode -i some_dir.tar.ingot | tar x
```

`ingot --help` and `ingot <command> --help` list every command and option.

Many small, similar files compress about twice as well with a dictionary trained on samples of
them:

```bash
ingot train -o configs.dict samples/
ingot encode -D configs.dict -i app.json -o app.json.ingot
ingot decode -D configs.dict -i app.json.ingot -o app.json
```

### Python

```python
import ingot
packed = ingot.compress(data, level=9)
assert ingot.decompress(packed) == data
```

Also streaming (`ingot.Compressor`, `ingot.Decompressor`) and folder archives (`ingot.list`,
`ingot.read`, `ingot.extract`). Built with `cmake --workflow --preset python`, which also writes a
wheel for `pip install`; see [docs/development.md](docs/development.md), "Python module".

## Building

macOS first, Linux (GCC and Clang) checked by hand; needs CMake 3.25+, Ninja and
[vcpkg](https://vcpkg.io) (`VCPKG_ROOT`):

```bash
cmake --workflow --preset release   # build/release/ingot/ingot
```

Development, tests, fuzzing and benchmarks: [docs/development.md](docs/development.md).
