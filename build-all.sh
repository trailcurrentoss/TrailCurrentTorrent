#!/bin/bash
# Build all Torrent address variants (0-2).
# Produces merged binaries (bootloader + partition table + OTA data + app)
# that can be flashed at offset 0x0:
#   build/torrent_addr0.bin, torrent_addr1.bin, torrent_addr2.bin
set -e

MAX_ADDR=2
OUTPUT_DIR="build"

for addr in $(seq 0 $MAX_ADDR); do
    echo "========================================"
    echo "Building Torrent address $addr ..."
    echo "========================================"
    idf.py build -DTORRENT_ADDRESS=$addr

    # Create merged binary (flashable at 0x0, includes all partitions)
    esptool.py --chip esp32 merge_bin -o "$OUTPUT_DIR/torrent_addr${addr}.bin" \
        --flash_mode dio --flash_size 4MB \
        0x1000 "$OUTPUT_DIR/bootloader/bootloader.bin" \
        0x8000 "$OUTPUT_DIR/partition_table/partition-table.bin" \
        0xe000 "$OUTPUT_DIR/ota_data_initial.bin" \
        0x10000 "$OUTPUT_DIR/torrent.bin"
    echo ""
done

echo "========================================"
echo "Build complete"
echo "========================================"
ls -lh "$OUTPUT_DIR"/torrent_addr*.bin
