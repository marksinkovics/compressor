# Compressor

This project is a basic implementation of [Huffman coding](https://en.wikipedia.org/wiki/Huffman_coding).

**:warning: It is only tested on macOS (v13.3.1) with Clang.**

## Usage

Compress data:

```bash
./compressor_app encode --input path/to/raw_data --output /path/to/decoded_data
```

Decompress data:

```bash
./compressor_app decode --input path/to/decoded_data --output /path/to/encoded_data
```

For more information, please check the help `compressor_app -h`

### Limitations

- It stores the raw, encoded and/or decoded data into the memory.
- It is designed to run only 1 thread

### :construction: [Development](docs/development.md)