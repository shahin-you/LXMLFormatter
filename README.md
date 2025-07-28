# XML Formatter

A high-performance XML formatter designed for extremely large files (hundreds of megabytes to gigabytes). Built with C++17 for speed and reliability.

## Features

- **Streaming Architecture**: Never loads entire file into memory
- **Unicode Support**: Full UTF-8 support with BOM detection
- **Fast Performance**: Optimized for gigabyte-sized files
- **Memory Efficient**: Uses only ~11MB regardless of file size
- **Safe**: No buffer overflows, proper error handling

## Requirements

- GCC 13.3.0 or later
- C++17 standard library
- GNU Make

## Building

```bash
# Build release version (optimized)
make

# Build debug version (with sanitizers)
make debug

# Run tests
make test

# Clean build files
make clean
```

## Usage

```bash
# Format an XML file
./bin/release/xmlformatter input.xml output.xml

# Format with custom options (once implemented)
./bin/release/xmlformatter --indent=4 --tabs input.xml output.xml
```

## Project Structure

```
.
├── bin/              # Build outputs
│   ├── debug/        # Debug builds
│   └── release/      # Release builds
├── src/              # Source code
│   ├── BufferedInputStream.h
│   ├── BufferedOutputStream.h
│   └── ...
├── tests/            # Unit tests
├── Makefile
├── LICENSE
└── README.md
```

## Development

```bash
# Format code (requires clang-format)
make format

# Run static analysis (requires cppcheck)
make analyze

# Generate compile_commands.json for IDEs (requires bear)
make compiledb
```

## Performance

Designed to handle gigabyte-sized XML files with:
- ~50-100 MB/s throughput on modern hardware
- Constant memory usage (~11MB)
- Efficient buffering strategy to minimize system calls

## License

[Your License Here]

## Contributing

[Contributing guidelines here]