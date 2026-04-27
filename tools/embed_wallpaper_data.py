#!/usr/bin/env python3
"""
Convert wallpaper BMP to C array for embedding in kernel
"""
import sys
import os

def main():
    if len(sys.argv) < 3:
        print("Usage: embed_wallpaper_data.py <wallpaper.bmp> <output.c>")
        sys.exit(1)

    bmp_path = sys.argv[1]
    out_path = sys.argv[2]

    if not os.path.exists(bmp_path):
        print(f"Error: Wallpaper BMP not found: {bmp_path}")
        sys.exit(1)

    # Read wallpaper BMP
    with open(bmp_path, 'rb') as f:
        bmp_data = f.read()

    if len(bmp_data) == 0:
        print("Error: Wallpaper BMP is empty")
        sys.exit(1)

    # Validate BMP signature
    if bmp_data[0:2] != b'BM':
        print(f"Error: Not a valid BMP file")
        sys.exit(1)

    # Parse BMP dimensions
    width = int.from_bytes(bmp_data[18:22], 'little', signed=True)
    height = int.from_bytes(bmp_data[22:26], 'little', signed=True)
    bpp = int.from_bytes(bmp_data[28:30], 'little')
    
    print(f"Wallpaper: {bmp_path}")
    print(f"  Size: {len(bmp_data)} bytes ({len(bmp_data)//1024}KB)")
    print(f"  Dimensions: {width}x{height}")
    print(f"  BPP: {bpp}")

    # Generate C file
    with open(out_path, 'w') as f:
        f.write("/* Auto-generated wallpaper data */\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"const uint32_t wallpaper_data_size = {len(bmp_data)};\n")
        f.write(f"const uint8_t wallpaper_data[] = {{\n")
        
        for i in range(0, len(bmp_data), 16):
            chunk = bmp_data[i:i+16]
            hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
            f.write(f"    {hex_str},\n")
        
        f.write("};\n")

    print(f"Generated: {out_path}")

if __name__ == '__main__':
    main()
