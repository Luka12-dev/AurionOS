# Use only if you have icons in icons/bmp/

import os
from PIL import Image
import struct

def convert_to_rle(bmp_path, rle_path):
    print(f"Converting {bmp_path} to {rle_path}...")
    try:
        img = Image.open(bmp_path).convert('RGBA')
        img = img.resize((64, 64), Image.Resampling.LANCZOS)
        
        pixels = []
        for y in range(64):
            for x in range(64):
                r, g, b, a = img.getpixel((x, y))
                # BGRA format
                pixels.append((b, g, r, a))
        
        rle_data = bytearray()
        i = 0
        while i < len(pixels):
            count = 1
            pixel = pixels[i]
            while i + count < len(pixels) and pixels[i + count] == pixel and count < 255:
                count += 1
            
            # Write count (byte)
            rle_data.append(count)
            # Write BGRA (4 bytes)
            rle_data.append(pixel[0])
            rle_data.append(pixel[1])
            rle_data.append(pixel[2])
            rle_data.append(pixel[3])
            
            i += count
            
        with open(rle_path, 'wb') as f:
            f.write(rle_data)
        print(f"Done. {len(rle_data)} bytes.")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    to_convert = [
        'terminal', 'Browser', 'notepad', 'calculator', 'files',
        'clock', 'paint', 'sys-info', 'folder', 'file_on_desktop',
        'settings', 'snake'
    ]
    for name in to_convert:
        bmp = f"icons/bmp/{name}.bmp"
        rle = f"icons/{name}.rle"
        if os.path.exists(bmp):
            convert_to_rle(bmp, rle)
        else:
            print(f"File {bmp} not found.")
