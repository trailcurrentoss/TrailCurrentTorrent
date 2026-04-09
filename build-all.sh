#!/bin/bash
# Build all Torrent address variants (0-2).
# Produces: build/torrent_addr0.bin, torrent_addr1.bin, torrent_addr2.bin
set -e

MAX_ADDR=2
OUTPUT_DIR="build"

for addr in $(seq 0 $MAX_ADDR); do
    echo "========================================"
    echo "Building Torrent address $addr ..."
    echo "========================================"
    idf.py build -DTORRENT_ADDRESS=$addr
    cp "$OUTPUT_DIR/torrent.bin" "$OUTPUT_DIR/torrent_addr${addr}.bin"
    echo ""
done

echo "========================================"
echo "Build complete"
echo "========================================"
ls -lh "$OUTPUT_DIR"/torrent_addr*.bin
