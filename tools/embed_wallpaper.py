#!/usr/bin/env python3
"""
Embed a BMP into AurionOS HDD image at a known LBA offset.
Default LBA is 10000 (5MB offset) for desktop wallpaper.
"""
import sys
import os

DEFAULT_WALLPAPER_LBA = 10000

def main():
    if len(sys.argv) < 3:
        print("Usage: embed_wallpaper.py <hdd_image> <wallpaper.bmp> [lba]")
        sys.exit(1)

    hdd_path = sys.argv[1]
    bmp_path = sys.argv[2]

    if not os.path.exists(hdd_path):
        print(f"Error: HDD image not found: {hdd_path}")
        sys.exit(1)

    if not os.path.exists(bmp_path):
        print(f"Error: Wallpaper BMP not found: {bmp_path}")
        sys.exit(1)

    target_lba = DEFAULT_WALLPAPER_LBA
    if len(sys.argv) >= 4:
        target_lba = int(sys.argv[3], 10)
        if target_lba <= 0:
            print(f"Error: invalid LBA: {target_lba}")
            sys.exit(1)

    # Read wallpaper BMP
    with open(bmp_path, 'rb') as f:
        bmp_data = f.read()

    if len(bmp_data) == 0:
        print("Error: Wallpaper BMP is empty")
        sys.exit(1)

    # Validate BMP signature
    if bmp_data[0:2] != b'BM':
        print(f"Error: Not a valid BMP file (got magic: {bmp_data[0:2]})")
        sys.exit(1)

    # Parse BMP dimensions
    width = int.from_bytes(bmp_data[18:22], 'little', signed=True)
    height = int.from_bytes(bmp_data[22:26], 'little', signed=True)
    bpp = int.from_bytes(bmp_data[28:30], 'little')
    print(f"Wallpaper: {bmp_path}")
    print(f"  Size: {len(bmp_data)} bytes ({len(bmp_data)//1024}KB)")
    print(f"  Dimensions: {width}x{height}")
    print(f"  BPP: {bpp}")

    # Check if BMP fits in HDD
    hdd_size = os.path.getsize(hdd_path)
    target_offset = target_lba * 512
    if target_offset + len(bmp_data) > hdd_size:
        print(f"Error: Wallpaper ({len(bmp_data)} bytes) won't fit at LBA {target_lba}")
        print(f"  HDD size: {hdd_size} bytes")
        print(f"  Required: {target_offset + len(bmp_data)} bytes")
        sys.exit(1)

    # Embed wallpaper into HDD image
    with open(hdd_path, 'r+b') as f:
        f.seek(target_offset)
        f.write(bmp_data)

    print(f"Embedded at LBA {target_lba} (offset {target_offset})")

if __name__ == '__main__':
    main()